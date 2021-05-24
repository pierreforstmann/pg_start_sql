# pg_start_sql
PostgreSQL extension to execute SQL statements at instance start.


# Installation
## Compiling

This module can be built using the standard PGXS infrastructure. For this to work, the pg_config program must be available in your $PATH:
  
`git clone https://github.com/pierreforstmann/pg_start_sql.git` <br>
`cd pg_start_sql` <br>
`make` <br>
`make install` <br>

## PostgreSQL setup

Extension must be loaded:

At server level with `shared_preload_libraries` parameter: <br> 
`shared_preload_libraries = 'pg_start_sql'` <br>
And following SQL statement should be run: <br>
`create extension pg_start_sql;`

This extension is installed at instance level: it does not need to be installed in each database. <br>

`pg_start_sql`  has been successfully tested with PostgreSQL 9.5, 9.6, 10, 11, 12 and 13. <br>

## Usage
pg_start_sql has 3 GUC parameters:
* `pg_start_sql.dbname` which is the database name where SQL statements must be run. This parameter is not mandatory : if not specified SQL statement is run in postgres database.
* `pg_start_sql.stmt` which is the SQL statement to be run.
* `pg_start_sql.file` which is a file name whose SQL statements are to be run.
At least one of the parameters`pg_start_sql.stmt` or `pg_start_sql.file` must be specified.

Statements are run with superuser privileges. There is no way to specify another database user to run SQL statements.


