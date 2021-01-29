divert(-1)
changequote([, ])

define(SQL_OPTS,
[.bail on
.changes on
.echo on
.eqp on
.expert
.headers on
.mode ascii
.nullvalue NULL
.separator "^" "\n"])

define(ENTER_BACKUP,
[.backup sloth_copy.db
.open sloth_copy.db])

divert
