sloth -- version control system
===============================

sloth is a minimalistic version control system that does not support
branches, multiple users or remotes. Saying that, sloth still rocks as it
works off a *simple* SQLite database, making it easy to understand.

It also has these nice features:

* Cross-platform,
* Import from and export to git,
* ISC license.


Install
-------

To build `sloth` simply run:
```
$ cc -O3 -o sloth sloth.c
```
or
```
> cl sloth.c
```
and place `sloth` or `sloth.exe`, *along with all of the SQL scripts*,
into the same directory somewhere in your `PATH`.

`sqlite3` and `m4` are required to run `sloth`.
I have a cross-platform `m4` implementation available.

Synopsis
--------

To use `sloth` the synopsis is:

```
sloth init|log|import|export
sloth commit msg [time]
```

Enjoy,
Logan =)_
