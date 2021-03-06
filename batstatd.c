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
	sqlite3_stmt* stmt;
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

static bat* bats = 0;
static int errout = STDERR_FILENO;

int daemonize(const char* pidfile)
{
	char buf[16];
	int nullfd, pf;
	ssize_t w;
	struct sigaction sa;

	pf = open(pidfile, O_EXCL | O_WRONLY | O_CREAT, 0440);
	if (pf == -1) {
		if (errno == EEXIST) {
			dprintf(errout, "ERROR: '%s' exists.\n", pidfile);
		}
		exit(EXIT_FAILURE);
	}
	pid_t pid = fork();
	if (pid < 0) {
		return errno;
	}
	if (pid > 0) {
		w = snprintf(buf, sizeof(buf), "%d", pid);
		write(pf, buf, w);
		close(pf);
		exit(EXIT_SUCCESS);
	}
	close(pf);

	setsid();

	if ((nullfd = open("/dev/null", O_RDWR)) == -1) {
		return errno;
	}
	dup2(STDERR_FILENO, nullfd);
	dup2(STDOUT_FILENO, nullfd);
	dup2(STDIN_FILENO, nullfd);
	close(nullfd);

	umask(0027);
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
	else if (r >= 0) {
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

static const char* const sql_log =
	"INSERT INTO log "
	"(time,present,cycle_count,"
	"capacity,charge_full,charge_now,"
	"current_now,voltage_now) "
	"VALUES (?8, ?1, ?2, ?3, ?4, ?5, ?6, ?7);";

int bat_init(bat* const B)
{
	if ((B->db_r = sqlite3_open(B->db_path, &B->db))
	|| (B->db_r = sqlite3_exec(B->db, sql_init, NULL, 0, &B->db_err))) {
		return -1;
	}
	sqlite3_close(B->db);
	return 0;
}

int bat_open(bat* const B)
{
	int fi = 0;
	const char* tail;
	size_t pl;

	while (fields[fi]) {
		pl = strlen(B->sys_path)+1+strlen(fields[fi]);
		B->fields[fi] = malloc(pl+1);
		snprintf(B->fields[fi], pl+1, "%s/%s", B->sys_path, fields[fi]);
		fi += 1;
	}
	if ((B->db_r = sqlite3_open(B->db_path, &B->db))
	|| (B->db_r = sqlite3_prepare_v2(B->db,
		sql_log, strlen(sql_log), &B->stmt, &tail))) {
		return -1;
	}
	return 0;
}

void bat_close(bat* const B)
{
	int fi = 0;
	sqlite3_close(B->db);
	free(B->name);
	free(B->sys_path);
	free(B->db_path);
	sqlite3_finalize(B->stmt);
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

		if ((access(B->db_path, F_OK) && bat_init(B)) || bat_open(B)) {
			return -1;
		}
	}
	closedir(sys);
	return 0;
}

int bat_log(bat* const B)
{
	static char catbuf[256];
	static long long f[FIELDS_LENGTH];
	time_t t;
	int fi = 0;
	int i;

	while (B->fields[fi]) {
		cat(B->fields[fi], catbuf, sizeof(catbuf));
		sscanf(catbuf, "%lld", &f[fi]);
		fi += 1;
	}
	t = time(0);
	if ((B->db_r = sqlite3_reset(B->stmt))
	|| (B->db_r = sqlite3_clear_bindings(B->stmt))) {
		return -1;
	}
	for (i = 1; i <= fi && !B->db_r; ++i) {
		B->db_r = sqlite3_bind_int64(B->stmt, i, f[i-1]);
	}
	if (B->db_r
	|| (B->db_r = sqlite3_bind_int64(B->stmt, i, t))
	|| (B->db_r = sqlite3_step(B->stmt)) != SQLITE_DONE) {
		return -1;
	}
	return 0;
}

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
		close(errout);
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
	"  -l, --logdir=DIRECTORY\n"
	"  \tSet log directory.\n"
	"  -e, --errfile=FILE\n"
	"  \tSet error log file.\n"
	"  -p, --pidfile=FILE\n"
	"  \tCreate pidfile. No effect if used without --daemon.\n"
	"  \tExits if pidfile exists.\n"
	"  -i, --interval=NUM\n"
	"  \tSet interval in seconds. Must be > 0.\n";

int main(int argc, char* argv[])
{
	int e = 0, o, opti = 0, interval = 5;
	static const char* sopt = "hdl:e:p:i:";
	struct option lopt[] = {
		{"help", no_argument, 0, 'h'},
		{"daemon", no_argument, 0, 'd'},
		{"logdir", required_argument, 0, 'l'},
		{"errfile", required_argument, 0, 'e'},
		{"pidfile", required_argument, 0, 'p'},
		{"interval", required_argument, 0, 'i'},
		{0, 0, 0, 0}
	};
	const char* logs_path = 0;
	const char* pidfile = 0;
	char* es = 0;
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
			errout = open(optarg, O_WRONLY, 0644);
			break;
		case 'p':
			pidfile = optarg;
			break;
		case 'i':
			interval = atoi(optarg);
			break;
		default:
			exit(EXIT_FAILURE);
		}
	}
	if ((es = "No log path suppiled.", !logs_path)
	|| (es = "Interval must be > 0.", interval <= 0)
	|| (e = detect_bats(&bats, logs_path))
	|| (es = "No batteries detected.\n", !bats)
	|| (daemon && (e = daemonize(pidfile)))) {
		dprintf(errout, "ERROR: %s\n", e ? strerror(e) : es);
		exit(EXIT_FAILURE);
	}

	for (;;) {
		B = bats;
		while (B) {
			if (bat_log(B)) {
				dprintf(errout, "%ld E%d: %s\n",
					time(0), B->db_r,
					sqlite3_errmsg(B->db));
			}
			B = B->next;
		}
		sleep(interval);
	}
	exit(EXIT_SUCCESS);
}
