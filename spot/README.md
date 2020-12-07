README
======

Welcome to spot
---------------

spot is a minimalistic and fast text editor.

spot:

* Is cross-platform.
* Does not use any curses library.
* Uses a double buffering method for graphics which has no flickering.
* Uses the gap buffer method for text with transactional operations.
* Uses the Quick Search algorithm with a reusable bad character table on
  repeated searches.
* Is released under the ISC License.

Install
-------

On POSIX systems:
```
$ cc -O3 -o spot lbuf.c lcurses.c spot.c
```
otherwise:
```
> cl spot.c lbuf.c lcurses.c
```
and place `spot` or `spot.exe` somewhere in your PATH.
