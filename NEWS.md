## HEAD

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
