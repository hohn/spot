/* Data Definition Language (DDL) for sloth */

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
(fn text not null unique primary key
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
