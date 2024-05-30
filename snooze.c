/*
 * snooze - run a command at a particular time
 *
 * To the extent possible under law, Leah Neukirchen <leah@vuxu.org>
 * has waived all copyright and related or neighboring rights to this work.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

#include <sys/stat.h>
#include <sys/types.h>

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/auxv.h>
#endif

static long slack = 60;
#define SLEEP_PHASE 300
static int nflag, vflag;

static int timewait = -1;
static int randdelay = 0;
static int jitter = 0;
static char *timefile;

static volatile sig_atomic_t alarm_rang = 0;

#ifdef __linux__
#include <stdint.h>
#include <inttypes.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/rtc.h>

#define DEFAULT_RTC_DEVICE	"/dev/rtc0"
#define RTC_MIN_DELTA		300
#define RTC_WKALM_SLACK		60

static const char *inhibitcmd = "/usr/bin/elogind-inhibit";
static const char *inhibitcmd_args[] = {
	"--why=snooze command running scheduled task",
	"--what=idle:sleep:handle-suspend-key:handle-hibernate-key",
	"--mode=block",
	"--no-pager",
	"--no-legend",
};
static const int inhibitcmd_n_args = sizeof(inhibitcmd_args) / sizeof(inhibitcmd_args[0]);

static int
rtc_wake_at(time_t *at)
{
	struct tm tm = { 0 };
	struct rtc_time rtc;
	struct rtc_wkalrm wake = { 0 };
	int fd = -1, err = 0, retries = 10;
	char s[64];
	time_t wake_time, sys_time, rtc_time;
	int64_t delta;

	// slightly reduce wake time, so machine has the time to exit sleep
	if (*at > RTC_WKALM_SLACK)
		*at -= RTC_WKALM_SLACK;

	sys_time = time(NULL);
	if (sys_time == (time_t)-1) {
		err = -1;
		fprintf(stderr, "failed to read system time\n");
		goto err_out;
	}

	if (vflag) {
		gmtime_r(at, &tm);
		fprintf(stderr, "[RTC]: wake requested for epoch %"PRId64", (UTC) %s", (int64_t) *at, asctime_r(&tm, s));
	}

	delta = (int64_t)*at - (int64_t)sys_time;
	if (delta < RTC_MIN_DELTA) {
		goto err_out;
	}

retry:
	errno = 0;
	fd = open(DEFAULT_RTC_DEVICE, O_RDWR);
	if (0 > fd) {
		if (errno == EBUSY || errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR) {
			if (retries-- > 0) {
				sleep(1);
				goto retry;
			}
		}
		fprintf(stderr, "failed to open RTC device\n");
		goto err_out;
	}

	errno = 0;
	if ((err = ioctl(fd, RTC_RD_TIME, &rtc)) < 0) {
		fprintf(stderr, "failed to read RTC time\n");
		goto err_out;
	}

	tm.tm_sec = rtc.tm_sec;
	tm.tm_min = rtc.tm_min;
	tm.tm_hour = rtc.tm_hour;
	tm.tm_mday = rtc.tm_mday;
	tm.tm_mon = rtc.tm_mon;
	tm.tm_year = rtc.tm_year;
	tm.tm_isdst = -1; /* assume system knows better than RTC */

	rtc_time = mktime(&tm);
	if (rtc_time == (time_t)-1) {
		err = -1;
		fprintf(stderr, "convert RTC time failed\n");
		goto err_out;
	}

	delta = (int64_t)sys_time - rtc_time;

	if (vflag) {
		gmtime_r(&sys_time, &tm);
		printf("sys_time = %"PRId64", (UTC) %s", (int64_t) sys_time, asctime_r(&tm, s));

		gmtime_r(&rtc_time, &tm);
		printf("rtc_time = %"PRId64", (UTC) %s", (int64_t) rtc_time, asctime_r(&tm, s));
	}

	if ((err = ioctl(fd, RTC_WKALM_RD, &wake)) < 0) {
		fprintf(stderr, "set RTC wake alarm failed");
		goto err_out;
	}

	tm.tm_sec = wake.time.tm_sec;
	tm.tm_min = wake.time.tm_min;
	tm.tm_hour = wake.time.tm_hour;
	tm.tm_mday = wake.time.tm_mday;
	tm.tm_mon = wake.time.tm_mon;
	tm.tm_year = wake.time.tm_year;
	tm.tm_isdst = -1;
	wake_time = mktime(&tm);
	if (wake_time == (time_t)-1) {
		err = -1;
		fprintf(stderr, "convert RTC wake alarm failed\n");
		goto err_out;
	}

	if (vflag) {
		gmtime_r(&wake_time, &tm);
		printf("rtc_wake_time = %"PRId64", (UTC) %s", (int64_t) wake_time, asctime_r(&tm, s));
	}

	// validate the RTC is "close" to system time. If not, we cannot use device!
	if ((delta > 0 ? delta : -delta) > RTC_MIN_DELTA) {
		fprintf(stderr, "clock skew of %"PRId64" seconds detected.\n", delta);
		err = -1;
		goto err_out;
	}

	// validate the requested time is actually in the future!
	if (*at <= sys_time) {
		if (vflag)
			fprintf(stderr, "requested wake time has past!\n");
		err = -1;
		goto err_out;
	}

	// ensure our requested time is actually before the current RTC wake alarm.
	if (*at > wake_time && wake_time >= sys_time) {
		err = 0; // not an error
		goto err_out;
	} else if (wake_time == *at) {
		err = 0; // not an error
		goto err_out;
	}

	if (vflag)
		printf("[RTC]: programming device...\n");

	// build value to store
	localtime_r(at, &tm);
	wake.time.tm_sec = tm.tm_sec;
	wake.time.tm_min = tm.tm_min;
	wake.time.tm_hour = tm.tm_hour;
	wake.time.tm_mday = tm.tm_mday;
	wake.time.tm_mon = tm.tm_mon;
	wake.time.tm_year = tm.tm_year;
	wake.time.tm_wday = -1;
	wake.time.tm_yday = -1;
	wake.time.tm_isdst = -1;
	wake.enabled = 1;

	if ((err = ioctl(fd, RTC_WKALM_SET, &wake)) < 0) {
		fprintf(stderr, "set RTC wake alarm failed");
		goto err_out;
	}

