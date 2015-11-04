## snooze: run a command at a particular time

`snooze` is a new tool for to wait until a particular time and then
run a command.  Together with a service supervision system such as runit,
this can be used to replace cron(8).

`lr` has been tested on Linux 4.2.
It will likely work on other Unix-like systems with C99.

## Benefits

Over cron:
- mnemonic syntax
- no overlapping job runs possible
- filtering by ISO week and day of year
- due to supervision, no centralized daemon required
- due to supervision, can easily disable jobs or force their
  execution instantly
- due to supervision, have custom logs
- due to no centralized daemon, no fuzzing with multiple users/permissions
- very robust with respect to external time changes
- can use a file timestamp to ensure minimum waiting time between two
  runs, even across reboots
- randomized delays (some cron have that)
- variable slack (no need for anacron)

Over runwhen:
- less confusing usage (I hope)
- filtering by ISO week and day of year
- zero dependencies

Over uschedule:
- due to supervision, no centralized daemon required

## Rosetta stone

* run five minutes after midnight, every day:
  cron: `5 0 * * *`
  snooze: `-M5`
* run at 2:15pm on the first of every month:
  cron: `15 14 1 * *`
  snooze: `-d1 -H2 -M15`
* run at 10 pm on weekdays:
  cron: `0 22 * * 1-5`
  snooze: `-w1-5 -H22`
* run 23 minutes after midnight, 2am, 4am ..., everyday:
  cron: `23 0-23/2 * * *`
  snooze: `-H/2 -M23`
* run every second week:
  snooze: `-W/2`
* run every 10 days:
  snooze: `-D/10`

## Usage:

	snooze [-nv] [-t timefile] [-T timewait] [-R randdelay] [-s slack] [-d mday] [-m mon] [-w wday] [-D yday] [-W yweek] [-H hour] [-M min] [-S sec] COMMAND...

* `-n`: dry-run, print the next 5 times the command would run.
* `-v`: verbose, print scheduled (and rescheduled) times.
* `-t`, `-T`: see below timefiles
* `-R`: add between 0 and RANDDELAY seconds to the scheduled time.
* `-s`: commands are executed even if they are SLACK (default: 60) seconds late.

The durations RANDDELAY and SLACK and TIMEWAIT are parsed as seconds,
unless a postfix of `m` for minutes, `h` for hours, or `d` for days is used.

The remaining arguments are patterns for the time fields:

* `-d`: day of month
* `-m`: month
* `-w`: weekday (0-7, sunday is 0 and 7)
* `-D`: day of year
* `-W`: ISO week of year (0..53)
* `-H`: hour
* `-M`: minute
* `-S`: second

The following syntax is used for these options:

* exact match: `-d 3`, run on the 3rd
* alternation: `-d 3,10,27`, run on 3rd, 10th, 27th
* range: `-d 1-5`, run on 1st, 2nd, 3rd, 4th, 5th
* star: `-d '*'`, run every day
* repetition: `-d /5`, run on 5th, 10th, 15th, 20th, 25th, 30th day
* shifted repetition: `-d 2/5`, run on 7th, 12th, 17th, 22nd, 27th day

and combinations of those, e.g. `-d 1-10,15/5,28`.

The defaults are `-d* -m* -w* -D* -W* -H0 -M0 -S0`, that is, every midnight.

Note that *all* patterns need to match (contrary to cron where either
day of month *or* day of week matches), so `-w5 -d13` only runs on
Friday the 13th.

## Timefiles

Optionally, you can keep track of runs in time files, using `-t` and
optionally `-T`.

When `-T` is passed, execution will not start earlier than the mtime
of TIMEFILE plus TIMEWAIT seconds.

When `-T` is *not* passed, snooze will start finding the first matching time
starting from the mtime of TIMEFILE, and taking SLACK into account.
(E.g. `-H0 -s 1d -t timefile` will start an instant
execution when timefile has not been touched today, whereas without `-t`
this would always wait until next midnight.)

If TIMEFILE does not exist, it will be assumed outdated enough to
ensure earliest execution.

snooze does not update the timefiles, your job needs to do that!
Only mtime is looked at, so touch(1) is good.

## Exact behavior

* snooze parses the option flags and computes the first time the
  date pattern matches, as a symbolic date
* if a timefile is specified, the time is upped to timefile + timewait seconds
* if a random delay is requested, it is added
* snooze computes how far this event is in the future
* snooze sleeps that long, but at most 5 minutes
* after waking, snooze recomputes how far the event is in the future
* if the event is in the past, but fewer than SLACK seconds, snooze
  execs the command.  You need to ensure (by setting up supervision)
  snooze runs again after that!
* if we woke due to a SIGALRM, the command is executed immediately as well
* if the event is in the future, recompute the time it takes, possibly
  considering shifting of the system time or timezone changes
  (possibly only works on glibc)
* If no command was given, just return with status 0
* and so on...

## Common usages

Run a job like cron, every day at 7am and 7pm:

	exec snooze -H7,19 rdumpfs / /data/dump/mybox 2>&1

Run a job daily, never twice a day:

	exec snooze -H0 -s 1d -t timefile \
		sh -c 'run-parts /etc/cron.daily; touch timefile'

Use snooze inline, run a mirror script every hour at 30 minutes past,
but ensure there are at least 20 minutes in between.

	set -e
	snooze -H'*' -M30 -t timefile -T 20m
	touch timefile  # remove this if instantly retrying on failure were ok
	mirrorallthethings
	touch timefile

Use snooze inline, cron-style mail:

	set -e
	snooze ...
	actualjob >output 2>&1 ||
		mail -s "$(hostname): snooze job failed with status $?" root <output

## Installation

Use `make all` to build, `make install` to install relative to `PREFIX`
(`/usr/local` by default).  The `DESTDIR` convention is respected.
You can also just copy the binary into your `PATH`.

## Copyright

snooze is in the public domain.

To the extent possible under law,
Christian Neukirchen <chneukirchen@gmail.com>
has waived all copyright and related or
neighboring rights to this work.

http://creativecommons.org/publicdomain/zero/1.0/
