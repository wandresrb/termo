#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "termo.h"
#include "harness.h"

enum utf8_state termo_c_utf8_open(struct utf8_data *, u_char);
enum utf8_state termo_c_utf8_append(struct utf8_data *, u_char);

int
LLVMFuzzerTestOneInput(const u_char *data, size_t size)
{
	struct utf8_data	c, r;
	enum utf8_state		cs, rs;
	size_t			i;

	if (size > 512)
		return 0;
	memset(&c, 0, sizeof c);
	memset(&r, 0, sizeof r);
	cs = rs = UTF8_ERROR;
	for (i = 0; i < size; i++) {
		if (cs == UTF8_MORE) {
			cs = termo_c_utf8_append(&c, data[i]);
			rs = utf8_append(&r, data[i]);
		} else {
			cs = termo_c_utf8_open(&c, data[i]);
			rs = utf8_open(&r, data[i]);
		}
		if (cs != rs || memcmp(&c, &r, sizeof c) != 0)
			abort();
	}
	return 0;
}

int
LLVMFuzzerInitialize([[maybe_unused]] int *argc, [[maybe_unused]] char ***argv)
{
	termo_test_init();
	return 0;
}
