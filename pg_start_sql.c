/* -------------------------------------------------------------------------
*  
* pg_start_sql.c
*  
* Background code to run SQL statement at instance start.
* 
* This code is reusing worker_spi.c code from PostgresSQL code.
* 
* Copyright 2021 Pierre Forstmann
* -------------------------------------------------------------------------
*/
#include "postgres.h"

/* These are always necessary for a bgworker */
#include "miscadmin.h"
#include "postmaster/bgworker.h"
#include "storage/ipc.h"
#include "storage/latch.h"
#include "storage/lwlock.h"
#include "storage/proc.h"
#include "storage/shmem.h"

/* these headers are used by this particular worker's code */
#include "access/xact.h"
#include "executor/spi.h"
#include "fmgr.h"
#include "lib/stringinfo.h"
#include "pgstat.h"
#include "utils/builtins.h"
#include "utils/snapmgr.h"
#include "tcop/utility.h"

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(pg_start_sql_main);

void		_PG_init(void);

/* 
 *  flags set by signal handlers 
 */
static volatile sig_atomic_t got_sighup = false;
static volatile sig_atomic_t got_sigterm = false;

/* GUC variables */
static 	char	*pg_start_sql_dbname = NULL;
static 	char	*pg_start_sql_stmt = NULL;
static 	char	*pg_start_sql_file = NULL;

#define	LINE_SIZE 4096 

/*
 *  Signal handler for SIGTERM
 *  Set a flag to let the main loop to terminate, and set our latch to wake
 *  it up.
 */  
static void
pg_start_sql_sigterm(SIGNAL_ARGS)
{
	int			save_errno = errno;

	got_sigterm = true;
	SetLatch(MyLatch);

	errno = save_errno;
}

/*
 *  Signal handler for SIGHUP
 *  Set a flag to tell the main loop to reread the config file, and set
 *  our latch to wake it up.
 */
static void
pg_start_sql_sighup(SIGNAL_ARGS)
{
	int			save_errno = errno;

	got_sighup = true;
	SetLatch(MyLatch);

	errno = save_errno;
}

Datum
pg_start_sql_main(PG_FUNCTION_ARGS)
{
	StringInfoData 	buf_select;	
	int		ret;
	FILE		*fp;
	char		fline[LINE_SIZE];

	/* Establish signal handlers before unblocking signals. */
	pqsignal(SIGHUP, pg_start_sql_sighup);
	pqsignal(SIGTERM, pg_start_sql_sigterm);

	/* We're now ready to receive signals */
	BackgroundWorkerUnblockSignals();

	/* Connect to our database */
#if PG_VERSION_NUM >=110000
	BackgroundWorkerInitializeConnection(pg_start_sql_dbname, NULL, 0);
#else
	BackgroundWorkerInitializeConnection(pg_start_sql_dbname, NULL);
#endif
	elog(LOG, "pg_start_sql: %s initialized in database %s", MyBgworkerEntry->bgw_name, pg_start_sql_dbname);

	/*
  	 * no main loop: run SQL statements and exit
  	 */

	/*
  	 * Start a transaction on which we can run queries.  Note that each
  	 * StartTransactionCommand() call should be preceded by a
  	 * SetCurrentStatementStartTimestamp() call, which sets both the time
  	 * for the statement we're about the run, and also the transaction
  	 * start time.  Also, each other query sent to SPI should probably be
  	 * preceded by SetCurrentStatementStartTimestamp(), so that statement
  	 * start time is always up to date.
 	 *  The SPI_connect() call lets us run queries through the SPI manager,
 	 * and the PushActiveSnapshot() call creates an "active" snapshot
 	 * which is necessary for queries to have MVCC data to work on.
 	 *
 	 * The pgstat_report_activity() call makes our activity visible through the pgstat views.
  	 */
	SetCurrentStatementStartTimestamp();
	StartTransactionCommand();
	SPI_connect();
	PushActiveSnapshot(GetTransactionSnapshot());
	pgstat_report_activity(STATE_RUNNING, pg_start_sql_stmt);

	/* We can now execute queries via SPI */

	/* 
         * pg_start_sql.stmt 
	 */

	if (pg_start_sql_stmt != NULL)
	{
		initStringInfo(&buf_select);
		appendStringInfoString(&buf_select, pg_start_sql_stmt);
		elog(LOG, "pg_start_sql: running %s", buf_select.data);	
		ret = SPI_execute(buf_select.data, false, 0);

		if (ret != SPI_OK_SELECT)
			elog(ERROR, "pg_start_sql: %s failed: error code %d", buf_select.data, ret);
	}
	
	/* 
         * pg_start_sql.file 
	 */

	if (pg_start_sql_file != NULL)
	{
		fp = AllocateFile(pg_start_sql_file, "r");
		if (fp == NULL)
			elog(ERROR, "could not open file \"%s\" ", pg_start_sql_file);

		while (fgets(fline, sizeof(fline), fp) != NULL)
		{
			initStringInfo(&buf_select);
			appendStringInfoString(&buf_select, fline);
			pgstat_report_activity(STATE_RUNNING, buf_select.data);
			elog(LOG, "pg_start_sql: running %s", buf_select.data);	
			ret = SPI_execute(buf_select.data, false, 0);

			if (ret != SPI_OK_SELECT)
				elog(ERROR, "pg_start_sql: %s failed: error code %d", buf_select.data, ret);
		}
		FreeFile(fp);
	}

	/*
 	 * And finish our transaction.
 	 */
	SPI_finish();
	PopActiveSnapshot();
	CommitTransactionCommand();
	pgstat_report_stat(false);
	pgstat_report_activity(STATE_IDLE, NULL);

	elog(LOG, "pg_start_sql: exiting");

	proc_exit(0);
}

