/*
 * Copyright (C) 1994-2018 Altair Engineering, Inc.
 * For more information, contact Altair at www.altair.com.
 *
 * This file is part of the PBS Professional ("PBS Pro") software.
 *
 * Open Source License Information:
 *
 * PBS Pro is free software. You can redistribute it and/or modify it under the
 * terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * PBS Pro is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.
 * See the GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Commercial License Information:
 *
 * For a copy of the commercial license terms and conditions,
 * go to: (http://www.pbspro.com/UserArea/agreement.html)
 * or contact the Altair Legal Department.
 *
 * Altair’s dual-license business model allows companies, individuals, and
 * organizations to create proprietary derivative works of PBS Pro and
 * distribute them - whether embedded or bundled with other software -
 * under a commercial license agreement.
 *
 * Use of Altair’s trademarks, including but not limited to "PBS™",
 * "PBS Professional®", and "PBS Pro™" and Altair’s logos is subject to Altair's
 * trademark licensing policies.
 *
 */

/**
 * @file    job_recov_db.c
 *
 * @brief
 * 		job_recov_db.c - This file contains the functions to record a job
 *		data struture to database and to recover it from database.
 *
 *		The data is recorded in the database
 *
 * Functions included are:
 *
 *	job_save_db()         -	save job to database
 *	job_or_resv_save_db() -	save to database (job/reservation)
 *	job_recov_db()        - recover(read) job from database
 *	job_or_resv_recov_db() -	recover(read) job/reservation from database
 *	svr_to_db_job		  -	Load a server job object to a database job object
 *	db_to_svr_job		  - Load data from database job object to a server job object
 *	svr_to_db_resv		  -	Load data from server resv object to a database resv object
 *	db_to_svr_resv		  -	Load data from database resv object to a server resv object
 *	resv_save_db		  -	Save resv to database
 *	resv_recov_db		  - Recover resv from database
 *
 */


#include <pbs_config.h>   /* the master config generated by configure */

#include <stdio.h>
#include <sys/types.h>

#ifndef WIN32
#include <sys/param.h>
#endif

#include "pbs_ifl.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include <unistd.h>
#include "server_limits.h"
#include "list_link.h"
#include "attribute.h"

#ifdef WIN32
#include <sys/stat.h>
#include <io.h>
#include <windows.h>
#include "win.h"
#endif

#include "job.h"
#include "reservation.h"
#include "queue.h"
#include "log.h"
#include "pbs_nodes.h"
#include "svrfunc.h"
#include <memory.h>
#include "libutil.h"
#include "pbs_db.h"


#define MAX_SAVE_TRIES 3

#ifndef PBS_MOM
extern pbs_db_conn_t	*svr_db_conn;
extern char *pbs_server_id;
#endif

#ifdef NAS /* localmod 005 */
/* External Functions Called */
extern int save_attr_db(pbs_db_conn_t *conn, pbs_db_attr_info_t *p_attr_info,
	struct attribute_def *padef, struct attribute *pattr,
	int numattr, int newparent);
extern int recov_attr_db(pbs_db_conn_t *conn,
	void *parent,
	pbs_db_attr_info_t *p_attr_info,
	struct attribute_def *padef,
	struct attribute *pattr,
	int limit,
	int unknown);
#endif /* localmod 005 */

/* global data items */
extern time_t time_now;

#ifndef PBS_MOM

/**
 * @brief
 *		Load a server job object to a database job object
 *
 * @see
 * 		job_save_db
 *
 * @param[in]	pjob - Address of the job in the server
 * @param[out]	dbjob - Address of the database job object
 *
 * @return void
 */
