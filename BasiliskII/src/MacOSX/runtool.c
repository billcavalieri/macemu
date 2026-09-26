/*
 *  runtool.c - Run the BPF ethernet helper as root
 *  Copyright (C) 2010, Daniel Sumorok
 *  Copyright (C) 2026 Bill Cavalieri
 *
 *  Basilisk II (C) 1997-2008 Christian Bauer
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <unistd.h>
#include <spawn.h>

#include <CoreFoundation/CoreFoundation.h>
#include <Foundation/Foundation.h>
#include <ServiceManagement/ServiceManagement.h>

extern char **environ;

#define ETHER_SOCK "/var/run/com.sheepshaver.etherhelper.sock"

FILE * run_tool(const char *if_name, const char *tool_name);

static int connect_helper(void)
{
	int fd;
	struct sockaddr_un addr;

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, ETHER_SOCK, sizeof(addr.sun_path) - 1);
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static int bless_helper(void)
{
	NSError *error = nil;
	SMAppService *service;
	int i;

	service = [SMAppService daemonServiceWithPlistName:@"com.sheepshaver.etherhelper.plist"];
	if (service.status != SMAppServiceStatusEnabled) {
		if (![service registerAndReturnError:&error])
			return -1;
	}
	for (i = 0; i < 50; i++) {
		int probe = connect_helper();
		if (probe >= 0) {
			close(probe);
			return 0;
		}
		usleep(100000);
	}
	return -1;
}

/* Already-root parent: same packet pipe, no launchd. */
static FILE *spawn_direct(const char *path, const char *if_name)
{
	int sv[2];
	posix_spawn_file_actions_t actions;
	pid_t pid;
	char *argv[3];
	FILE *fp;
	char c;

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0)
		return NULL;
	if (posix_spawn_file_actions_init(&actions) != 0) {
		close(sv[0]);
		close(sv[1]);
		return NULL;
	}
	posix_spawn_file_actions_adddup2(&actions, sv[1], STDIN_FILENO);
	posix_spawn_file_actions_adddup2(&actions, sv[1], STDOUT_FILENO);
	posix_spawn_file_actions_addclose(&actions, sv[0]);
	argv[0] = (char *)path;
	argv[1] = (char *)if_name;
	argv[2] = NULL;
	if (posix_spawn(&pid, path, &actions, NULL, argv, environ) != 0) {
		posix_spawn_file_actions_destroy(&actions);
		close(sv[0]);
		close(sv[1]);
		return NULL;
	}
	posix_spawn_file_actions_destroy(&actions);
	close(sv[1]);
	fp = fdopen(sv[0], "r+");
	if (fp == NULL) {
		close(sv[0]);
		return NULL;
	}
	if (fread(&c, 1, 1, fp) != 1) {
		fclose(fp);
		return NULL;
	}
	return fp;
}

static int helper_path(const char *tool_name, char *path_buffer, size_t path_len)
{
	CFBundleRef bundle_ref;
	CFStringRef tool_name_str;
	CFURLRef url_ref;
	CFStringRef path_str;

	bundle_ref = CFBundleGetMainBundle();
	if (bundle_ref == NULL)
		return -1;
	tool_name_str = CFStringCreateWithCString(NULL, tool_name, kCFStringEncodingUTF8);
	url_ref = CFBundleCopyResourceURL(bundle_ref, tool_name_str, NULL, NULL);
	CFRelease(tool_name_str);
	if (url_ref == NULL)
		return -1;
	path_str = CFURLCopyFileSystemPath(url_ref, kCFURLPOSIXPathStyle);
	CFRelease(url_ref);
	if (path_str == NULL)
		return -1;
	if (!CFStringGetCString(path_str, path_buffer, path_len, kCFStringEncodingUTF8)) {
		CFRelease(path_str);
		return -1;
	}
	CFRelease(path_str);
	return 0;
}

FILE * run_tool(const char *if_name, const char *tool_name)
{
	char path_buffer[1024];
	char line[256];
	int fd;
	int n;
	FILE *fp;
	char c;

	if (helper_path(tool_name, path_buffer, sizeof(path_buffer)) != 0)
		return NULL;

	fd = connect_helper();
	if (fd < 0) {
		if (bless_helper() == 0)
			fd = connect_helper();
	}
	if (fd < 0) {
		if (geteuid() == 0)
			return spawn_direct(path_buffer, if_name);
		fprintf(stderr, "%s: SMJobBless failed; BPF ethernet needs a signed app or root.\n",
			__func__);
		return NULL;
	}

	n = snprintf(line, sizeof(line), "%s\n", if_name);
	if (n < 0 || n >= (int)sizeof(line) || write(fd, line, (size_t)n) != n) {
		close(fd);
		return NULL;
	}
	fp = fdopen(fd, "r+");
	if (fp == NULL) {
		close(fd);
		return NULL;
	}
	if (fread(&c, 1, 1, fp) != 1) {
		fclose(fp);
		return NULL;
	}
	return fp;
}
