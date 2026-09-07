#ifndef TERMO_TEST_HARNESS_H
#define TERMO_TEST_HARNESS_H

/* Server globals without a server: options with defaults, environ, libevent. */
extern struct event_base	*libevent;

void	termo_test_init(void);
void	termo_test_reset(void);

#endif