static void
svr_to_db_job(job *pjob, pbs_db_job_info_t *dbjob)
{
	memset(dbjob, 0, sizeof(pbs_db_job_info_t));
	strcpy(dbjob->ji_jobid, pjob->ji_qs.ji_jobid);
	strcpy(dbjob->ji_sv_name, pbs_server_id);
	dbjob->ji_state     = pjob->ji_qs.ji_state;
	dbjob->ji_substate  = pjob->ji_qs.ji_substate;
	dbjob->ji_svrflags  = pjob->ji_qs.ji_svrflags;
	dbjob->ji_numattr   = pjob->ji_qs.ji_numattr;
	dbjob->ji_ordering  = pjob->ji_qs.ji_ordering;
	dbjob->ji_priority  = pjob->ji_qs.ji_priority;
	dbjob->ji_stime     = pjob->ji_qs.ji_stime;
	dbjob->ji_endtBdry  = pjob->ji_qs.ji_endtBdry;
	strcpy(dbjob->ji_queue, pjob->ji_qs.ji_queue);
	strcpy(dbjob->ji_destin, pjob->ji_qs.ji_destin);
	dbjob->ji_un_type   = pjob->ji_qs.ji_un_type;
	if (pjob->ji_qs.ji_un_type == JOB_UNION_TYPE_NEW) {
		dbjob->ji_fromsock  = pjob->ji_qs.ji_un.ji_newt.ji_fromsock;
		dbjob->ji_fromaddr  = pjob->ji_qs.ji_un.ji_newt.ji_fromaddr;
	} else if (pjob->ji_qs.ji_un_type == JOB_UNION_TYPE_EXEC) {
		dbjob->ji_momaddr   = pjob->ji_qs.ji_un.ji_exect.ji_momaddr;
		dbjob->ji_momport   = pjob->ji_qs.ji_un.ji_exect.ji_momport;
		dbjob->ji_exitstat  = pjob->ji_qs.ji_un.ji_exect.ji_exitstat;
	} else if (pjob->ji_qs.ji_un_type == JOB_UNION_TYPE_ROUTE) {
		dbjob->ji_quetime   = pjob->ji_qs.ji_un.ji_routet.ji_quetime;
		dbjob->ji_rteretry  = pjob->ji_qs.ji_un.ji_routet.ji_rteretry;
	} else if (pjob->ji_qs.ji_un_type == JOB_UNION_TYPE_MOM) {
		dbjob->ji_exitstat  = pjob->ji_qs.ji_un.ji_momt.ji_exitstat;
	}

	/* extended portion */
	strcpy(dbjob->ji_4jid, pjob->ji_extended.ji_ext.ji_4jid);
	strcpy(dbjob->ji_4ash, pjob->ji_extended.ji_ext.ji_4ash);
	dbjob->ji_credtype  = pjob->ji_extended.ji_ext.ji_credtype;
	dbjob->ji_qrank = pjob->ji_wattr[(int)JOB_ATR_qrank].at_val.at_long;
}

/**
 * @brief
 *		Load data from database job object to a server job object
 *
 * @see
 * 		job_recov_db
 *
 * @param[out]	pjob - Address of the job in the server
 * @param[in]	dbjob - Address of the database job object
 *
 * @return	void
 */
