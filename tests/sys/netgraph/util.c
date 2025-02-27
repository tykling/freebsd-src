/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2021 Lutz Donnerhacke
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <atf-c.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <sys/select.h>
#include <sys/queue.h>

#include "util.h"


static int cs = -1, ds = -1;
static ng_error_t error_handling = FAIL;

#define CHECK(r, x)	do {		\
	if (error_handling == FAIL)	\
	    ATF_REQUIRE(x);		\
	else if(!(x))			\
	    return r;			\
} while(0)

struct data_handler {
	char const *hook;
	ng_data_handler_t handler;
	SLIST_ENTRY(data_handler) next;
};
static SLIST_HEAD(, data_handler) data_head = SLIST_HEAD_INITIALIZER(data_head);

static void handle_data(void *ctx);
static void handle_msg(void);

void
ng_connect(char const *path1, char const *hook1,
	   char const *path2, char const *hook2)
{
	struct ngm_connect c;

	strncpy(c.ourhook,  hook1, sizeof(c.ourhook));
	strncpy(c.peerhook, hook2, sizeof(c.peerhook));
	strncpy(c.path,     path2, sizeof(c.path));

	CHECK(, -1 != NgSendMsg(cs, path1,
	    NGM_GENERIC_COOKIE, NGM_CONNECT,
	    &c, sizeof(c)));
}

void
ng_mkpeer(char const *path1, char const *hook1,
	  char const *type,  char const *hook2)
{
	struct ngm_mkpeer p;

	strncpy(p.ourhook,  hook1, sizeof(p.ourhook));
	strncpy(p.peerhook, hook2, sizeof(p.peerhook));
	strncpy(p.type,     type,  sizeof(p.type));

	CHECK(, -1 != NgSendMsg(cs, path1,
	    NGM_GENERIC_COOKIE, NGM_MKPEER,
	    &p, sizeof(p)));
}

void
ng_rmhook(char const *path, char const *hook)
{
	struct ngm_rmhook h;

	strncpy(h.ourhook, hook, sizeof(h.ourhook));

	CHECK(, -1 != NgSendMsg(cs, path,
	    NGM_GENERIC_COOKIE, NGM_RMHOOK,
	    &h, sizeof(h)));
}

void
ng_name(char const *path, char const *name)
{
	struct ngm_name n;

	strncpy(n.name, name, sizeof(n.name));

	CHECK(, -1 != NgSendMsg(cs, path,
	    NGM_GENERIC_COOKIE, NGM_NAME,
	    &n, sizeof(n)));
}

void
ng_shutdown(char const *path)
{
	CHECK(, -1 != NgSendMsg(cs, path,
	    NGM_GENERIC_COOKIE, NGM_SHUTDOWN,
	    NULL, 0));
}

void
ng_register_data(char const *hook, ng_data_handler_t proc)
{
	struct data_handler *p;

	ATF_REQUIRE(NULL != (p = calloc(1, sizeof(struct data_handler))));
	ATF_REQUIRE(NULL != (p->hook = strdup(hook)));
	ATF_REQUIRE(NULL != (p->handler = proc));
	SLIST_INSERT_HEAD(&data_head, p, next);
}

void
ng_send_data(char const *hook,
	     void const *data, size_t len)
{
	CHECK(, -1 != NgSendData(ds, hook, data, len));
}

static void
handle_msg(void) {
	struct ng_mesg *m;
	char path[NG_PATHSIZ];

	ATF_REQUIRE(-1 != NgAllocRecvMsg(cs, &m, path));

	printf("Got message from %s\n", path);
	free(m);
}

static void
handle_data(void *ctx) {
	char hook[NG_HOOKSIZ];
	struct data_handler *hnd;
	u_char *data;
	int len;

	ATF_REQUIRE(0 < (len = NgAllocRecvData(ds, &data, hook)));
	SLIST_FOREACH(hnd, &data_head, next)
		if (0 == strcmp(hnd->hook, hook))
			break;

	if (hnd != NULL)
		(*(hnd->handler))(data, len, ctx);

	free(data);
}

int
ng_handle_event(unsigned int ms, void *context)
{
	fd_set fds;
	int maxfd = (ds < cs) ? cs : ds;
	struct timeval timeout = { 0, ms * 1000lu };

	FD_ZERO(&fds);
	FD_SET(cs, &fds);
	FD_SET(ds, &fds);
retry:
	switch (select(maxfd+1, &fds, NULL, NULL, &timeout)) {
	case -1:
		ATF_REQUIRE_ERRNO(EINTR, 1);
		goto retry;
	case 0:			       /* timeout */
		return 0;
	default:		       /* something to do */
		if (FD_ISSET(cs, &fds))
		    handle_msg();
		if (FD_ISSET(ds, &fds))
		    handle_data(context);
		return 1;
	}
}

void
ng_handle_events(unsigned int ms, void *context)
{
	while(ng_handle_event(ms, context))
		;
}

int
ng_send_msg(char const *path, char const *msg)
{
	int res;

	CHECK(-1, -1 != (res = NgSendAsciiMsg(cs, path, "%s", msg)));
	return (res);
}

ng_error_t
ng_errors(ng_error_t n)
{
	ng_error_t o = error_handling;

	error_handling = n;
	return (o);
}

void
ng_init(void) {
	if (cs >= 0)		       /* prevent reinit */
		return;

	ATF_REQUIRE(0 == NgMkSockNode(NULL, &cs, &ds));
	NgSetDebug(3);
}
