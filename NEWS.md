## HEAD

## 0.4 (2020-02-07)

* Feature: include runit service files to run cron drop-in scripts.
* Bug: force stdout line-buffered.

## 0.3 (2018-05-05)

* Bug: off-by-one for month and day of month.
* Bug: weird scheduling across DST changes.

## 0.2 (2017-08-29)

* Bug: Enforce POSIX option processing (stop argument parsing after
  first nonoption).
* Bug: off-by-one during week of year parsing.
* Bug: in a leap year, finding the next event can take longer than 365 days.
* Feature: `-X/` now works like `-X*` for all time fields.

## 0.1 (2016-01-05)

* Initial release
