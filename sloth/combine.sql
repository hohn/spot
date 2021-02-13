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

/* sloth combine SQL */

SQL_OPTS

/* Attach other database */
.output .tmp
select 'attach database ''' || (select a.x from sloth_tmp_text as a) || ''' as other;';
.output
.read .tmp

/* Make sure there are no conflicting file paths */
delete from main.sloth_non_zero_trap;

/* Will create an error if both repos have a file path the same */
insert into main.sloth_non_zero_trap
select
count(a.fn)
from main.sloth_file as a
where a.fn in (select b.fn from other.sloth_file as b group by b.fn);

/* Load data from other repo into main repo */
insert into main.sloth_commit
select * from other.sloth_commit;

insert into main.sloth_file
select * from other.sloth_file;

/* Load unique blobs only */
insert into main.sloth_blob
select * from other.sloth_blob as a
where a.h not in (select b.h from main.sloth_blob as b);

/* Only the commit operation reads .track files */
insert into main.sloth_track
select * from other.sloth_track;

/* Write .track file. This is not atomic but it is external to the database. */
.output .track
select fn from main.sloth_track;
.output

detach database other;

.quit