static void
db_to_svr_job(job *pjob,  pbs_db_job_info_t *dbjob)
{
	/* Variables assigned constant values are not stored in the DB */
	pjob->ji_qs.ji_jsversion = JSVERSION;
	strcpy(pjob->ji_qs.ji_jobid, dbjob->ji_jobid);
	pjob->ji_qs.ji_state = dbjob->ji_state;
	pjob->ji_qs.ji_substate = dbjob->ji_substate;
	pjob->ji_qs.ji_svrflags = dbjob->ji_svrflags;
	pjob->ji_qs.ji_numattr = dbjob->ji_numattr ;
	pjob->ji_qs.ji_ordering = dbjob->ji_ordering;
	pjob->ji_qs.ji_priority = dbjob->ji_priority;
	pjob->ji_qs.ji_stime = dbjob->ji_stime;
	pjob->ji_qs.ji_endtBdry = dbjob->ji_endtBdry;
	strcpy(pjob->ji_qs.ji_queue, dbjob->ji_queue);
	strcpy(pjob->ji_qs.ji_destin, dbjob->ji_destin);
	pjob->ji_qs.ji_fileprefix[0] = 0;
	pjob->ji_qs.ji_un_type = dbjob->ji_un_type;
	if (pjob->ji_qs.ji_un_type == JOB_UNION_TYPE_NEW) {
		pjob->ji_qs.ji_un.ji_newt.ji_fromsock = dbjob->ji_fromsock;
		pjob->ji_qs.ji_un.ji_newt.ji_fromaddr = dbjob->ji_fromaddr;
		pjob->ji_qs.ji_un.ji_newt.ji_scriptsz = 0;
	} else if (pjob->ji_qs.ji_un_type == JOB_UNION_TYPE_EXEC) {
		pjob->ji_qs.ji_un.ji_exect.ji_momaddr = dbjob->ji_momaddr;
		pjob->ji_qs.ji_un.ji_exect.ji_momport = dbjob->ji_momport;
		pjob->ji_qs.ji_un.ji_exect.ji_exitstat = dbjob->ji_exitstat;
	} else if (pjob->ji_qs.ji_un_type == JOB_UNION_TYPE_ROUTE) {
		pjob->ji_qs.ji_un.ji_routet.ji_quetime = dbjob->ji_quetime;
		pjob->ji_qs.ji_un.ji_routet.ji_rteretry = dbjob->ji_rteretry;
	} else if (pjob->ji_qs.ji_un_type == JOB_UNION_TYPE_MOM) {
		pjob->ji_qs.ji_un.ji_momt.ji_svraddr = 0;
		pjob->ji_qs.ji_un.ji_momt.ji_exitstat = dbjob->ji_exitstat;
		pjob->ji_qs.ji_un.ji_momt.ji_exuid = 0;
		pjob->ji_qs.ji_un.ji_momt.ji_exgid = 0;
	}

	/* extended portion */
#if defined(__sgi)
	pjob->ji_extended.ji_ext.ji_jid = 0;
	pjob->ji_extended.ji_ext.ji_ash = 0;
#else
	strcpy(pjob->ji_extended.ji_ext.ji_4jid, dbjob->ji_4jid);
	strcpy(pjob->ji_extended.ji_ext.ji_4ash, dbjob->ji_4ash);
#endif
	pjob->ji_extended.ji_ext.ji_credtype = dbjob->ji_credtype;
}

/**
 * @brief
 *		Load data from server resv object to a database resv object
 *
 * @see
 * 		resv_save_db
 *
 * @param[in]	presv - Address of the resv in the server
 * @param[in]	dbresv - Address of the database resv object
 *
 * @return	void
 */
static void
svr_to_db_resv(resc_resv *presv,  pbs_db_resv_info_t *dbresv)
{
	memset(dbresv, 0, sizeof(pbs_db_resv_info_t));
	strcpy(dbresv->ri_resvid, presv->ri_qs.ri_resvID);
	strcpy(dbresv->ri_sv_name, pbs_server_id);
	strcpy(dbresv->ri_queue, presv->ri_qs.ri_queue);
	dbresv->ri_duration = presv->ri_qs.ri_duration;
	dbresv->ri_etime = presv->ri_qs.ri_etime;
	dbresv->ri_un_type = presv->ri_qs.ri_un_type;
	if (dbresv->ri_un_type == RESV_UNION_TYPE_NEW) {
		dbresv->ri_fromaddr = presv->ri_qs.ri_un.ri_newt.ri_fromaddr;
		dbresv->ri_fromsock = presv->ri_qs.ri_un.ri_newt.ri_fromsock;
	}
	dbresv->ri_numattr = presv->ri_qs.ri_numattr;
	dbresv->ri_resvTag = presv->ri_qs.ri_resvTag;
	dbresv->ri_state = presv->ri_qs.ri_state;
	dbresv->ri_stime = presv->ri_qs.ri_stime;
	dbresv->ri_substate = presv->ri_qs.ri_substate;
	dbresv->ri_svrflags = presv->ri_qs.ri_svrflags;
	dbresv->ri_tactive = presv->ri_qs.ri_tactive;
	dbresv->ri_type = presv->ri_qs.ri_type;
}

