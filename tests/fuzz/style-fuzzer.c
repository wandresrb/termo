/*
 * Copyright (c) 2026 David Korczynski <david@adalogics.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Fuzz the tmux style parser (style_parse).
 *
 * This exercises:
 *   - style.c (style string parsing, alignment, ranges)
 *   - colour.c (colour name, RGB, and indexed colour parsing)
 */

#include <stddef.h>
#include <string.h>

#include "termo.h"
#include "harness.h"


int
LLVMFuzzerTestOneInput(const u_char *data, size_t size)
{
	struct style		 sy;
	struct grid_cell	 gc;
	char			*buf;

	if (size > 512 || size == 0)
		return 0;

	/* Null-terminate the input for style_parse. */
	buf = malloc(size + 1);
	if (buf == NULL)
		return 0;
	memcpy(buf, data, size);
	buf[size] = '\0';

	memset(&gc, 0, sizeof gc);
	style_set(&sy, &gc);

	style_parse(&sy, &gc, buf);

	free(buf);
	return 0;
}

int
LLVMFuzzerInitialize(__unused int *argc, __unused char ***argv)
{
	termo_test_init();

	return 0;
}
