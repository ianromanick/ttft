/*
 * Copyright © 2026 Ian D. Romanick
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MY_BOOL_H
#define MY_BOOL_H

#ifdef HAVE_STDBOOL_H
#include <stdbool.h>
#else
typedef unsigned char bool;
#define true    1
#define false   0
#endif

#endif /* MY_BOOL_H */