/**
 * @brief
 *		Load data from database resv object to a server resv object
 *
 * @see
 * 		resv_recov_db
 *
 * @param[in]	presv - Address of the resv in the server
 * @param[in]	dbresv - Address of the database resv object
 *
 * @return	void
 */
static void
db_to_svr_resv(resc_resv *presv, pbs_db_resv_info_t *pdresv)
{
	strcpy(presv->ri_qs.ri_resvID, pdresv->ri_resvid);
	strcpy(presv->ri_qs.ri_queue, pdresv->ri_queue);
	presv->ri_qs.ri_duration = pdresv->ri_duration;
	presv->ri_qs.ri_etime = pdresv->ri_etime;
	presv->ri_qs.ri_un_type = pdresv->ri_un_type;
	if (pdresv->ri_un_type == RESV_UNION_TYPE_NEW) {
		presv->ri_qs.ri_un.ri_newt.ri_fromaddr = pdresv->ri_fromaddr;
		presv->ri_qs.ri_un.ri_newt.ri_fromsock = pdresv->ri_fromsock;
	}
	presv->ri_qs.ri_numattr = pdresv->ri_numattr;
	presv->ri_qs.ri_resvTag = pdresv->ri_resvTag;
	presv->ri_qs.ri_state = pdresv->ri_state;
	presv->ri_qs.ri_stime = pdresv->ri_stime;
	presv->ri_qs.ri_substate = pdresv->ri_substate;
	presv->ri_qs.ri_svrflags = pdresv->ri_svrflags;
	presv->ri_qs.ri_tactive = pdresv->ri_tactive;
	presv->ri_qs.ri_type = pdresv->ri_type;
}

/**
 * @brief
 *		Save job to database
 *
 * @param[in]	pjob - The job to save
 * @param[in]   updatetype:
 *				SAVEJOB_QUICK - Quick update, save only quick save area
 *				SAVEJOB_FULL  - Update along with attributes
 *				SAVEJOB_NEW   - Create new job in database (insert)
 *				SAVEJOB_FULLFORCE - Same as SAVEJOB_FULL
 *
 * @return      Error code
 * @retval	 0 - Success
 * @retval	-1 - Failure
 *
 */
