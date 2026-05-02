/*
 * Copyright © 2026 Ian D. Romanick
 * SPDX-License-Identifier: GPL-3.0-only
 */

/**
 * \file
 * Implementation of message passing for two-player Tetris on UNIX.
 */
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>

#include "two_player.h"

static int my_qid = -1;
static int other_qid = -1;
static const char *msg_file = "/tmp/tetris-xxx";

void
fully_disconnect()
{
    if (my_qid >= 0) {
        msgctl(my_qid, IPC_RMID, NULL);
        my_qid = -1;
    }
}

int
poll_message(struct tetris_message *tm)
{
    if (msgrcv(my_qid, tm, sizeof(*tm) - sizeof(long), 0, IPC_NOWAIT) == -1)
        return -1;

    return tm->qid == other_qid ? 1 : 0;
}

int
send_ready(void)
{
    struct tetris_message tm;

    tm.msg_type = READY_TO_PLAY;
    tm.qid = my_qid;
    tm.msg_data[0] = 0;
    tm.msg_data[1] = 0;

    return msgsnd(other_qid, &tm, sizeof(tm) - sizeof(long), 0);
}

int
send_game_over(uint32_t score)
{
    struct tetris_message tm;

    tm.msg_type = MY_GAME_OVER;
    tm.qid = my_qid;
    tm.msg_data[0] = score;
    tm.msg_data[1] = 0;

    return msgsnd(other_qid, &tm, sizeof(tm) - sizeof(long), 0);
}

int
send_score(uint32_t score, uint16_t garbage_lines)
{
    struct tetris_message tm;

    tm.msg_type = SEND_SCORE;
    tm.qid = my_qid;
    tm.msg_data[0] = score;
    tm.msg_data[1] = garbage_lines;

    return msgsnd(other_qid, &tm, sizeof(tm) - sizeof(long), 0);
}

int
connect_to_other_game(uint16_t *seed)
{
    int ret;
    key_t k;
    struct tetris_message tm;

    umask(0);
    atexit(fully_disconnect);

#ifdef S_IRUSR
#define PERM (S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH)
#else
#define PERM 0666
#endif

    ret = creat(msg_file, PERM);
    if (ret < 0 && errno != EEXIST) {
        perror("Create tetris-a");
        exit(1);
    }

    k = ftok(msg_file, 'a');
    if (k < 0) {
        perror("ftok tetris-a");
        exit(1);
    }

    /* Try first to be the server. */
    my_qid = msgget(k, IPC_CREAT | IPC_EXCL | PERM);
    if (my_qid < 0) {
        if (errno != EEXIST) {
            perror("msgget");
            return -1;
        }

        /* Fall back to being the client. */
        my_qid = msgget(IPC_PRIVATE, PERM);
        if (my_qid < 0) {
            perror("msgget - client");
            return -1;
        }

        /* Connect to server. */
        other_qid = msgget(k, PERM);
        if (other_qid < 0) {
            perror("msgget - server");
            return -1;
        }

        /* Tell server who we are. */
        tm.msg_type = SET_RNG_SEED;
        tm.qid = my_qid;
        tm.msg_data[0] = *seed;

        if (msgsnd(other_qid, &tm, sizeof(tm) - sizeof(long), 0) == -1) {
            perror("msgsnd - RNG seed");
            return -1;
        }

        if (msgrcv(my_qid, &tm, sizeof(tm) - sizeof(long), 0, 0) == -1) {
            perror("msgrcv - client waiting for ack rng seed");
            return -1;
        }

        if (tm.qid != other_qid || tm.msg_type != ACK_RNG_SEED)
            return -1;
    } else {
        ret = msgrcv(my_qid, &tm, sizeof(tm) - sizeof(long), 0, 0);

        unlink(msg_file);

        if (ret == -1) {
            perror("msgrcv - server waiting for RNG seed");
            return -1;
        }

        if (tm.msg_type != SET_RNG_SEED) {
            return -1;
        }

        other_qid = tm.qid;

        *seed = tm.msg_data[0];

        tm.msg_type = ACK_RNG_SEED;
        tm.qid = my_qid;

        if (msgsnd(other_qid, &tm, sizeof(tm) - sizeof(long), 0) == -1) {
            perror("msgsnd - ack rng seed");
            return -1;
        }
    }

    return 0;
}
