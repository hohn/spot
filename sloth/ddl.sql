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

/* Stop after first error */
.bail on

create table sloth_commit
(cid integer not null unique primary key,
t integer not null unique,
msg text not null,
check(msg <> '')
);

create table sloth_blob
(bid integer not null unique primary key,
d blob not null unique
);

create unique index uidx_blob_data on sloth_blob(d);

create table sloth_file
(cid integer not null,
bid integer not null,
fn text not null,
check(fn <> '')
);

create index idx_file_cid on sloth_file(cid);
create index idx_file_bid on sloth_file(bid);

create table sloth_track
(fn text not null unique primary key,
check(fn <> '')
);

create table sloth_stage
(fn text not null unique primary key,
d blob not null unique,
check(fn <> '')
);

create table sloth_tmp_cid
(cid integer not null unique primary key
);

create table sloth_tmp_t
(t integer not null unique primary key
);

create table sloth_user
(full_name not null unique primary key,
email text not null unique,
check(full_name <> ''),
check(email <> '')
);

delete from sloth_user;
insert into sloth_user (full_name, email)
values('Logan McLintock', 'loganpkg@gmail.com');

/* Only one zero is accepted */
create table sloth_tmp_trap
(x integer not null unique,
check (x = 0)
);
