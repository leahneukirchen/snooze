/*
 * snooze - run a command at a particular time
 *
 * To the extent possible under law,
 * Christian Neukirchen <chneukirchen@gmail.com>
 * has waived all copyright and related or neighboring rights to this work.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

/*
##% gcc -Os -Wall -g -o $STEM $FILE -Wextra -Wwrite-strings
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
static char *timefile;

static sig_atomic_t alarm_rang = 0;

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
char dayofmonth[31] = {0};
char month[12] = {0};
char dayofyear[366] = {0};
char weekofyear[53] = {0};
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
	while (!(weekday[tm->tm_wday] == '*'
	    && dayofmonth[tm->tm_mday-1] == '*'
	    && month[tm->tm_mon] == '*'
	    && weekofyear[isoweek(tm)] == '*'
	    && dayofyear[tm->tm_yday] == '*')) {
		if (month[tm->tm_mon] != '*') {
			// if month is not good, step month
			tm->tm_mon++;
			tm->tm_mday = 1;
		} else {
			tm->tm_mday++;
		}

		tm->tm_sec = 0;
		tm->tm_min = 0;
		tm->tm_hour = 0;

		t = mktime(tm);
		if (t > from+(365*24*60*60))  // no result within a year
			return -1;
	}

	int y = tm->tm_yday;  // save yday

	while (!(hour[tm->tm_hour] == '*'
	    && minute[tm->tm_min] == '*'
	    && second[tm->tm_sec] == '*')) {
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
	    
	return t;
}

static char isobuf[25];
char *
isotime(const struct tm *tm)
{
	strftime(isobuf, sizeof isobuf, "%FT%T%z", tm);
	return isobuf;
}

int main(int argc, char *argv[])
{
	int c;
	time_t t;
	time_t now = time(0);

	/* default: every day at 00:00:00 */
	memset(weekday, '*', sizeof weekday);
	memset(dayofmonth, '*', sizeof dayofmonth);
	memset(month, '*', sizeof month);
	memset(dayofyear, '*', sizeof dayofyear);
	memset(weekofyear, '*', sizeof weekofyear);
	hour[0] = '*';
	minute[0] = '*';
	second[0] = '*';

	while ((c = getopt(argc, argv, "D:W:H:M:S:T:R:d:m:ns:t:vw:")) != -1)
                switch(c) {
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
		default:
                        fprintf(stderr, "Usage: %s [-nv] [-t timefile] [-T timewait] [-R randdelay] [-s slack]\n"
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
			if (t + timewait > start)
				start = st.st_mtime + timewait;
		}
	}

	if (randdelay) {
		long delay;
#ifdef __linux__
		long rnd = getauxval(AT_RANDOM);
		if (rnd > 0)
			delay = rnd % randdelay;
		else
#endif
		{
			srand48(getpid() ^ start);
			delay = lrand48() % randdelay;
		}
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
			if (t > 0) {
				char weekstr[4];
				struct tm *tm = localtime(&t);
				strftime(weekstr, sizeof weekstr, "%a", tm);
				printf("%s %s %2ldd%3ldh%3ldm%3lds\n",
				    isotime(tm),
				    weekstr,
				    ((t - now) / (60*60*24)),
				    ((t - now) / (60*60)) % 24,
				    ((t - now) / 60) % 60,
				    (t - now) % 60);
			}
			t = find_next(t + 1);
		}
		exit(0);
	}

	struct tm *tm = localtime(&t);
	if (vflag)
		printf("Snoozing until %s\n", isotime(tm));

	// setup SIGALRM handler to force early execution
	struct sigaction sa;
	sa.sa_handler = &wakeup;
	sa.sa_flags = SA_RESTART;
	sigfillset(&sa.sa_mask);
	sigaction(SIGALRM, &sa, NULL);  // XXX error handling

	while (!alarm_rang) {
		now = time(0);
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
			// do some sleeping, but not more than SLEEP_PHASE
			struct timespec ts;
			ts.tv_nsec = 0;
			ts.tv_sec = t - now > SLEEP_PHASE ? SLEEP_PHASE : t - now;
			nanosleep(&ts, 0);
			// we just iterate again when this exits early
		}
	}

	// no command to run, the outside script can go on
	if (argc == optind)
		return 0;

	if (vflag) {
		now = time(0);
		tm = localtime(&now);
		printf("Starting execution at %s\n", isotime(tm));
	}

	execvp(argv[optind], argv+optind);
	perror("execvp");
	return 255;
}
