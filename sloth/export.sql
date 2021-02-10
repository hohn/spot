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

/* Export SQL for sloth */

SQL_OPTS

/* Set user info from file */
delete from sloth_user;
.import .user sloth_user

/* Generate the blobs marks */
delete from sloth_blob_mark;

insert into sloth_blob_mark (h, mk)
select
h,
row_number() over (order by h asc) as mk
from sloth_blob;


/* Export the blobs first */
select
'blob' || x'0A'
|| 'mark :' || b.mk || x'0A'
|| 'data ' || length(a.d) || x'0A'
|| a.d
from sloth_blob as a
inner join sloth_blob_mark as b
on a.h = b.h
order by b.mk asc;


/* Generate the commit marks */
delete from sloth_commit_mark;

insert into sloth_commit_mark (t, mk)
select
a.t,
row_number() over (order by a.t asc)
    + (select max(b.mk) from sloth_blob_mark as b) as mk
from sloth_commit as a;


/* Export the commits */
select
'commit refs/heads/master' || x'0A'
|| 'mark :' || c.mk || x'0A'
|| 'author '
    || (select d.full_name || ' <' || d.email || '> ' from sloth_user as d)
    || a.t || ' +0000' || x'0A'
|| 'committer '
    || (select e.full_name || ' <' || e.email || '> ' from sloth_user as e)
    || a.t || ' +0000' || x'0A'
|| 'data ' || (length(a.msg) + 1) || x'0A'
|| a.msg || x'0A'
|| case when a.t <> (select min(f.t) from sloth_commit as f)
    then 'from :' || (c.mk - 1) || x'0A'
    else '' end
|| 'deleteall' || x'0A'
|| group_concat('M 100644 :' || d.mk || ' ' || b.fn, x'0A')
from sloth_commit as a
inner join sloth_file as b
on a.t >= b.entry_t and a.t < b.exit_t
inner join sloth_commit_mark as c
on a.t = c.t
inner join sloth_blob_mark as d
on b.h = d.h
group by a.t, a.msg
order by a.t asc;
