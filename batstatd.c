/*
 * Copyright (c) 2018 Michał Czarnecki
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _DEFAULT_SOURCE
	#define _DEFAULT_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
//#include <time.h>
//#include <sqlite3.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <getopt.h>

#define PATH_BUF_SIZE (PATH_MAX)
#define PATH_MAX_LEN (PATH_MAX-1)

#define NAME_BUF_SIZE (NAME_MAX+1)
#define NAME_MAX_LEN (NAME_MAX)

#define DOTDOT(S) (S[0] == '.' && (!S[1] || (S[1] == '.' && !S[2])))

typedef struct {
	char* name;
	char* fmt;
	void* dst;
} field;

typedef struct _bat {
	char* sys_path;
	char* name;
	//char* db_path;
	//sqlite3* db;
	//char* db_path;
	//int db_r;
	//char* db_err;
	struct _bat* next;
} bat;

int daemonize(void);
int cat(const char* const, char* const, const size_t);
int detect_bats(bat**);
int bat_log(const char* const);

int daemonize(void)
{
	// TODO pidfile
	// TODO signals???
	int nullfd;
	pid_t pid = fork();
	if (pid < 0) {
		return errno;
	}
	if (pid > 0) {
		exit(EXIT_SUCCESS);
	}

	setsid();

	if ((nullfd = open("/dev/null", O_RDWR)) == -1) {
		return errno;
	}
	dup2(STDERR_FILENO, nullfd);
	dup2(STDOUT_FILENO, nullfd);
	dup2(STDIN_FILENO, nullfd);
	close(nullfd);

	umask(0027);
	if (chdir("/")) {
		return errno;
	}
	return 0;
}

int cat(const char* const path, char* const buf, const size_t bufs)
{
	int e = 0, fd;
	ssize_t r;
	if ((fd = open(path, O_RDONLY)) == -1) {
		return errno;
	}
	if ((r = read(fd, buf, bufs)) == -1) {
		e = errno;
	}
	else {
		buf[r] = 0;
	}
	close(fd);
	return e;
}

int detect_bats(bat** H)
{
	static const char* const syspath = "/sys/class/power_supply";
	int e;
	char path[1024];
	const size_t syspath_len = strlen(syspath);
	memcpy(path, syspath, syspath_len+1);
	DIR* sys = opendir(syspath);
	struct dirent* ent;
	if (!sys) return errno;
	errno = 0;
	while ((ent = readdir(sys))) {
		if (DOTDOT(ent->d_name)
		|| memcmp(ent->d_name, "BAT", 3)) continue;
		path[syspath_len] = '/';
		const size_t ent_len = strlen(ent->d_name);
		memcpy(path+syspath_len+1, ent->d_name, ent_len+1);

		bat* b = malloc(sizeof(bat));
		b->sys_path = malloc(syspath_len+1+ent_len+1);
		memcpy(b->sys_path, path, syspath_len+1+ent_len+1);
		b->name = b->sys_path+syspath_len+1;
		b->next = *H;
		*H = b;

		path[syspath_len] = 0;
	}
	e = errno;
	closedir(sys);
	return e;
}

int bat_log(const char* const batpath)
{
	char present;
	unsigned int cycle_count;
	unsigned int capacity;
	char capacity_level[32];
	char status[32];
	unsigned long charge_full;
	unsigned long charge_now;
	unsigned long current_now;
	unsigned long voltage_now;

	/* TODO is this evil or not? */
	/* TODO check doc for exact types and possible outputs */
	const field fields[] = {
		{ "present", "%c", (void*) &present },
		{ "cycle_count", "%u", (void*) &cycle_count },
		{ "capacity", "%u", (void*) &capacity },
		{ "capacity_level", "%s", (void*) capacity_level },
		{ "status", "%s", (void*) status },
		{ "charge_full", "%lu",  (void*) &charge_full },
		{ "charge_now", "%lu",  (void*) &charge_now },
		{ "current_now", "%lu",  (void*) &current_now },
		{ "voltage_now", "%lu",  (void*) &voltage_now },
		{ NULL, NULL, NULL }
	};

	const size_t batpath_len = strnlen(batpath, PATH_MAX_LEN);
	int f = 0;
	char catbuf[256];
	char* pathbuf = malloc(batpath_len+1+NAME_BUF_SIZE);
	memcpy(pathbuf, batpath, batpath_len+1);
	pathbuf[batpath_len] = '/';
	while (fields[f].name) {
		// TODO to sqlite db
		memcpy(pathbuf+batpath_len+1, fields[f].name, strlen(fields[f].name)+1);
		cat(pathbuf, catbuf, sizeof(catbuf));
		printf("%16s: %s", fields[f].name, catbuf);
		sscanf(catbuf, fields[f].fmt, fields[f].dst);
		f += 1;
	}
	printf("\n");
	return 0;
}

int main(int argc, char* argv[])
{
	static const char* const help = "...\n";
	int o, opti = 0;
	static const char sopt[] = "h";
	struct option lopt[] = {
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};
	while ((o = getopt_long(argc, argv, sopt, lopt, &opti)) != -1) {
		switch (o) {
		case 'h':
			printf("%s", help);
			exit(EXIT_SUCCESS);
		default:
			exit(EXIT_FAILURE);
		}
	}

	//if ((sq_r = sqlite3_open(db_path, &sq_db))) {
	//	printf("%s\n", sqlite3_errmsg(sq_db));
	//	exit(EXIT_FAILURE);
	//}

	//r = sqlite3_exec(db, sql_init, callback, 0, &err_msg);
	//if (r != SQLITE_OK) {
	//	printf("%s\n", err_msg);
	//}

	//int e;
	//if ((e = daemonize())) {
	//	fprintf(stderr, "%s\n", strerror(e));
	//	exit(EXIT_FAILURE);
	//}

	bat* bats = NULL;
	detect_bats(&bats);

	for (;;) {
		bat* B = bats;
		while (B) {
			printf("%s (%s)\n", B->sys_path, B->name);
			bat_log(B->sys_path);
			B = B->next;
		}
		sleep(1);
	}

	//sqlite3_close(sq_db);
	exit(EXIT_SUCCESS);
}
