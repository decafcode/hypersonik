#include <windows.h>

#include <assert.h>
#include <process.h>
#include <stdbool.h>
#include <stdlib.h>

#include "defs.h"
#include "hr.h"
#include "reaper.h"
#include "snd-buffer.h"
#include "snd-service.h"
#include "snd-stream.h"
#include "trace.h"

struct reaper {
    CRITICAL_SECTION lock;
    CONDITION_VARIABLE cond;
    HANDLE fence;
    HANDLE thread;
    struct snd_client *cli;
    struct list *tasks;
    struct list *tasks_pending;
    bool stop;
};

struct reaper_task {
    struct list_node node;
    struct snd_command *cmd;
    struct snd_stream *stm;
    struct snd_buffer *buf;
};

static unsigned int __stdcall reaper_thread_main(void *ctx);
static void reaper_thread_submit_commands(struct reaper *reaper);
static void reaper_thread_destroy_resources(struct list_node *node);
static void reaper_signal_fence(void *ctx);

HRESULT reaper_alloc(
        struct reaper **out,
        struct snd_client *cli)
{
    struct reaper *reaper;
    HRESULT hr;
    int r;

    assert(out != NULL);
    assert(cli != NULL);

    *out = NULL;

    reaper = calloc(1, sizeof(*reaper));

    if (reaper == NULL) {
        hr = E_OUTOFMEMORY;

        goto end;
    }

    InitializeCriticalSection(&reaper->lock);
    InitializeConditionVariable(&reaper->cond);

    reaper->fence = CreateEventW(NULL, TRUE, FALSE, NULL);

    if (reaper->fence == NULL) {
        hr = hr_from_win32();
        hr_trace("CreateEventW", hr);

        goto end;
    }

    r = list_alloc(&reaper->tasks);

    if (r < 0) {
        hr = hr_from_errno(r);

        goto end;
    }

    r = list_alloc(&reaper->tasks_pending);

    if (r < 0) {
        hr = hr_from_errno(r);

        goto end;
    }

    reaper->cli = cli;

    *out = reaper;
    reaper = NULL;
    hr = S_OK;

end:
    reaper_free(reaper);

    return hr;
}

void reaper_free(struct reaper *reaper)
{
    DWORD result;
    HRESULT hr;

    if (reaper == NULL) {
        return;
    }

    if (reaper->thread != NULL) {
        trace("Stopping reaper thread");

        EnterCriticalSection(&reaper->lock);
        reaper->stop = true;
        WakeConditionVariable(&reaper->cond);
        LeaveCriticalSection(&reaper->lock);

        result = WaitForSingleObject(reaper->thread, INFINITE);

        if (result != WAIT_OBJECT_0) {
            hr = hr_from_win32();
            hr_trace("WaitForSingleObject", hr);
            abort();
        }

        CloseHandle(reaper->thread);

        trace("Reaper thread stopped");
    }

    if (reaper->fence != NULL) {
        CloseHandle(reaper->fence);
    }

    assert( reaper->tasks == NULL || list_is_empty(reaper->tasks));
    assert( reaper->tasks_pending == NULL ||
            list_is_empty(reaper->tasks_pending));

    list_free(reaper->tasks, NULL);
    list_free(reaper->tasks_pending, NULL);

    /* Apparently win32 condition variables do not need to be destroyed */
    DeleteCriticalSection(&reaper->lock);
    free(reaper);
}

HRESULT reaper_start(struct reaper *reaper)
{
    HRESULT hr;

    assert(reaper != NULL);
    assert(reaper->thread == NULL);

    trace("Starting reaper thread");

    reaper->thread = (HANDLE) _beginthreadex(
            NULL,
            0,
            reaper_thread_main,
            reaper,
            0,
            NULL);

    if (reaper->thread == NULL) {
        hr = hr_from_win32();
        hr_trace("_beginthreadex", hr);

        return hr;
    }

    return S_OK;
}

HRESULT reaper_alloc_task(
        struct reaper *reaper,
        struct reaper_task **out,
        struct snd_stream *stm,
        struct snd_buffer *buf)
{
    struct reaper_task *task;
    HRESULT hr;
    int r;

    assert(reaper != NULL);
    assert(out != NULL);
    assert(stm != NULL);
    /* buf can be NULL */

    *out = NULL;
    task = calloc(1, sizeof(*task));

    if (task == NULL) {
        hr = E_OUTOFMEMORY;

        goto end;
    }

    r = snd_client_cmd_alloc(reaper->cli, &task->cmd);

