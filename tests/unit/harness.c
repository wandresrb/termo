#include <sys/types.h>

#include <locale.h>
#include <string.h>

#include "termo.h"
#include "harness.h"

struct event_base	*libevent;

static void
options_defaults(void)
{
	const struct options_table_entry	*oe;

	global_options = options_create(nullptr);
	global_s_options = options_create(nullptr);
	global_w_options = options_create(nullptr);
	for (oe = options_table; oe->name != nullptr; oe++) {
		if (oe->scope & OPTIONS_TABLE_SERVER)
			options_default(global_options, oe);
		if (oe->scope & OPTIONS_TABLE_SESSION)
			options_default(global_s_options, oe);
		if (oe->scope & OPTIONS_TABLE_WINDOW)
			options_default(global_w_options, oe);
	}
}

void
termo_test_init(void)
{
	if (global_options != nullptr)
		return;
	if (setlocale(LC_CTYPE, "en_US.UTF-8") == nullptr &&
	    setlocale(LC_CTYPE, "C.UTF-8") == nullptr)
		setlocale(LC_CTYPE, "");
	global_environ = environ_create();
	options_defaults();
	utf8_update_width_cache();
	libevent = osdep_event_init();
	socket_path = xstrdup("termo-test");
}

/* Fresh option trees so one test cannot leak state into the next. */
void
termo_test_reset(void)
{
	options_free(global_options);
	options_free(global_s_options);
	options_free(global_w_options);
	options_defaults();
}
