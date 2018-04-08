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

#define DOTDOT(S) (S[0] == '.' && (!S[1] || (S[1] == '.' && !S[2])))

int daemonize(void);
int cat(const char* const, char* const, const size_t);
int detect_bats(void);

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

int detect_bats(void)
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
		memcpy(path+syspath_len+1, ent->d_name, strlen(ent->d_name)+1);
		printf("%s\n", ent->d_name);
		path[syspath_len] = 0;
	}
	e = errno;
	closedir(sys);
	return e;
}

int main(int argc, char* argv[])
{
	int e;
	//char* db_path = "./test.db";
	//sqlite3* sq_db = NULL;
	//char* sq_err = NULL;
	//int sq_r;

	//if ((sq_r = sqlite3_open(db_path, &sq_db))) {
	//	printf("%s\n", sqlite3_errmsg(sq_db));
	//	exit(EXIT_FAILURE);
	//}

	//r = sqlite3_exec(db, sql_init, callback, 0, &err_msg);
	//if (r != SQLITE_OK) {
	//	printf("%s\n", err_msg);
	//}

	//detect_bats();
	if ((e = daemonize())) {
		fprintf(stderr, "%s\n", strerror(e));
		exit(EXIT_FAILURE);
	}

	sleep(10); // TEST
	//sqlite3_close(sq_db);
	exit(EXIT_SUCCESS);
}
