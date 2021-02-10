/*
 * Copyright (c) 2021 Logan Ryan McLintock
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* Data Definition Language (DDL) for sloth */

SQL_OPTS
SQL_DEBUG

create table sloth_commit
(t integer not null unique primary key,
msg text not null,
check(msg <> '')
);

create table sloth_blob
(h text not null unique primary key,
d blob not null unique
);

create unique index uidx_blob_data on sloth_blob(d);

create table sloth_file
(fn text not null,
h text not null,
entry_t integer not null, /* Inclusive */
exit_t integer not null, /* Exclusive */
check(fn <> '')
);

/* Probably need some time index */
create index idx_file_h on sloth_file(h);

create table sloth_track
(fn text not null unique primary key,
check(fn <> '')
);

create table sloth_stage
(fn text not null unique primary key,
d blob not null unique,
check(fn <> '')
);

create table sloth_stage_clamp
(fn text not null unique primary key,
h text not null unique,
check(fn <> '')
);

create table sloth_tmp_t
(t integer not null unique primary key
);

/* Time of the previous commit */
create table sloth_prev_t
(t integer not null unique primary key
);

create table sloth_tmp_msg
(msg text not null unique primary key,
check(msg <> '')
);

/* Just used for export */
create table sloth_user
(full_name not null unique primary key,
email text not null unique,
check(full_name <> ''),
check(email <> '')
);

/* Just used for export */
create table sloth_blob_mark
(h text not null unique primary key,
mk integer not null unique
);

create unique index uidx_blob_mark_mk on sloth_blob_mark(mk);

/* Just used for export */
create table sloth_commit_mark
(t integer not null unique primary key,
mk integer not null unique
);

create unique index uidx_commit_mark_mk on sloth_commit_mark(mk);


/* Only one zero is accepted */
create table sloth_non_zero_trap
(x integer not null unique,
check (x = 0)
);

.quit
