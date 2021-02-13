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

/* sloth commit SQL */

SQL_OPTS
SQL_DEBUG

/* Set files to track */
delete from sloth_track;

.import .track sloth_track

/* Import files */
delete from sloth_stage;

insert into sloth_stage (fn, d)
select fn, readfile(fn) from sloth_track;

/* Clean files: Remove problem characters */
/* Carriage Return, \r, ^M */
update sloth_stage
set d = replace(d, X'0D', '');

/* Null character, \0, ^@ */
update sloth_stage
set d = replace(d, X'00', '');

insert into sloth_blob (h, d)
select
sha1(a.d) as h,
a.d
from sloth_stage as a
where a.d not in (select b.d from sloth_blob as b);

/* Clamp the files */
delete from sloth_stage_clamp;

insert into sloth_stage_clamp
select
a.fn,
b.h
from sloth_stage as a
inner join sloth_blob as b
on a.d = b.d
;

delete from sloth_non_zero_trap;

/* Will create an error if there is an old time greater than the current time */
insert into sloth_non_zero_trap
select
count(a.t)
from sloth_commit as a
where a.t > (select b.i from sloth_tmp_int as b);

/* Record the time of the last commit before this one in progress */
delete from sloth_prev_t;

insert into sloth_prev_t (t)
select max(t) from sloth_commit;

/* Fill in commit info */
insert into sloth_commit (t, msg)
select
(select b.i from sloth_tmp_int as b),
(select trim(c.x) from sloth_tmp_text as c)
;

/* Close off open records that are now gone (not staged) */
update sloth_file
set exit_t = (select b.i from sloth_tmp_int as b)
where
/* Record is open */
(select c.i from sloth_tmp_int as c) >= entry_t
and (select d.i from sloth_tmp_int as d) < exit_t
/* Record now gone */
and (fn, h) not in
(select
e.fn,
e.h
from sloth_stage_clamp as e
);

/* Delete staged present records that are still open */
delete from sloth_stage_clamp as a
where (a.fn, a.h) in
(select
b.fn,
b.h
from sloth_file as b
where
/* Record is open */
(select c.i from sloth_tmp_int as c) >= b.entry_t
and (select d.i from sloth_tmp_int as d) < b.exit_t
);

/* Insert new records */
insert into sloth_file (fn, h, entry_t, exit_t)
select
a.fn,
a.h,
(select b.i from sloth_tmp_int as b),
/* Will not overflow if timezone is added */
strftime('%s', '9999-12-31 00:00:00')
from sloth_stage_clamp as a
;

delete from sloth_non_zero_trap;

/* Will create an error if there have been no changes */
insert into sloth_non_zero_trap (x)
select z.count_now = z.count_prev and z.count_now = z.count_union
from
(select
    (select count(w.h) from sloth_file as w
        where (select a.i from sloth_tmp_int as a) >= w.entry_t
          and (select b.i from sloth_tmp_int as b) <  w.exit_t
    ) as count_now,

    (select count(x.h) from sloth_file as x
        where (select c.t from sloth_prev_t as c) >= x.entry_t
          and (select d.t from sloth_prev_t as d) <  x.exit_t
    ) as count_prev,

    (select count(y.h) from
        (select q.h, q.fn from sloth_file as q
            where (select e.i from sloth_tmp_int as e) >= q.entry_t
              and (select f.i from sloth_tmp_int as f) <  q.exit_t
        union
        select r.h, r.fn from sloth_file as r
            where (select g.t from sloth_prev_t as g) >= r.entry_t
              and (select h.t from sloth_prev_t as h) <  r.exit_t
        ) as y
    ) as count_union
) as z;

.quit
