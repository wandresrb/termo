#ifndef TERMO_TEST_HARNESS_H
#define TERMO_TEST_HARNESS_H

/* Server globals without a server: options with defaults, environ, libevent. */
extern struct event_base	*libevent;

void	termo_test_init(void);
void	termo_test_reset(void);

/* Run queued commands and pending libevent callbacks. */
void	termo_test_drain(void);

/* A window with n tiled panes split left-to-right, no ptys. */
struct window	*termo_test_window(u_int, u_int, u_int);
void		 termo_test_window_free(struct window *);

/* A session with the window attached as its current window. */
struct session	*termo_test_session(const char *, struct window *);
void		 termo_test_session_free(struct session *);

/* A detached callback item, for code that dereferences its item. */
struct cmdq_item *termo_test_item(void);
void		  termo_test_item_free(struct cmdq_item *);

/* Decode one UTF-8 character into a default cell. */
void	termo_test_wide(struct grid_cell *, const char *);

#endif
