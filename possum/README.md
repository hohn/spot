possum
======

possum organises photos and videos based on the creation date stored
inside the file. It is cross-platform and released under the ISC license.


Install
-------

Firstly, `exiftool` and `jdupes` must be available to run `possum`.
Edit the paths in possum.c under the comment *"Set paths to the dependencies"*
to point to where the `exiftool` and `jdupes` executables are on your
system.

Now, to build `possum` simply run:
```
$ cc -O3 -o possum possum.c
```
or
```
> cl possum.c
```
and place `possum` or `possum.exe` somewhere in your `PATH`.


Synopsis
--------

To use `possum` the synopsis is:

```
possum search_dir store_dir
```

Enjoy,
Logan =)_