int
job_save_db(job *pjob, int updatetype)
{
	pbs_db_attr_info_t attr_info;
	pbs_db_job_info_t dbjob;
	pbs_db_obj_info_t obj;
	pbs_db_conn_t *conn = svr_db_conn;

	/*
	 * if job has new_job flag set, then updatetype better be SAVEJOB_NEW
	 * If not, ignore and return success
	 * This is to avoid saving the job at several places even before the job
	 * is initially created in the database in req_commit
	 * We reset the flag ji_newjob in req_commit (server only)
	 * after we have successfully created the job in the database
	 */
	if (pjob->ji_newjob == 1 && updatetype != SAVEJOB_NEW)
		return (0);

	/* if ji_modified is set, ie an attribute changed, then update mtime */
	if (pjob->ji_modified) {
		pjob->ji_wattr[JOB_ATR_mtime].at_val.at_long = time_now;
		pjob->ji_wattr[JOB_ATR_mtime].at_flags |= ATR_VFLAG_MODCACHE;
	}

	if (pjob->ji_qs.ji_jsversion != JSVERSION) {
		/* version of job structure changed, force full write */
		pjob->ji_qs.ji_jsversion = JSVERSION;
		updatetype = SAVEJOB_FULLFORCE;
	}

	svr_to_db_job(pjob, &dbjob);
	obj.pbs_db_obj_type = PBS_DB_JOB;
	obj.pbs_db_un.pbs_db_job = &dbjob;

	if (updatetype == SAVEJOB_QUICK) {
		/* update database */
		if (pbs_db_update_obj(conn, &obj) != 0)
			goto db_err;
	} else {

		/*
		 * write the whole structure to the database.
		 * The update has five parts:
		 * (1) the job structure,
		 * (2) the extended area,
		 * (3) if a Array Job, the index tracking table
		 * (4) the attributes in the "encoded "external form, and last
		 * (5) the dependency list.
		 */
		if (pbs_db_begin_trx(conn, 0, 0) !=0)
			goto db_err;

		attr_info.parent_id = pjob->ji_qs.ji_jobid;
		attr_info.parent_obj_type = PARENT_TYPE_JOB; /* job attr */

		if (updatetype == SAVEJOB_NEW) {
			/* do database inserts for job and job_attr */
			if (pbs_db_insert_obj(conn, &obj) != 0)
				goto db_err;

			if (save_attr_db(conn, &attr_info, job_attr_def,
				pjob->ji_wattr,
				(int)JOB_ATR_LAST, 1) != 0)
				goto db_err;

		} else {
			/* do database updates for job and job_attr */
			if (pbs_db_update_obj(conn, &obj) != 0)
				goto db_err;

			if (save_attr_db(conn, &attr_info, job_attr_def,
				pjob->ji_wattr,
				(int)JOB_ATR_LAST, 0) != 0)
				goto db_err;
		}
		if (pbs_db_end_trx(conn, PBS_DB_COMMIT) != 0)
			goto db_err;

		pjob->ji_modified = 0;
		pjob->ji_newjob = 0; /* reset dontsave - job is now saved */
	}
	return (0);
db_err:
	sprintf(log_buffer, "Failed to save job %s ", pjob->ji_qs.ji_jobid);
	if (conn->conn_db_err != NULL)
		strncat(log_buffer, conn->conn_db_err, LOG_BUF_SIZE - strlen(log_buffer) - 1);
	log_err(-1, "job_save", log_buffer);
	(void) pbs_db_end_trx(conn, PBS_DB_ROLLBACK);
	if (updatetype == SAVEJOB_NEW) {
		/* database save failed for new job, stay up, */
		return (-1); /* return without calling panic_stop_db */
	}
	panic_stop_db(log_buffer);
	return (-1);
}

/**
 * @brief
 *		Save resv to database
 *
 * @see
 * 		job_or_resv_save_db, svr_migrate_data_from_fs
 *
 * @param[in]	presv - The resv to save
 * @param[in]   updatetype:
 *				SAVERESV_QUICK - Quick update without attributes
 *				SAVERESV_FULL  - Full update with attributes
 *				SAVERESV_NEW   - New resv, insert into database
 *
 * @return      Error code
 * @retval	 0 - Success
 * @retval	-1 - Failure
 *
 */
int
resv_save_db(resc_resv *presv, int updatetype)
{
	pbs_db_attr_info_t attr_info;
	pbs_db_resv_info_t dbresv;
	pbs_db_obj_info_t obj;
	pbs_db_conn_t *conn = svr_db_conn;

	/* if ji_modified is set, ie an attribute changed, then update mtime */
	if (presv->ri_modified) {
		presv->ri_wattr[RESV_ATR_mtime].at_val.at_long = time_now;
		presv->ri_wattr[RESV_ATR_mtime].at_val.at_long |= ATR_VFLAG_MODCACHE;
	}

	svr_to_db_resv(presv, &dbresv);
	obj.pbs_db_obj_type = PBS_DB_RESV;
	obj.pbs_db_un.pbs_db_resv = &dbresv;

	if (updatetype == SAVERESV_QUICK) {
		/* update database */
		if (pbs_db_update_obj(conn, &obj) != 0)
			goto db_err;
	} else {

		/*
		 * write the whole structure to database.
		 * The file is updated in four parts:
		 * (1) the resv structure,
		 * (2) the extended area,
		 * (3) the attributes in the "encoded "external form, and last
		 * (4) the dependency list.
		 */
		if (pbs_db_begin_trx(conn, 0, 0) !=0)
			goto db_err;

		attr_info.parent_id = presv->ri_qs.ri_resvID;
		attr_info.parent_obj_type = PARENT_TYPE_RESV; /* resv attr */

		if (updatetype == SAVERESV_NEW) {
			/* do database inserts for job and job_attr */
			if (pbs_db_insert_obj(conn, &obj) != 0)
				goto db_err;
			if (save_attr_db(conn, &attr_info, resv_attr_def,
				presv->ri_wattr, (int)RESV_ATR_LAST, 1) != 0)
				goto db_err;
		} else {
			/* do database updates for job and job_attr */
			if (pbs_db_update_obj(conn, &obj) != 0)
				goto db_err;
			if (save_attr_db(conn, &attr_info, resv_attr_def,
				presv->ri_wattr, (int)RESV_ATR_LAST, 0) != 0)
				goto db_err;
		}
		if (pbs_db_end_trx(conn, PBS_DB_COMMIT) != 0)
			goto db_err;

		presv->ri_modified = 0;
	}
	return (0);
db_err:
	sprintf(log_buffer, "Failed to save resv %s ", presv->ri_qs.ri_resvID);
	if (conn->conn_db_err != NULL)
		strncat(log_buffer, conn->conn_db_err, LOG_BUF_SIZE - strlen(log_buffer) - 1);
	log_err(-1, "resv_save", log_buffer);
	(void) pbs_db_end_trx(conn, PBS_DB_ROLLBACK);
	if (updatetype == SAVERESV_NEW) {
		/* database save failed for new resv, stay up, */
		return (-1); /* return without calling panic_stop_db */
	}
	panic_stop_db(log_buffer);
	return (-1);
}


