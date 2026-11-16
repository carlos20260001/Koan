/*	$NetBSD: res_state.c,v 1.6 2008/04/28 20:23:02 martin Exp $	*/

/*-
 * Copyright (c) 2004 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
 
/* Note to Haiku Developers:
   -------------------------
   This file contains the thread-safe versions of res functions, taken from
   NetBSD's libpthread directory. Do *not* replace it with the legacy
   single-threaded version from NetBSD's netresolv directory.
*/

#include <sys/cdefs.h>
#if defined(LIBC_SCCS) && !defined(lint)
__RCSID("$NetBSD: res_state.c,v 1.6 2008/04/28 20:23:02 martin Exp $");
#endif

#include <sys/types.h>
#include <sys/queue.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <stdlib.h>
#include <unistd.h>
#include <resolv.h>
#include <netdb.h>

#include <pthread.h>

#ifdef __HAIKU__
#include <string.h>
#include <sys/stat.h>

#include <FindDirectory.h>
#endif

struct __res_state _nres
# if defined(__BIND_RES_TEXT)
	= { .retrans = RES_TIMEOUT, }	/*%< Motorola, et al. */
# endif
	;

static SLIST_HEAD(, _res_st) res_list = LIST_HEAD_INITIALIZER(&res_list);

struct _res_st {
	/* __res_put_state() assumes st_res is the first member. */
	struct __res_state	st_res;

	SLIST_ENTRY(_res_st)	st_list;
#ifdef __HAIKU__
	/* resolv.conf as this state last read it */
	struct timespec		st_conf_time;
	ino_t			st_conf_inode;
#endif
};

static pthread_mutex_t res_mtx = PTHREAD_MUTEX_INITIALIZER;

res_state __res_state(void);
res_state __res_get_state(void);
void __res_put_state(res_state);

#ifdef RES_STATE_DEBUG
static void
res_state_debug(const char *msg, void *p)
{
	char buf[512];
	pthread_t self = pthread__self();
	int len = snprintf(buf, sizeof(buf), "%p: %s %p\n", self, msg, p);

	(void)write(STDOUT_FILENO, buf, (size_t)len);
}
#else
#define res_state_debug(a, b)
#endif


#ifdef __HAIKU__
/*
 * The states are kept for the life of the process, and res_check() (the
 * kqueue on resolv.conf) is not built here: without this, a program kept the
 * name servers it saw first. A VPN that connects (or goes away), DHCP from
 * another network: every later lookup still asked the old servers, and a
 * server that is gone costs the full timeout (5 s) on each one.
 */
static pthread_once_t res_conf_path_once = PTHREAD_ONCE_INIT;
static char res_conf_path[PATH_MAX];

static void
res_conf_path_init(void)
{
	if (find_directory(B_SYSTEM_SETTINGS_DIRECTORY, -1, false, res_conf_path,
			sizeof(res_conf_path)) == B_OK)
		strlcat(res_conf_path, "/network/resolv.conf", sizeof(res_conf_path));
	else
		res_conf_path[0] = '\0';
}

/* Whether resolv.conf changed since the state last read it; notes the new one. */
static int
res_conf_changed(struct _res_st *st)
{
	struct stat info;

	pthread_once(&res_conf_path_once, res_conf_path_init);
	if (res_conf_path[0] == '\0' || stat(res_conf_path, &info) != 0)
		memset(&info, 0, sizeof(info));
	if (info.st_mtim.tv_sec == st->st_conf_time.tv_sec
		&& info.st_mtim.tv_nsec == st->st_conf_time.tv_nsec
		&& info.st_ino == st->st_conf_inode)
		return 0;
	st->st_conf_time = info.st_mtim;
	st->st_conf_inode = info.st_ino;
	return 1;
}
#endif

res_state
__res_get_state(void)
{
	res_state res;
	struct _res_st *st;
	pthread_mutex_lock(&res_mtx);
	st = SLIST_FIRST(&res_list);
	if (st != NULL) {
		SLIST_REMOVE_HEAD(&res_list, st_list);
		pthread_mutex_unlock(&res_mtx);
		res = &st->st_res;
		res_state_debug("checkout from list", st);
	} else {
		pthread_mutex_unlock(&res_mtx);
		st = calloc(1, sizeof(*st));
		if (st == NULL) {
			h_errno = NETDB_INTERNAL;
			return NULL;
		}
		res = &st->st_res;
		res->options = 0;
		res_state_debug("alloc new", res);
	}
#ifdef __HAIKU__
	/* As res_check() does: res_ninit() of a set up state destroys it first. */
	if (res_conf_changed(st) || (res->options & RES_INIT) == 0) {
#else
	if ((res->options & RES_INIT) == 0) {
#endif
		if (res_ninit(res) == -1) {
			h_errno = NETDB_INTERNAL;
			free(st);
			return NULL;
		}
	}
	return res;
}

void
/*ARGSUSED*/
__res_put_state(res_state res)
{
	struct _res_st *st = (struct _res_st *)(void *)res;

	res_state_debug("free", res);
	pthread_mutex_lock(&res_mtx);
	SLIST_INSERT_HEAD(&res_list, st, st_list);
	pthread_mutex_unlock(&res_mtx);
}


/*
 * This is aliased via a macro to _res.
 * This function is not thread safe.
 */
res_state
__res_state(void)
{
	if ((_nres.options & RES_INIT) == 0 && res_ninit(&_nres) == -1) {
		h_errno = NETDB_INTERNAL;
		return NULL;
	}

	return &_nres;
}
