#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "snd-mixer.h"
#include "snd-stream.h"

struct snd_client;
struct snd_command;
struct snd_service;

typedef void (*snd_callback_t)(void *ctx);

void snd_command_free(struct snd_command *cmd);
void snd_command_play(
        struct snd_command *cmd,
        struct snd_stream *stm,
        bool loop);
void snd_command_stop(struct snd_command *cmd, struct snd_stream *stm);
void snd_command_set_volume(
        struct snd_command *cmd,
        struct snd_stream *stm,
        size_t channel_no,
        uint8_t value);
void snd_command_set_callback(
        struct snd_command *cmd,
        snd_callback_t callback,
        void *ctx);

int snd_service_alloc(struct snd_service **out);
void snd_service_free(struct snd_service *svc);
void snd_service_intake(struct snd_service *svc, struct snd_mixer *m);
void snd_service_exhaust(struct snd_service *svc);

int snd_client_alloc(struct snd_client **out, struct snd_service *svc);
void snd_client_free(struct snd_client *cli);
int snd_client_cmd_alloc(struct snd_client *cli, struct snd_command **out);
void snd_client_cmd_submit(struct snd_client *cli, struct snd_command *cmd);
