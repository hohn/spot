divert(-1)
changequote([, ])

define(LIB_DIR, [/home/logan/lib])

define(DIR_SEP,
[/])

define(SQL_OPTS,
[.bail on
.binary on
.mode ascii
.nullvalue NULL
.separator "^" "\n"
.load LIB_DIR/sha1])

define(SQL_DEBUG,
[.changes on
.echo on
.eqp on
.expert on
.headers on])

divert
