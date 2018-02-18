#include <assert.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "defs.h"
#include "queue.h"
#include "snd-mixer.h"
#include "snd-service.h"
#include "snd-stream.h"
#include "trace.h"

enum snd_command_type {
    SND_COMMAND_INVALID,
    SND_COMMAND_PLAY,
    SND_COMMAND_STOP,
    SND_COMMAND_SET_VOLUME,
};

struct snd_command {
    struct qitem qi;
    struct snd_stream *stm;

    union {
        uint16_t volumes[2];
        bool loop;
    };

    snd_callback_t callback;
    void *callback_ctx;
    enum snd_command_type type;
};

struct snd_service {
    struct queue_shared *cmds_intake;
    struct queue_private *cmds_chamber;
    struct queue_shared *cmds_exhaust;
};

struct snd_client {
    struct snd_service *svc;
    struct queue_private *cmd_pool;
};

static int snd_command_alloc(struct snd_command **out);
static struct snd_command *snd_command_downcast(struct qitem *qi);
static struct qitem *snd_command_upcast(struct snd_command *cmd);
static void snd_command_clear(struct snd_command *cmd);

static void snd_service_cmd_dtor(struct qitem *qi);

static int snd_command_alloc(struct snd_command **out)
{
    struct snd_command *cmd;

    assert(out != NULL);

    *out = NULL;
    cmd = calloc(sizeof(*cmd), 1);

    if (cmd == NULL) {
        return -ENOMEM;
    }

    qitem_init(&cmd->qi);
    snd_command_clear(cmd);
    *out = cmd;

    return 0;
}

void snd_command_free(struct snd_command *cmd)
{
    if (cmd == NULL) {
        return;
    }

    qitem_fini(&cmd->qi);
    free(cmd);
}

static struct snd_command *snd_command_downcast(struct qitem *qi)
{
    assert(qi != NULL);

    return containerof(qi, struct snd_command, qi);
}

static struct qitem *snd_command_upcast(struct snd_command *cmd)
{
    assert(cmd != NULL);

    return &cmd->qi;
}

static void snd_command_clear(struct snd_command *cmd)
{
    assert(cmd != NULL);
    assert(!qitem_is_queued(&cmd->qi));

    memset(cmd, 0, sizeof(*cmd));
    qitem_init(&cmd->qi);
    cmd->volumes[0] = 0x100;
    cmd->volumes[1] = 0x100;
}

void snd_command_play(
        struct snd_command *cmd,
        struct snd_stream *stm,
        bool loop)
{
    assert(cmd != NULL);

    cmd->type = SND_COMMAND_PLAY;
    cmd->stm = stm;
    cmd->loop = loop;
}

void snd_command_stop(struct snd_command *cmd, struct snd_stream *stm)
{
    assert(cmd != NULL);

    cmd->type = SND_COMMAND_STOP;
    cmd->stm = stm;
}

void snd_command_set_volume(
        struct snd_command *cmd,
        struct snd_stream *stm,
        size_t channel_no,
        uint8_t value)
{
    assert(cmd != NULL);

    cmd->type = SND_COMMAND_SET_VOLUME;
    cmd->stm = stm;
    cmd->volumes[channel_no] = value;
}

void snd_command_set_callback(
        struct snd_command *cmd,
        snd_callback_t callback,
        void *ctx)
{
    assert(cmd != NULL);

    cmd->callback = callback;
    cmd->callback_ctx = ctx;
}

int snd_service_alloc(struct snd_service **out)
{
    struct snd_service *svc;
    int r;

    trace_enter();
    assert(out != NULL);

    *out = NULL;
    svc = calloc(sizeof(*svc), 1);

    if (svc == NULL) {
        r = -ENOMEM;

        goto end;
    }

    r = queue_shared_alloc(&svc->cmds_intake);

    if (r < 0) {
        goto end;
    }

    r = queue_private_alloc(&svc->cmds_chamber);

    if (r < 0) {
        goto end;
    }

    r = queue_shared_alloc(&svc->cmds_exhaust);

    if (r < 0) {
        goto end;
    }

    *out = svc;
    svc = NULL;

end:
    snd_service_free(svc);
    trace_exit();

    return r;
}

