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
ENTER_BACKUP

/* Will be passed as an arg */
delete from sloth_tmp_msg;
insert into sloth_tmp_msg (msg) values('save');

/* Test files */
delete from sloth_track;
insert into sloth_track (fn) values ('mouse');
insert into sloth_track (fn) values ('dog');

delete from sloth_stage;

insert into sloth_stage (fn, d)
select fn, readfile(fn) from sloth_track;

/* bid will be auto filled */
insert into sloth_blob (d)
select a.d from sloth_stage as a
where a.d not in (select b.d from sloth_blob as b);

delete from sloth_tmp_t;

insert into sloth_tmp_t (t)
select strftime('%s','now');

delete from sloth_tmp_trap;

/* Will create an error if there is an old time greater than the current time */
insert into sloth_tmp_trap
select
count(a.t)
from sloth_commit as a
where a.t > (select b.t from sloth_tmp_t as b);

insert into sloth_commit (t, msg)
select
(select b.t from sloth_tmp_t as b),
(select c.msg from sloth_tmp_msg as c)
;


/* Close off open records that are now gone */
update sloth_file
set exit_t = (select b.t from sloth_tmp_t as b)
where
/* Record is open */
(select c.t from sloth_tmp_t as c) >= entry_t
and (select d.t from sloth_tmp_t as d) < exit_t
/* Record now gone */
and (fn, bid) not in
(select
e.fn,
f.bid
from sloth_stage as e
inner join sloth_blob as f
on e.d = f.d
)
;

/* Delete staged present records that are still open */
delete from sloth_stage as a
where a.fn in
(select
b.fn
from sloth_stage as b
inner join sloth_blob as c
on b.d = c.d
where (b.fn, c.bid) in
(select
d.fn,
d.bid
from sloth_file as d
where
/* Record is open */
(select e.t from sloth_tmp_t as e) >= d.entry_t
and (select f.t from sloth_tmp_t as f) < d.exit_t
)
)
;

/* Insert new records */
insert into sloth_file (fn, bid, entry_t, exit_t)
select
a.fn,
b.bid,
(select c.t from sloth_tmp_t as c),
/* Will not overflow if timezone is added */
strftime('%s', '9999-12-31 00:00:00')
from sloth_stage as a
inner join sloth_blob as b
on a.d = b.d
;

delete from sloth_tmp_trap;

/* Will create an error if there have been no changes */
/* UP TO HERE */
/*
insert into sloth_tmp_trap (x)
select x.c_now = x.c_prev and x.c_now = x.c_union
from
(select
    (select count(a.bid) from sloth_file as a
        where a.cid = (select b.cid from sloth_tmp_cid as b)) as c_now,
    (select count(c.bid) from sloth_file as c
        where c.cid = (select d.cid - 1 from sloth_tmp_cid as d)) as c_prev,
    (select count(k.bid) from
        (select e.bid, e.fn from sloth_file as e
            where e.cid = (select f.cid from sloth_tmp_cid as f)
        union
        select g.bid, g.fn from sloth_file as g
            where g.cid = (select h.cid - 1 from sloth_tmp_cid as h)
        ) as k
    ) as c_union
) as x;
*/

.quit
