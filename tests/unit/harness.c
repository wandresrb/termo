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
	struct paste_buffer	*pb;

	options_free(global_options);
	options_free(global_s_options);
	options_free(global_w_options);
	options_defaults();
	while ((pb = paste_get_top(nullptr)) != nullptr)
		paste_free(pb);
	input_set_buffer_size(INPUT_BUF_DEFAULT_SIZE);
}

void
termo_test_drain(void)
{
	while (cmdq_next(nullptr) != 0)
		;
	event_base_loop(libevent, EVLOOP_NONBLOCK);
}

struct window *
termo_test_window(u_int sx, u_int sy, u_int npanes)
{
	struct window		*w = window_create(sx, sy, 0, 0);
	struct window_pane	*wp, *prev;
	struct layout_cell	*lc;
	u_int			 i;

	window_add_ref(w, __func__);
	prev = window_add_pane(w, nullptr, 0, 0);
	window_set_active_pane(w, prev, 0);
	layout_init(w, prev);
	for (i = 1; i < npanes; i++) {
		lc = layout_split_pane(prev, LAYOUT_LEFTRIGHT, -1, 0);
		if (lc == nullptr)
			break;
		wp = window_add_pane(w, prev, 0, 0);
		layout_assign_pane(lc, wp, 0);
		prev = wp;
	}
	return (w);
}

void
termo_test_window_free(struct window *w)
{
	window_remove_ref(w, __func__);
}

struct session *
termo_test_session(const char *name, struct window *w)
{
	struct session	*s;
	char		*cause;

	s = session_create(nullptr, name, "/", environ_create(),
	    options_create(global_s_options), nullptr);
	s->curw = session_attach(s, w, -1, &cause);
	termo_test_drain();
	return (s);
}

/* session_destroy frees the session from a libevent callback. */
void
termo_test_session_free(struct session *s)
{
	session_destroy(s, 0, __func__);
	termo_test_drain();
}

static enum cmd_retval
noop_cb([[maybe_unused]] struct cmdq_item *item, [[maybe_unused]] void *data)
{
	return (CMD_RETURN_NORMAL);
}

struct cmdq_item *
termo_test_item(void)
{
	return (cmdq_get_callback(noop_cb, nullptr));
}

void
termo_test_item_free(struct cmdq_item *item)
{
	cmdq_append(nullptr, item);
	termo_test_drain();
}

void
termo_test_wide(struct grid_cell *gc, const char *s)
{
	enum utf8_state	st;

	memcpy(gc, &grid_default_cell, sizeof *gc);
	st = utf8_open(&gc->data, *s++);
	while (st == UTF8_MORE)
		st = utf8_append(&gc->data, *s++);
}