/*
 *  Entry point of this module.
 */
void
_PG_init(void)
{
	BackgroundWorker worker;

	/* get the configuration */
	DefineCustomStringVariable("pg_start_sql.dbname",
				"database name",
				NULL,
				&pg_start_sql_dbname,
				NULL,
				PGC_POSTMASTER,
				0,
				NULL,
				NULL,
				NULL);

	if (pg_start_sql_dbname == NULL)		
		pg_start_sql_dbname = "postgres";


	/* get the configuration */
	DefineCustomStringVariable("pg_start_sql.stmt",
				"SQL statement",
				NULL,
				&pg_start_sql_stmt,
				NULL,
				PGC_POSTMASTER,
				0,
				NULL,
				NULL,
				NULL);

	/* get the configuration */
	DefineCustomStringVariable("pg_start_sql.file",
				"SQL file name",
				NULL,
				&pg_start_sql_file,
				NULL,
				PGC_POSTMASTER,
				0,
				NULL,
				NULL,
				NULL);

	if (pg_start_sql_stmt == NULL && pg_start_sql_file == NULL)		
		elog(FATAL, "pg_start_sql.stmt and pg_start_sql.file_name are not set");


	/* set up common data for all our workers */
	memset(&worker, 0, sizeof(worker));
	worker.bgw_flags = BGWORKER_SHMEM_ACCESS |
		BGWORKER_BACKEND_DATABASE_CONNECTION;
	worker.bgw_start_time = BgWorkerStart_RecoveryFinished;
	sprintf(worker.bgw_library_name, "pg_start_sql");
	sprintf(worker.bgw_function_name, "pg_start_sql_main");
	worker.bgw_restart_time = BGW_NEVER_RESTART;
	worker.bgw_notify_pid = 0;

	snprintf(worker.bgw_name, BGW_MAXLEN, "pg_start_sql_worker");
#if PG_VERSION_NUM >= 110000
	snprintf(worker.bgw_type, BGW_MAXLEN, "pg_start_sql_worker");
#endif
	worker.bgw_main_arg = 0;

	RegisterBackgroundWorker(&worker);

	elog(LOG, "%s started", worker.bgw_name);

}
