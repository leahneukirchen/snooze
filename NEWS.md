## HEAD

## 0.6 (2026-08-11)

* Incompatible change: if a timefile has missed execution in the past,
  trigger immediately (this is the behavior you usually want for
  anacron-like behavior).
* Feature: `4-7/2` is now parsed correctly.
* Feature: `*/4` is now parsed like `/4`.
* Feature: reevaluate timer on SIGCONT.
* Bugfix: fix race condition in SIGALRM.
* Bugfix: jitter and randdelay were off by one.
* Many small bugfixes and code improvements.

## 0.5.1 (2025-10-03)

* Minor bug fixes and documentation improvements.

## 0.5 (2021-01-14)

* Feature: add `-J` for jitter.
* Bugfix: fix verbose output when no command is passed.
* Bugfix: change timefile calculations to respect slack.

## 0.4 (2020-02-07)

* Feature: include runit service files to run cron drop-in scripts.
* Bugfix: force stdout line-buffered.

## 0.3 (2018-05-05)

* Bugfix: off-by-one for month and day of month.
* Bugfix: weird scheduling across DST changes.

## 0.2 (2017-08-29)

* Bugfix: Enforce POSIX option processing (stop argument parsing after
  first nonoption).
* Bugfix: off-by-one during week of year parsing.
* Bugfix: in a leap year, finding the next event can take longer than 365 days.
* Feature: `-X/` now works like `-X*` for all time fields.

## 0.1 (2016-01-05)

* Initial release.