    if (r < 0) {
        hr = hr_from_errno(r);

        goto end;
    }

    list_node_init(&task->node);
    task->stm = stm;
    task->buf = buf;

    *out = task;
    task = NULL;
    hr = S_OK;

end:
    reaper_task_discard(task);

    return hr;
}

void reaper_submit_task(
        struct reaper *reaper,
        struct reaper_task *task)
{
    bool transition;

    assert(reaper != NULL);
    assert(task != NULL);
    assert(!list_node_is_inserted(&task->node));

    EnterCriticalSection(&reaper->lock);

    transition = list_is_empty(reaper->tasks_pending);
    list_append(reaper->tasks_pending, &task->node);

    if (transition) {
        WakeConditionVariable(&reaper->cond);
    }

    LeaveCriticalSection(&reaper->lock);
}

static unsigned int __stdcall reaper_thread_main(void *ctx)
{
    struct reaper *reaper;
    bool stop;
    BOOL ok;

    trace("Started reaper thread");

    reaper = ctx;

    ok = SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);

    if (!ok) {
        hr_trace("SetThreadPriority", hr_from_win32());
    }

    do {
        EnterCriticalSection(&reaper->lock);

        if (!reaper->stop && list_is_empty(reaper->tasks_pending)) {
            ok = SleepConditionVariableCS(
                    &reaper->cond,
                    &reaper->lock,
                    INFINITE);

            if (!ok) {
                hr_trace("SleepConditionVariableCS", hr_from_win32());
                abort();
            }
        }

        stop = reaper->stop;
        list_move(reaper->tasks, reaper->tasks_pending);

        LeaveCriticalSection(&reaper->lock);

        /* Send a batch of stop commands */

        reaper_thread_submit_commands(reaper);

        /* We can now safely release this batch of resources */

        list_clear(reaper->tasks, reaper_thread_destroy_resources);

        /* If we were told to stop then stop here, in order to ensure that all
           queued deletions have been processed. No new tasks could have come in
           since the stop flag was raised, since the last reference to the
           reaper is currently being held by the destructor. */
    } while (!stop);

    trace("Reaper thread is exiting");

    return 0;
}

static void reaper_thread_submit_commands(struct reaper *reaper)
{
    struct reaper_task *task;
    struct reaper_task *task_prev;
    struct list_node *node;
    struct list_iter iter;
    DWORD result;
    BOOL ok;

    task_prev = NULL;

    for (   list_iter_init(&iter, reaper->tasks) ;
            list_iter_is_valid(&iter) ;
            list_iter_next(&iter)) {
        if (task_prev != NULL) {
            snd_client_cmd_submit(reaper->cli, task_prev->cmd);
            task_prev->cmd = NULL;
        }

        node = list_iter_deref(&iter);
        task = containerof(node, struct reaper_task, node);

        snd_command_stop(task->cmd, task->stm);

        task_prev = task;
    }

    if (task_prev == NULL) {
        return;
    }

    /* The final command needs to signal our fence object once it has been
       processed so that we know it is safe to proceed */

    snd_command_set_callback(task_prev->cmd, reaper_signal_fence, reaper);
    snd_client_cmd_submit(reaper->cli, task_prev->cmd);
    task_prev->cmd = NULL;

    /* Wait for the final command to finish executing. The entire reaper
       mechanism exists so that we can eat this multi-millisecond delay on a
       dedicated thread instead of blocking the application. */

    result = WaitForSingleObject(reaper->fence, INFINITE);

    if (result != WAIT_OBJECT_0) {
        hr_trace("WaitForSingleObject", hr_from_win32());
        abort();
    }

    ok = ResetEvent(reaper->fence);

    if (!ok) {
        hr_trace("ResetEvent", hr_from_win32());
        abort();
    }
}

static void reaper_thread_destroy_resources(struct list_node *node)
{
    struct reaper_task *task;

    task = containerof(node, struct reaper_task, node);

    snd_stream_free(task->stm);
    snd_buffer_free(task->buf);
    free(task);
}

static void reaper_signal_fence(void *ctx)
{
    struct reaper *reaper;
    BOOL ok;

    reaper = ctx;
    ok = SetEvent(reaper->fence);

    if (!ok) {
        hr_trace("SetEvent", hr_from_win32());
        abort();
    }
}

void reaper_task_discard(struct reaper_task *task)
{
    if (task == NULL) {
        return;
    }

    snd_command_free(task->cmd);
    list_node_fini(&task->node);
    free(task);
}
