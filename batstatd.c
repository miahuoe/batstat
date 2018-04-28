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
#include <time.h>
#include <sqlite3.h>
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
	struct _bat* next;
	char* sys_path;
	char* name;
	char* db_path;
	sqlite3* db;
	int db_r;
	char* db_err;
} bat;

int daemonize(void);
int cat(const char* const, char* const, const size_t);
int detect_bats(bat**, const char* const);
int bat_init(bat* const);
int bat_log(bat* const);
int bat_open(bat* const);
void bat_close(bat* const);

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

static const char* const sql_init =
"CREATE TABLE log ("
"id INTEGER PRIMARY KEY AUTOINCREMENT,"
"time UNSIGNED BIG INT,"
"present TINYINT NOT NULL,"
"cycle_count INT NOT NULL,"
"capacity INT NOT NULL,"
"capacity_level TEXT NOT NULL,"
"status TEXT NOT NULL,"
"charge_full INT NOT NULL,"
"charge_now INT NOT NULL,"
"current_now INT NUL NULL,"
"voltage_now INT NOT NULL"
");";

int bat_init(bat* const B)
{
	if ((B->db_r = sqlite3_open(B->db_path, &B->db))) {
		// TODO ERR
		return -1;
	}
	if ((B->db_r = sqlite3_exec(B->db, sql_init, NULL, 0, &B->db_err))) {

	}
	sqlite3_close(B->db);
	return 0;
}

int bat_open(bat* const B)
{
	if ((B->db_r = sqlite3_open(B->db_path, &B->db))) {
		// INIT
	}
	return 0;
}

void bat_close(bat* const B)
{
	sqlite3_close(B->db);
	free(B->name);
	free(B->sys_path);
	free(B->db_path);
}

int detect_bats(bat** H, const char* const logs_path)
{
	// TODO string hell
	static const char* const syspath = "/sys/class/power_supply";
	const size_t syspath_len = strlen(syspath);
	const size_t logs_path_len = strlen(logs_path);
	int e;
	DIR* sys = opendir(syspath);
	struct dirent* ent;
	if (!sys) return errno;
	errno = 0;
	while ((ent = readdir(sys))) {
		if (DOTDOT(ent->d_name)
		|| memcmp(ent->d_name, "BAT", 3)) continue;
		bat* b = malloc(sizeof(bat));
		const size_t ent_len = strlen(ent->d_name);

		b->db_path = malloc(logs_path_len+1+ent_len+3+1);
		snprintf(b->db_path, logs_path_len+1+ent_len+3+1,
			"%s/%s.db", logs_path, ent->d_name);

		b->sys_path = malloc(syspath_len+1+ent_len+1);
		snprintf(b->sys_path, syspath_len+1+ent_len+1,
			"%s/%s", syspath, ent->d_name);

		b->name = malloc(ent_len+1);
		memcpy(b->name, ent->d_name, ent_len+1);

		b->next = *H;
		*H = b;

		if (!access(b->db_path, F_OK)) {
			bat_open(b);
		}
		else {
			bat_init(b);
		}
	}
	e = errno;
	closedir(sys);
	return e;
}

int bat_log(bat* const B)
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
	time_t t;

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

	const size_t syspath_len = strnlen(B->sys_path, PATH_MAX_LEN);
	int f = 0;
	char catbuf[256];
	char* pathbuf = malloc(syspath_len+1+NAME_BUF_SIZE);
	memcpy(pathbuf, B->sys_path, syspath_len+1);
	pathbuf[syspath_len] = '/';
	char qbuf[1024];
	while (fields[f].name) {
		memcpy(pathbuf+syspath_len+1, fields[f].name, strlen(fields[f].name)+1);
		cat(pathbuf, catbuf, sizeof(catbuf));
		sscanf(catbuf, fields[f].fmt, fields[f].dst);
		f += 1;
	}
	t = time(0);
	snprintf(qbuf, sizeof(qbuf),
		"INSERT INTO log "
		"(time,present,cycle_count,"
		"capacity,capacity_level,"
		"status,charge_full,charge_now,"
		"current_now, voltage_now) "
		"VALUES "
		"(%lu, %c, %u, %u, '%s', '%s', %lu, %lu, %lu, %lu);",
		t, present, cycle_count,
		capacity, capacity_level,
		status, charge_full, charge_now,
		current_now, voltage_now
	);
	if ((B->db_r = sqlite3_exec(B->db, qbuf, NULL, 0, &B->db_err))) {
		sqlite3_errmsg(B->db);
		return B->db_r;
	}
	return 0;
}

int main(int argc, char* argv[])
{
	int e;
	static const char* const help = "...\n";
	int o, opti = 0;
	static const char sopt[] = "hd";
	struct option lopt[] = {
		{"help", no_argument, 0, 'h'},
		{"daemon", no_argument, 0, 'd'},
		{"log-dir", required_argument, 0, 0},
		{0, 0, 0, 0}
	};
	const char* logs_path = "/tmp"; // TODO
	while ((o = getopt_long(argc, argv, sopt, lopt, &opti)) != -1) {
		switch (o) {
		case 'd':
			if ((e = daemonize())) {
				fprintf(stderr, "%s\n", strerror(e));
				exit(EXIT_FAILURE);
			}
			break;
		case 'l':
			logs_path = optarg;
			break;
		case 'h':
			printf("%s", help);
			exit(EXIT_SUCCESS);
		default:
			exit(EXIT_FAILURE);
		}
	}

	bat* bats = NULL;
	detect_bats(&bats, logs_path);

	for (;;) {
		bat* B = bats;
		while (B) {
			bat_log(B);
			B = B->next;
		}
		sleep(1);
	}

	exit(EXIT_SUCCESS);
}
