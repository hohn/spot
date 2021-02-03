divert(-1)
changequote([, ])

define(SQL_OPTS,
[.bail on
.binary on
.mode ascii
.nullvalue NULL
.separator "^" "\n"])

define(SQL_DEBUG,
[.changes on
.echo on
.eqp on
.expert
.headers on])

define(ENTER_BACKUP,
[.backup sloth_copy.db
.open sloth_copy.db])

divert
