# pg_start_sql Makefile

MODULES = pg_start_sql

EXTENSION = pg_start_sql

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

#
pgxn:
	git archive --format zip  --output ../pgxn/pg_start_sql/pg_start_sql-1.0.1.zip master
