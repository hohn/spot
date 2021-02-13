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

/* sloth subdir SQL */

SQL_OPTS

update sloth_file
set fn = (select trim(a.x) from sloth_tmp_text as a) || 'DIR_SEP' || fn;

update sloth_commit
set msg = (select trim(a.x) from sloth_tmp_text as a) || ': ' || msg;

/*
 * The .track file is only read during a commit operation, so changes made
 * without a sucessful commit will be discarded.
 */
update sloth_track
set fn = (select trim(a.x) from sloth_tmp_text as a) || 'DIR_SEP' || fn;

/* Write .track file. This is not atomic but it is external to the database. */
.output .track
select fn from sloth_track;
.output

.quit