err_out:
	if (fd != -1)
		close(fd);
	return err;
}
#endif

static void
wakeup(int sig)
{
	(void)sig;
	alarm_rang = 1;
}

static long
parse_int(char **s, size_t minn, size_t maxn)
{
	long n;
	char *end;

	errno = 0;
	n = strtol(*s, &end, 10);
	if (errno) {
		perror("strtol");
		exit(1);
	}
	if (n < (long)minn || n >= (long)maxn) {
		fprintf(stderr, "number outside %zd <= n < %zd\n", minn, maxn);
		exit(1);
	}
	*s = end;
	return n;
}

static long
parse_dur(char *s)
{
	long n;
	char *end;

	errno = 0;
	n = strtol(s, &end, 10);
	if (errno) {
		perror("strtol");
		exit(1);
	}
	if (n < 0) {
		fprintf(stderr, "negative duration\n");
		exit(1);
	}
	switch (*end) {
	case 'm': n *= 60; break;
	case 'h': n *= 60*60; break;
	case 'd': n *= 24*60*60; break;
	case 0: break;
	default:
		fprintf(stderr, "junk after duration: %s\n", end);
		exit(1);
	}
	return n;
}

static int
parse(char *expr, char *buf, long bufsiz, int offset)
{
	char *s;
	long i, n = 0, n0 = 0;

	memset(buf, ' ', bufsiz);

	s = expr;
	while (*s) {
		switch (*s) {
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			n = parse_int(&s, -offset, bufsiz);
			buf[n+offset] = '*';
			break;
		case '-':
			n0 = n;
			s++;
			n = parse_int(&s, -offset, bufsiz);
			for (i = n0; i <= n; i++)
				buf[i+offset] = '*';
			break;
		case '/':
			s++;
			n0 = n;
			if (*s)
				n = parse_int(&s, -offset, bufsiz);
			if (n == 0)  // / = *
				n = 1;
			for (i = n0; i < bufsiz; i += n)
				buf[i+offset] = '*';
			break;
		case ',':
			s++;
			n = 0;
			break;
		case '*':
			s++;
			n = 0;
			for (i = 0; i < bufsiz; i++)
				buf[i+offset] = '*';
			break;
		default:
			fprintf(stderr, "can't parse: %s %s\n", expr, s);
			exit(1);
		}
	}

	return 0;
}

char weekday[8] = {0};
char dayofmonth[32] = {0};
char month[13] = {0};
char dayofyear[367] = {0};
char weekofyear[54] = {0};
char hour[24] = {0};
char minute[60] = {0};
char second[61] = {0};

