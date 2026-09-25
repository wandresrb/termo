/* $OpenBSD: xmalloc.h,v 1.5 2026/06/18 10:56:22 nicm Exp $ */

/*
 * Author: Tatu Ylonen <ylo@cs.hut.fi>
 * Copyright (c) 1995 Tatu Ylonen <ylo@cs.hut.fi>, Espoo, Finland
 *                    All rights reserved
 * Created: Mon Mar 20 22:09:17 1995 ylo
 *
 * Versions of malloc and friends that check their results, and never return
 * failure (they call fatal if they encounter an error).
 *
 * As far as I am concerned, the code I have written for this software
 * can be used freely for any purpose.  Any derived versions of this
 * software must be clearly marked as such, and if the derived work is
 * incompatible with the protocol description in the RFC file, it must be
 * called by a name other than "ssh" or "Secure Shell".
 */

#ifndef XMALLOC_H
#define XMALLOC_H

[[nodiscard]] void	*xmalloc(size_t);
[[nodiscard]] void	*xcalloc(size_t, size_t);
[[nodiscard]] void	*xrealloc(void *, size_t);
[[nodiscard]] void	*xreallocarray(void *, size_t, size_t);
[[nodiscard]] void	*xrecallocarray(void *, size_t, size_t, size_t);
[[nodiscard]] char	*xstrdup(const char *);
[[nodiscard]] char	*xstrndup(const char *, size_t);
[[nodiscard]] char	*xmemdup(const void *, size_t);
[[gnu::format(printf, 2, 3)]] [[gnu::nonnull(2)]]
int	 xasprintf(char **, const char *, ...);
[[gnu::format(printf, 2, 0)]] [[gnu::nonnull(2)]]
int	 xvasprintf(char **, const char *, va_list);
[[gnu::format(printf, 3, 4)]] [[gnu::nonnull(3)]]
int	 xsnprintf(char *, size_t, const char *, ...);
[[gnu::format(printf, 3, 0)]] [[gnu::nonnull(3)]]
int	 xvsnprintf(char *, size_t, const char *, va_list);

#endif	/* XMALLOC_H */
