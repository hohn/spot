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

begin;

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

delete from sloth_tmp_cid;

insert into sloth_tmp_cid (cid)
select coalesce((select max(cid) from sloth_commit), 0) + 1;

insert into sloth_commit (cid, t, msg)
select
(select a.cid from sloth_tmp_cid as a),
strftime('%s','now'),
'save'
;

insert into sloth_file (cid, bid, fn)
select
(select c.cid from sloth_tmp_cid as c),
b.bid,
a.fn
from sloth_stage as a
inner join sloth_blob as b
on a.d = b.d
;

commit;