int
isoweek(struct tm *tm)
{
	/* ugh, but easier than the correct formula... */
	char weekstr[3];
	char *w = weekstr;
	strftime(weekstr, sizeof weekstr, "%V", tm);
	return parse_int(&w, 1, 54);
}

time_t
find_next(time_t from)
{
	time_t t;
	struct tm *tm;

	t = from;
	tm = localtime(&t);

next_day:
	while (!(
	    weekday[tm->tm_wday] == '*' &&
	    dayofmonth[tm->tm_mday-1] == '*' &&
	    month[tm->tm_mon] == '*' &&
	    weekofyear[isoweek(tm)-1] == '*' &&
	    dayofyear[tm->tm_yday] == '*')) {
		if (month[tm->tm_mon] != '*') {
			// if month is not good, step month
			tm->tm_mon++;
			tm->tm_mday = 1;
		} else {
			tm->tm_mday++;
		}

		tm->tm_isdst = -1;
		tm->tm_sec = 0;
		tm->tm_min = 0;
		tm->tm_hour = 0;

		t = mktime(tm);
		tm->tm_isdst = -1;

		if (t > from+(366*24*60*60))  // no result within a year
			return -1;
	}

	int y = tm->tm_yday;  // save yday

	while (!(
	    hour[tm->tm_hour] == '*' &&
	    minute[tm->tm_min] == '*' &&
	    second[tm->tm_sec] == '*')) {
		if (hour[tm->tm_hour] != '*') {
			tm->tm_hour++;
			tm->tm_min = 0;
			tm->tm_sec = 0;
		} else if (minute[tm->tm_min] != '*') {
			tm->tm_min++;
			tm->tm_sec = 0;
		} else {
			tm->tm_sec++;
		}
		t = mktime(tm);
		if (tm->tm_yday != y)  // hit a different day, retry...
			goto next_day;
	}

	if (jitter && !nflag) {
		long delay;
		delay = lrand48() % jitter;
		if (vflag)
			printf("adding %lds for jitter.\n", delay);
		t += delay;
	}

	return t;
}

static char isobuf[25];
char *
isotime(const struct tm *tm)
{
	strftime(isobuf, sizeof isobuf, "%FT%T%z", tm);
	return isobuf;
}