void snd_service_free(struct snd_service *svc)
{
    if (svc == NULL) {
        return;
    }

    queue_shared_free(svc->cmds_intake, snd_service_cmd_dtor);
    queue_private_free(svc->cmds_chamber, snd_service_cmd_dtor);
    queue_shared_free(svc->cmds_exhaust, snd_service_cmd_dtor);
    free(svc);
}

static void snd_service_cmd_dtor(struct qitem *qi)
{
    assert(qi != NULL);

    snd_command_free(snd_command_downcast(qi));
}

void snd_service_intake(struct snd_service *svc, struct snd_mixer *m)
{
    const struct snd_command *cmd;
    struct queue_private_iter i;

    assert(svc != NULL);
    assert(m != NULL);

    queue_private_move_from_shared(svc->cmds_chamber, svc->cmds_intake);

    for (   queue_private_iter_init(&i, svc->cmds_chamber) ;
            queue_private_iter_is_valid(&i) ;
            queue_private_iter_next(&i)) {
        cmd = snd_command_downcast(queue_private_iter_deref(&i));

        switch (cmd->type) {
        case SND_COMMAND_PLAY:
            snd_stream_set_looping(cmd->stm, cmd->loop);
            snd_mixer_play(m, cmd->stm);

            break;

        case SND_COMMAND_STOP:
            snd_mixer_stop(m, cmd->stm);

            break;

        case SND_COMMAND_SET_VOLUME:
            snd_stream_set_volume(cmd->stm, 0, cmd->volumes[0]);
            snd_stream_set_volume(cmd->stm, 1, cmd->volumes[1]);

            break;

        default:
            abort();
        }
    }
}

void snd_service_exhaust(struct snd_service *svc)
{
    const struct snd_command *cmd;
    struct queue_private_iter i;

    assert(svc != NULL);

    for (   queue_private_iter_init(&i, svc->cmds_chamber) ;
            queue_private_iter_is_valid(&i) ;
            queue_private_iter_next(&i)) {
        cmd = snd_command_downcast(queue_private_iter_deref(&i));

        if (cmd->callback != NULL) {
            cmd->callback(cmd->callback_ctx);
        }
    }

    queue_shared_move_from_private(svc->cmds_exhaust, svc->cmds_chamber);
}

int snd_client_alloc(struct snd_client **out, struct snd_service *svc)
{
    struct snd_client *cli;
    int r;

    assert(out != NULL);
    assert(svc != NULL);

    *out = NULL;
    cli = calloc(sizeof(*cli), 1);

    if (cli == NULL) {
        r = -ENOMEM;

        goto end;
    }

    r = queue_private_alloc(&cli->cmd_pool);

    if (r < 0) {
        goto end;
    }

    cli->svc = svc;
    *out = cli;
    cli = NULL;

end:
    snd_client_free(cli);

    return r;
}

void snd_client_free(struct snd_client *cli)
{
    if (cli == NULL) {
        return;
    }

    queue_private_free(cli->cmd_pool, snd_service_cmd_dtor);
    free(cli);
}

int snd_client_cmd_alloc(struct snd_client *cli, struct snd_command **out)
{
    struct snd_command *cmd;
    struct qitem *qi;

    assert(cli != NULL);
    assert(out != NULL);

    /*  A lockless queue_shared_pop() would be quite intricate, if it is
        even possible at all; to pop from a shared linked stack we need to
        (1) get the current head, (2) dereference our conditionally-accessed
        current head to get its next link, and (3) atomically CAS the stack
        head with that next value. But between (1) and (2) we can get pre-
        empted by another thread that pops the same head that we're looking at
        and then, say, de-allocates it. Whereupon we resume at step (2) and
        access invalid memory.

        So instead we have to recycle command objects in two steps: first we
        spill the shared exhaust queue into a private pool, then we can safely
        pop from that private pool until it is empty. */

    *out = NULL;

    if (queue_private_is_empty(cli->cmd_pool)) {
        queue_private_move_from_shared(
                cli->cmd_pool,
                cli->svc->cmds_exhaust);
    }

    qi = queue_private_pop(cli->cmd_pool);

    if (qi == NULL) {
        return snd_command_alloc(out);
    }

    cmd = snd_command_downcast(qi);
    snd_command_clear(cmd);
    *out = cmd;

    return 0;
}

void snd_client_cmd_submit(struct snd_client *cli, struct snd_command *cmd)
{
    assert(cli != NULL);
    assert(cmd != NULL);

    queue_shared_push(cli->svc->cmds_intake, snd_command_upcast(cmd));
}
