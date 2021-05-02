# pg_start_sql Makefile

MODULES = pg_start_sql

EXTENSION = pg_start_sql

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
