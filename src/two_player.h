/*
 * Copyright © 2026 Ian D. Romanick
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef TWO_PLAYER_H
#define TWO_PLAYER_H

#include "my_int.h"

/* Message type identifiers.
 *
 * msgsnd and msgrcv require that the message type be non-zero.
 */
enum tetris_message_types {
    /* Sent by player 2 to initiate the connection. Sets the initial randomizer
     * seed.
     */
    SET_RNG_SEED = 1,

    /* Sent by player 1 to acknowledge SET_RNG_SEED. */
    ACK_RNG_SEED,

    /* Sent by both players to acknowledge that they are ready to begin
     * play. This may wait for user input, so the delay may be arbitrary. If the
     * delay is too long, SEND_SCORE must be sent to keep the connection
     * alive. See below.
     */
    READY_TO_PLAY,

    /* Sent by both players to inform the other player of their score. This is
     * also the generic heartbeat message. A SEND_SCORE message must be received
     * at least every 5 seconds, or it will be assumed that the other player
     * dropped the connection.
     */
    SEND_SCORE,

    /* Sent by both players to notify the other player that their game has
     * ended. Once both players have sent MY_GAME_OVER messages, the connection
     * is dropped.
     *
     * Note: If the other player is still playing, SEND_SCORE messages must
     * continue to be sent to keep the connection active.
     */
    MY_GAME_OVER,
};

struct tetris_message {
    long msg_type;
    int qid;
    uint32_t msg_data[2];
};

extern int connect_to_other_game(uint16_t *seed);
extern int send_ready(void);
extern int send_game_over(uint32_t score);
extern int send_score(uint32_t score, uint16_t garbage_lines);
extern int poll_message(struct tetris_message *tm);

#endif /* TWO_PLAYER_H */