/**
 * @brief
 *		Recover job from database
 *
 * @param[in]	jid - Job id of job to recover
 *
 * @return      The recovered job
 * @retval	 NULL - Failure
 * @retval	!NULL - Success, pointer to job structure recovered
 *
 */
job *
job_recov_db(char *jid)
{
	job		*pj;
	pbs_db_job_info_t dbjob;
	pbs_db_attr_info_t attr_info;
	pbs_db_obj_info_t obj;
	pbs_db_conn_t *conn = svr_db_conn;

	pj = job_alloc();	/* allocate & initialize job structure space */
	if (pj == NULL) {
		return NULL;
	}

	if (pbs_db_begin_trx(conn, 0, 0) !=0)
		goto db_err;

	strcpy(dbjob.ji_jobid, jid);
	obj.pbs_db_obj_type = PBS_DB_JOB;
	obj.pbs_db_un.pbs_db_job = &dbjob;

	/* read in job fixed sub-structure */
	if (pbs_db_load_obj(conn, &obj) != 0)
		goto db_err;

	db_to_svr_job(pj, &dbjob);

	attr_info.parent_id = jid;
	attr_info.parent_obj_type = PARENT_TYPE_JOB; /* job attr */

	/* read in working attributes */
	if (recov_attr_db(conn, pj, &attr_info, job_attr_def, pj->ji_wattr,
		(int)JOB_ATR_LAST, (int)JOB_ATR_UNKN) != 0) {
		sprintf(log_buffer, "error loading attributes for %s",
			jid);
		log_err(-1, "job_recov", log_buffer);
		goto db_err;
	}
	if (pbs_db_end_trx(conn, PBS_DB_COMMIT) != 0)
		goto db_err;

	return (pj);
db_err:
	if (pj)
		job_free(pj);

	sprintf(log_buffer, "Failed to recover job %s", jid);
	log_err(-1, "job_recov", log_buffer);

	(void) pbs_db_end_trx(conn, PBS_DB_ROLLBACK);
	return NULL;
}

/**
 * @brief
 *		Recover resv from database
 *
 * @see
 * 		job_or_resv_recov_db
 *
 * @param[in]	resvid - Resv id to recover
 *
 * @return      The recovered reservation
 * @retval	 NULL - Failure
 * @retval	!NULL - Success, pointer to resv structure recovered
 *
 */