int
main(int argc, char *argv[])
{
	int c;
	time_t t;
	time_t now = time(0);
	time_t last = 0;

	/* default: every day at 00:00:00 */
	memset(weekday, '*', sizeof weekday);
	memset(dayofmonth, '*', sizeof dayofmonth);
	memset(month, '*', sizeof month);
	memset(dayofyear, '*', sizeof dayofyear);
	memset(weekofyear, '*', sizeof weekofyear);
	hour[0] = '*';
	minute[0] = '*';
	second[0] = '*';

	setvbuf(stdout, 0, _IOLBF, 0);

#ifdef __linux__
	setenv("TZ", "UTC", 1);
	tzset();
#endif

	while ((c = getopt(argc, argv, "+D:W:H:M:S:T:R:J:d:m:ns:t:vw:")) != -1)
		switch (c) {
		case 'D': parse(optarg, dayofyear, sizeof dayofyear, -1); break;
		case 'W': parse(optarg, weekofyear, sizeof weekofyear, -1); break;
		case 'H': parse(optarg, hour, sizeof hour, 0); break;
		case 'M': parse(optarg, minute, sizeof minute, 0); break;
		case 'S': parse(optarg, second, sizeof second, 0); break;
		case 'd': parse(optarg, dayofmonth, sizeof dayofmonth, -1); break;
		case 'm': parse(optarg, month, sizeof month, -1); break;
		case 'w': parse(optarg, weekday, sizeof weekday, 0);
			// special case: sunday is both 0 and 7.
			if (weekday[7] == '*')
				weekday[0] = '*';
			break;
		case 'n': nflag++; break;
		case 'v': vflag++; break;
		case 's': slack = parse_dur(optarg); break;
		case 'T': timewait = parse_dur(optarg); break;
		case 't': timefile = optarg; break;
		case 'R': randdelay = parse_dur(optarg); break;
		case 'J': jitter = parse_dur(optarg); break;
		default:
			fprintf(stderr, "Usage: %s [-nv] [-t timefile] [-T timewait] [-R randdelay] [-J jitter] [-s slack]\n"
			    "  [-d mday] [-m mon] [-w wday] [-D yday] [-W yweek] [-H hour] [-M min] [-S sec] COMMAND...\n"
			    "Timespec: exact: 1,3,5\n"
			    "          range: 1-7\n"
			    "          every n-th: /10\n", argv[0]);
			exit(2);
		}

	time_t start = now + 1;

	if (timefile) {
		struct stat st;
		if (stat(timefile, &st) < 0) {
			if (errno != ENOENT)
				perror("stat");
			t = start - slack - 1 - timewait;
		} else {
			t = st.st_mtime + 1;
		}
		if (timewait == -1) {
			while (t < start - slack)
				t = find_next(t + 1);
			start = t;
		} else {
			if (t + timewait > start - slack)
				start = t + timewait;
		}
	}

	srand48(getpid() ^ start);

	if (randdelay) {
		long delay;
		delay = lrand48() % randdelay;
		if (vflag)
			printf("randomly delaying by %lds.\n", delay);
		start += delay;
	}

	t = find_next(start);
	if (t < 0) {
		fprintf(stderr, "no satisfying date found within a year.\n");
		exit(2);
	}

	if (nflag) {
		/* dry-run, just output the next 5 dates. */
		int i;
		for (i = 0; i < 5; i++) {
			char weekstr[4];
			struct tm *tm = localtime(&t);
			strftime(weekstr, sizeof weekstr, "%a", tm);
			printf("%s %s %2dd%3dh%3dm%3ds ",
			    isotime(tm),
			    weekstr,
			    ((int)(t - now) / (60*60*24)),
			    ((int)(t - now) / (60*60)) % 24,
			    ((int)(t - now) / 60) % 60,
			    (int)(t - now) % 60);
			if(jitter) {
				printf("(plus up to %ds for jitter)\n", jitter);
			} else {
				printf("\n");
			}
			t = find_next(t + 1);
			if (t < 0) {
				fprintf(stderr,
				    "no satisfying date found within a year.\n");
				exit(2);
			}
		}
		exit(0);
	}

	struct tm *tm = localtime(&t);
	if (vflag)
		printf("Snoozing until %s\n", isotime(tm));

	// setup SIGALRM handler to force early execution
	struct sigaction sa = { 0 };
	sa.sa_handler = &wakeup;
	sa.sa_flags = SA_RESTART;
	sigfillset(&sa.sa_mask);
	sigaction(SIGALRM, &sa, NULL);

	while (!alarm_rang) {
		now = time(0);
		if (now < last) {
			t = find_next(now);
			if (vflag)
				printf("Time moved backwards, rescheduled for %s\n", isotime(tm));
		}
		t = mktime(tm);
		if (t <= now) {
			if (now - t <= slack)  // still about time
				break;
			else {  // reschedule to next event
				if (vflag)
					printf("Missed execution at %s\n", isobuf);
				t = find_next(now + 1);
				tm = localtime(&t);
				if (vflag)
					printf("Snoozing until %s\n", isotime(tm));
			}
		} else {
#ifdef __linux__
			if (t - now > RTC_WKALM_SLACK)
				rtc_wake_at(&t);
#endif
			// do some sleeping, but not more than SLEEP_PHASE
			struct timespec ts;
			ts.tv_nsec = 0;
			ts.tv_sec = t - now > SLEEP_PHASE ? SLEEP_PHASE : t - now;
			last = now;
			nanosleep(&ts, 0);
			// we just iterate again when this exits early
		}
	}

	if (vflag) {
		now = time(0);
		tm = localtime(&now);
		printf("Starting execution at %s\n", isotime(tm));
	}

	// no command to run, the outside script can go on
	if (argc == optind)
		return 0;

#ifdef __linux__
	int n_args = argc - optind;
	char *args[n_args + 2 + inhibitcmd_n_args];
	memset(args, 0, sizeof(args));

	args[0] = calloc(1, strlen(inhibitcmd) + 1);
	memcpy(args[0], inhibitcmd, strlen(inhibitcmd));

	for (int i = 0; i < inhibitcmd_n_args; ++i) {
		args[i+1] = calloc(1, strlen(inhibitcmd_args[i]) + 1);
		memcpy(args[i+1], inhibitcmd_args[i], strlen(inhibitcmd_args[i]));
	}

	for (int i = 0; i < n_args; ++i) {
		args[i+1+inhibitcmd_n_args] = calloc(1, strlen(argv[optind+i]) + 1);
		memcpy(args[i+1+inhibitcmd_n_args], argv[optind+i], strlen(argv[optind+i]));
	}

	execvp(args[0], args);
#else
	execvp(argv[optind], argv+optind);
#endif
	perror("execvp");
	return 255;
}
