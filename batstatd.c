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
#include <signal.h>

#define PATH_BUF_SIZE (PATH_MAX)
#define PATH_MAX_LEN (PATH_MAX-1)

#define NAME_BUF_SIZE (NAME_MAX+1)
#define NAME_MAX_LEN (NAME_MAX)

#define DOTDOT(S) (S[0] == '.' && (!S[1] || (S[1] == '.' && !S[2])))

#define FIELDS_LENGTH 16

typedef struct bat {
	struct bat* next;
	char* fields[FIELDS_LENGTH];
	char* sys_path;
	char* name;
	char* db_path;
	sqlite3* db;
	int db_r;
	char* db_err;
} bat;

int daemonize(const char* pidfile);
int cat(const char* const, char* const, const size_t);
int detect_bats(bat**, const char* const);
int bat_init(bat* const);
int bat_log(bat* const);
int bat_open(bat* const);
void bat_close(bat* const);
void sighandler(int);

int daemonize(const char* pidfile)
{
	int nullfd;
	pid_t pid = fork();
	if (pid < 0) {
		return errno;
	}
	if (pid > 0) {
		int pf = creat(pidfile, 0600);
		if (pf == -1) {
			exit(EXIT_FAILURE);
		}
		char buf[16];
		int w = snprintf(buf, sizeof(buf), "%d", pid);
		write(pf, buf, w);
		close(pf);
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
	struct sigaction sa;
	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_handler = sighandler;
	if (chdir("/") || sigaction(SIGTERM, &sa, NULL)) {
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

static const char* const syspath = "/sys/class/power_supply";

static const char* const fields[] = {
	"present",
	"cycle_count",
	"capacity",
	"charge_full",
	"charge_now",
	"current_now",
	"voltage_now",
	0
};

static const char* const sql_init =
	"CREATE TABLE log ("
	"id INTEGER PRIMARY KEY AUTOINCREMENT,"
	"time INTEGER INT,"
	"present INTEGER NOT NULL,"
	"cycle_count INTEGER NOT NULL,"
	"capacity INTEGER NOT NULL,"
	"charge_full INTEGER NOT NULL,"
	"charge_now INTEGER NOT NULL,"
	"current_now INTEGER NUL NULL,"
	"voltage_now INTEGER NOT NULL"
	");";

int bat_init(bat* const B)
{
	if ((B->db_r = sqlite3_open(B->db_path, &B->db))) {
		// TODO ERR
		return -1;
	}
	if ((B->db_r = sqlite3_exec(B->db, sql_init, NULL, 0, &B->db_err))) {
		return B->db_r;
	}
	sqlite3_close(B->db);
	return 0;
}

int bat_open(bat* const B)
{
	int fi = 0;
	while (fields[fi]) {
		size_t pl = strlen(B->sys_path)+1+strlen(fields[fi]);
		B->fields[fi] = malloc(pl+1);
		snprintf(B->fields[fi], pl+1, "%s/%s", B->sys_path, fields[fi]);
		fi += 1;
	}
	if ((B->db_r = sqlite3_open(B->db_path, &B->db))) {
		return B->db_r;
	}
	return 0;
}

void bat_close(bat* const B)
{
	sqlite3_close(B->db);
	free(B->name);
	free(B->sys_path);
	free(B->db_path);
	int fi = 0;
	while (B->fields[fi]) {
		free(B->fields[fi]);
		fi += 1;
	}
}

int detect_bats(bat** H, const char* const logs_path)
{
	const size_t syspath_len = strlen(syspath);
	const size_t logs_path_len = strlen(logs_path);
	DIR* sys = opendir(syspath);
	struct dirent* ent;
	if (!sys) return errno;
	errno = 0;
	while ((ent = readdir(sys))) {
		if (DOTDOT(ent->d_name)
		|| memcmp(ent->d_name, "BAT", 3)) continue;
		bat* B = calloc(1, sizeof(bat));
		const size_t ent_len = strlen(ent->d_name);

		B->db_path = malloc(logs_path_len+1+ent_len+3+1);
		snprintf(B->db_path, logs_path_len+1+ent_len+3+1,
			"%s/%s.db", logs_path, ent->d_name);

		B->sys_path = malloc(syspath_len+1+ent_len+1);
		snprintf(B->sys_path, syspath_len+1+ent_len+1,
			"%s/%s", syspath, ent->d_name);

		B->name = malloc(ent_len+1);
		memcpy(B->name, ent->d_name, ent_len+1);

		B->next = *H;
		*H = B;

		if ((access(B->db_path, F_OK) && bat_init(B))
		|| bat_open(B)) {
			return 1;
		}
	}
	closedir(sys);
	return 0;
}

int bat_log(bat* const B)
{
	static char catbuf[256];
	static char qbuf[1024];
	static long long f[FIELDS_LENGTH];
	time_t t;
	int fi = 0;

	while (B->fields[fi]) {
		cat(B->fields[fi], catbuf, sizeof(catbuf));
		sscanf(catbuf, "%lld", &f[fi]);
		fi += 1;
	}
	t = time(0);
	snprintf(qbuf, sizeof(qbuf),
		"INSERT INTO log "
		"(time,present,cycle_count,"
		"capacity,charge_full,charge_now,"
		"current_now,voltage_now) "
		"VALUES "
		"(%ld, %lld, %lld, %lld, %lld, %lld, %lld, %lld);",
		t, f[0], f[1], f[2], f[3], f[4], f[5], f[6]
	);
	if ((B->db_r = sqlite3_exec(B->db, qbuf, NULL, 0, &B->db_err))) {
		return B->db_r;
	}
	return 0;
}

static bat* bats = 0;
static FILE* errout = 0;

void sighandler(int sig)
{
	bat* B, *old;
	switch (sig) {
	case SIGTERM:
		B = bats;
		while (B) {
			old = B;
			bat_close(B);
			B = B->next;
			free(old);
		}
		fclose(errout);
		exit(EXIT_SUCCESS);
		break;
	default:
		break;
	}
}

static const char* const help =
	"[OPTIONS]\n"
	"Options:\n"
	"  -d, --daemon\n"
	"  \tRun in backgroud.\n"
	"  -h, --help\n"
	"  \tDisplay this help message.\n"
	"  -l, --log-dir\n"
	"  \tSet log directory.\n"
	"  -e, --errout\n"
	"  \tSet error log file.\n"
	"  -p, --pidfile\n"
	"  \tCreate pidfile. No effect if used without --daemon.\n";

int main(int argc, char* argv[])
{
	int e, o, opti = 0;
	static const char sopt[] = "hdl:e:p:";
	struct option lopt[] = {
		{"help", no_argument, 0, 'h'},
		{"daemon", no_argument, 0, 'd'},
		{"log-dir", required_argument, 0, 'l'},
		{"errout", required_argument, 0, 'e'},
		{"pidfile", required_argument, 0, 'p'},
		{0, 0, 0, 0}
	};
	const char* logs_path = 0;
	const char* pidfile = 0;
	_Bool daemon = 0;
	bat* B;
	while ((o = getopt_long(argc, argv, sopt, lopt, &opti)) != -1) {
		switch (o) {
		case 'h':
			printf("Usage: %s %s", argv[0], help);
			exit(EXIT_SUCCESS);
		case 'd':
			daemon = 1;
			break;
		case 'l':
			logs_path = optarg;
			break;
		case 'e':
			errout = fopen(optarg, "a");
			break;
		case 'p':
			pidfile = optarg;
			break;
		default:
			exit(EXIT_FAILURE);
		}
	}
	if (!errout) {
		errout = stderr;
	}
	if (!logs_path) {
		fprintf(errout, "ERROR: No log path suppiled.\n");
		exit(EXIT_FAILURE);
	}
	if ((e = detect_bats(&bats, logs_path))) {
		fprintf(errout, "ERROR: %s\n", strerror(e));
		exit(EXIT_FAILURE);
	}
	if (!bats) {
		fprintf(errout, "No batteries detected in '%s'\n", syspath);
		exit(EXIT_FAILURE);
	}
	if (daemon && (e = daemonize(pidfile))) {
		fprintf(errout, "ERROR: %s\n", strerror(e));
		exit(EXIT_FAILURE);
	}

	for (;;) {
		B = bats;
		while (B) {
			if (bat_log(B)) {
				fprintf(errout, "%s\n", sqlite3_errmsg(B->db));
			}
			B = B->next;
		}
		sleep(5);
	}
	exit(EXIT_SUCCESS);
}