resc_resv *
resv_recov_db(char *resvid)
{
	resc_resv               *presv;
	pbs_db_resv_info_t	dbresv;
	pbs_db_attr_info_t      attr_info;
	pbs_db_obj_info_t       obj;
	pbs_db_conn_t *conn = svr_db_conn;

	presv = resc_resv_alloc();
	if (presv == NULL) {
		return NULL;
	}

	if (pbs_db_begin_trx(conn, 0, 0) !=0)
		goto db_err;

	strcpy(dbresv.ri_resvid, resvid);
	obj.pbs_db_obj_type = PBS_DB_RESV;
	obj.pbs_db_un.pbs_db_resv = &dbresv;

	/* read in job fixed sub-structure */
	if (pbs_db_load_obj(conn, &obj) != 0)
		goto db_err;

	db_to_svr_resv(presv, &dbresv);

	attr_info.parent_id = resvid;
	attr_info.parent_obj_type = PARENT_TYPE_RESV; /* resv attr */

	/* read in working attributes */
	if (recov_attr_db(conn, presv, &attr_info, resv_attr_def,
		presv->ri_wattr, (int)RESV_ATR_LAST, (int)RESV_ATR_UNKN) != 0) {
		sprintf(log_buffer, "error loading attributes portion for %s",
			resvid);
		log_err(-1, "resv_recov", log_buffer);
		goto db_err;
	}

	if (pbs_db_end_trx(conn, PBS_DB_COMMIT) != 0)
		goto db_err;

	return (presv);
db_err:
	if (presv)
		resv_free(presv);

	sprintf(log_buffer, "Failed to recover resv %s", resvid);
	log_err(-1, "resv_recov", log_buffer);

	(void) pbs_db_end_trx(conn, PBS_DB_ROLLBACK);
	return NULL;
}


/**
 * @brief
 *		Save job or reservation to database
 *
 * @param[in]	pobj - Address of job or reservation
 * @param[in]   updatetype - Type of update, see descriptions of job_save_db
 *			     and resv_save_db
 *				0=quick, 1=full existing, 2=full new
 * @param[in]	objtype	- Type of the object, job or resv
 *			JOB_OBJECT, RESC_RESV_OBJECT, RESV_JOB_OBJECT
 *
 * @return      Error code
 * @retval	 0 - Success
 * @retval	-1 - Failure
 *
 */
int
job_or_resv_save_db(void *pobj, int updatetype, int objtype)
{
	int rc = 0;

	if (objtype == RESC_RESV_OBJECT || objtype == RESV_JOB_OBJECT) {
		resc_resv *presv;
		presv = (resc_resv *) pobj;

		/* call resv_save */
		rc = resv_save_db(presv, updatetype);
		if (rc)
			return (rc);
	} else if (objtype == JOB_OBJECT) {
		job *pj = (job *) pobj;
		if (pj->ji_resvp) {
			if (updatetype == SAVEJOB_QUICK)
				rc = job_or_resv_save((void *) pj->ji_resvp,
					SAVERESV_QUICK,
					RESC_RESV_OBJECT);
			else if ((updatetype == SAVEJOB_FULL) ||
				(updatetype == SAVEJOB_FULLFORCE) ||
				(updatetype == SAVEJOB_NEW))
				rc = job_or_resv_save((void *) pj->ji_resvp,
					SAVERESV_FULL,
					RESC_RESV_OBJECT);
			if (rc)
				return (rc);
		}
		rc = job_save_db(pj, updatetype);
		if (rc)
			return (rc);
	} else {
		/*Don't expect to get here; incorrect object type*/
		return (-1);
	}
	return (0);
}

/**
 * @brief
 *		Recover job or reservation from database
 *
 * @see
 * 		pbsd_init
 *
 * @param[in]	id	- Id of the reservation/job to recover
 * @param[in]	objtype	- Type of the object, job or resv
 *
 * @return       The recovered job or resv
 * @retval	  NULL - Failure
 * @retval	 !NULL - Success - job/resv object returned
 *
 */
void*
job_or_resv_recov_db(char *id, int objtype)
{
	if (objtype == RESC_RESV_OBJECT) {
		return (resv_recov_db(id));
	} else {
		return (job_recov_db(id));
	}
}
#endif
