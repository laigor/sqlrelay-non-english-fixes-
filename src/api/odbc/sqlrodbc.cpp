// Copyright (c) 2007  David Muse
// See the file COPYING for more information

#include <config.h>

#include <sqlrelay/sqlrclient.h>
#include <rudiments/bytestring.h>
#include <rudiments/linkedlist.h>
#include <rudiments/parameterstring.h>
#include <rudiments/charstring.h>
#include <rudiments/character.h>
#include <rudiments/environment.h>
#include <rudiments/stdio.h>
#include <rudiments/error.h>

#define DEBUG_MESSAGES 1
//#define DEBUG_TO_FILE 1
#include <debugprint.h>

// windows needs this (don't include for __CYGWIN__ though)
#ifdef _WIN32
	#include <windows.h>
#endif

#include <sql.h>
#include <sqlext.h>
#include <odbcinst.h>

#include <parsedatetime.h>

#ifndef SQL_NULL_DESC
	#define SQL_NULL_DESC 0
#endif

#ifdef _WIN64
	#undef SQLCOLATTRIBUTE_SQLLEN
	#define SQLCOLATTRIBUTE_SQLLEN 1
#endif

#ifdef SQLCOLATTRIBUTE_SQLLEN
typedef SQLLEN * NUMERICATTRIBUTETYPE;
#else
typedef SQLPOINTER NUMERICATTRIBUTETYPE;
#endif

#ifndef HAVE_SQLROWSETSIZE
typedef SQLULEN SQLROWSETSIZE;
#endif

#define ODBC_INI "odbc.ini"

extern "C" {

static	uint16_t	stmtid=0;

struct CONN;

struct ENV {
	SQLINTEGER			odbcversion;
	singlylinkedlist<CONN *>	connlist;
	char				*error;
	int64_t				errn;
	const char			*sqlstate;
};

struct STMT;

struct CONN {
	sqlrconnection			*con;
	ENV				*env;
	singlylinkedlist<STMT *>	stmtlist;
	char				*error;
	int64_t				errn;
	const char			*sqlstate;
	char				dsn[1024];
	char				server[1024];
	uint16_t			port;
	char				socket[1024];
	char				user[1024];
	char				password[1024];
	int32_t				retrytime;
	int32_t				tries;
	bool				debug;
};

struct rowdesc {
	STMT	*stmt;
};

struct paramdesc {
	STMT	*stmt;
};

struct FIELD {
	SQLSMALLINT	targettype;
	SQLPOINTER	targetvalue;
	SQLLEN		bufferlength;
	SQLLEN		*strlen_or_ind;
};

struct outputbind {
	SQLUSMALLINT	parameternumber;
	SQLSMALLINT	valuetype;
	SQLULEN		lengthprecision;
	SQLSMALLINT	parameterscale;
	SQLPOINTER	parametervalue;
	SQLLEN		bufferlength;
	SQLLEN		*strlen_or_ind;
};

struct STMT {
	sqlrcursor				*cur;
	uint64_t				currentfetchrow;
	uint64_t				currentstartrow;
	uint64_t				currentgetdatarow;
	CONN					*conn;
	char					*name;
	char					*error;
	int64_t					errn;
	const char				*sqlstate;
	dictionary<int32_t, FIELD *>		fieldlist;
	rowdesc					*approwdesc;
	paramdesc				*appparamdesc;
	rowdesc					*improwdesc;
	paramdesc				*impparamdesc;
	dictionary<int32_t, char *>		inputbindstrings;
	dictionary<int32_t,outputbind *>	outputbinds;
	SQLROWSETSIZE				*rowsfetchedptr;
	SQLUSMALLINT				*rowstatusptr;
	bool					executed;
	bool					executedbynumresultcols;
	SQLRETURN				executedbynumresultcolsresult;
	SQLULEN					rowbindtype;
};

static SQLRETURN SQLR_SQLAllocHandle(SQLSMALLINT handletype,
					SQLHANDLE inputhandle,
					SQLHANDLE *outputhandle);

SQLRETURN SQL_API SQLAllocConnect(SQLHENV environmenthandle,
					SQLHDBC *connectionhandle) {
	debugFunction();
	return SQLR_SQLAllocHandle(SQL_HANDLE_DBC,
				(SQLHANDLE)environmenthandle,
				(SQLHANDLE *)connectionhandle);
}

SQLRETURN SQL_API SQLAllocEnv(SQLHENV *environmenthandle) {
	debugFunction();
	return SQLR_SQLAllocHandle(SQL_HANDLE_ENV,NULL,
				(SQLHANDLE *)environmenthandle);
}

static void SQLR_ENVSetError(ENV *env, const char *error,
				int64_t errn, const char *sqlstate) {
	debugFunction();

	// set the error, convert NULL's to empty strings,
	// some apps have trouble with NULLS
	delete[] env->error;
	env->error=charstring::duplicate((error)?error:"");
	env->errn=errn;
	env->sqlstate=(sqlstate)?sqlstate:"";
	debugPrintf("  error: %s\n",env->error);
	debugPrintf("  errn: %lld\n",env->errn);
	debugPrintf("  sqlstate: %s\n",env->sqlstate);
}

static void SQLR_ENVClearError(ENV *env) {
	debugFunction();
	SQLR_ENVSetError(env,NULL,0,"00000");
}

static void SQLR_CONNSetError(CONN *conn, const char *error,
				int64_t errn, const char *sqlstate) {
	debugFunction();

	// set the error, convert NULL's to empty strings,
	// some apps have trouble with NULLS
	delete[] conn->error;
	conn->error=charstring::duplicate((error)?error:"");
	conn->errn=errn;
	conn->sqlstate=(sqlstate)?sqlstate:"";
	debugPrintf("  error: %s\n",conn->error);
	debugPrintf("  errn: %lld\n",conn->errn);
	debugPrintf("  sqlstate: %s\n",conn->sqlstate);
}

static void SQLR_CONNClearError(CONN *conn) {
	debugFunction();
	SQLR_CONNSetError(conn,NULL,0,"00000");
}

static void SQLR_STMTSetError(STMT *stmt, const char *error,
				int64_t errn, const char *sqlstate) {
	debugFunction();

	// set the error, convert NULL's to empty strings,
	// some apps have trouble with NULLS
	delete[] stmt->error;
	stmt->error=charstring::duplicate((error)?error:"");
	stmt->errn=errn;
	stmt->sqlstate=(sqlstate)?sqlstate:"";
	debugPrintf("  error: %s\n",stmt->error);
	debugPrintf("  errn: %lld\n",stmt->errn);
	debugPrintf("  sqlstate: %s\n",stmt->sqlstate);
}

static void SQLR_STMTClearError(STMT *stmt) {
	debugFunction();
	SQLR_STMTSetError(stmt,NULL,0,"00000");
}

static SQLRETURN SQLR_SQLAllocHandle(SQLSMALLINT handletype,
					SQLHANDLE inputhandle,
					SQLHANDLE *outputhandle) {
	debugFunction();

	switch (handletype) {
		case SQL_HANDLE_ENV:
			{
			debugPrintf("  handletype: SQL_HANDLE_ENV\n");
			if (outputhandle) {
				ENV	*env=new ENV;
				env->odbcversion=0;
				*outputhandle=(SQLHANDLE)env;
				env->error=NULL;
				SQLR_ENVClearError(env);
			}
			return SQL_SUCCESS;
			}
		case SQL_HANDLE_DBC:
			{
			debugPrintf("  handletype: SQL_HANDLE_DBC\n");
			ENV	*env=(ENV *)inputhandle;
			if (inputhandle==SQL_NULL_HENV || !env) {
				debugPrintf("  NULL env handle\n");
				if (outputhandle) {
					*outputhandle=SQL_NULL_HENV;
				}
				return SQL_INVALID_HANDLE;
			}
			if (outputhandle) {
				CONN	*conn=new CONN;
				conn->con=NULL;
				*outputhandle=(SQLHANDLE)conn;
				conn->error=NULL;
				SQLR_CONNClearError(conn);
				env->connlist.append(conn);
				conn->env=env;
			}
			return SQL_SUCCESS;
			}
		case SQL_HANDLE_STMT:
			{
			debugPrintf("  handletype: SQL_HANDLE_STMT\n");
			CONN	*conn=(CONN *)inputhandle;
			if (inputhandle==SQL_NULL_HANDLE ||
						!conn || !conn->con) {
				debugPrintf("  NULL conn handle\n");
				*outputhandle=SQL_NULL_HENV;
				return SQL_INVALID_HANDLE;
			}
			if (outputhandle) {
				STMT	*stmt=new STMT;
				stmt->cur=new sqlrcursor(conn->con,true);
				*outputhandle=(SQLHANDLE)stmt;
				stmt->currentfetchrow=0;
				stmt->currentstartrow=0;
				stmt->currentgetdatarow=0;
				stmt->conn=conn;
				conn->stmtlist.append(stmt);
				stmt->name=NULL;
				stmt->error=NULL;
				SQLR_STMTClearError(stmt);
				stmt->improwdesc=new rowdesc;
				stmt->improwdesc->stmt=stmt;
				stmt->impparamdesc=new paramdesc;
				stmt->impparamdesc->stmt=stmt;
				stmt->approwdesc=stmt->improwdesc;
				stmt->appparamdesc=stmt->impparamdesc;
				stmt->rowsfetchedptr=NULL;
				stmt->rowstatusptr=NULL;
				stmt->executed=false;
				stmt->executedbynumresultcols=false;
				stmt->executedbynumresultcolsresult=SQL_SUCCESS;
				stmt->rowbindtype=SQL_BIND_BY_COLUMN;
			}
			return SQL_SUCCESS;
			}
		case SQL_HANDLE_DESC:
			debugPrintf("  handletype: SQL_HANDLE_DESC\n");
			// FIXME: no idea what to do here
			return SQL_ERROR;
		default:
			debugPrintf("  invalid handletype: %d\n",handletype);
			break;
	}
	return SQL_ERROR;
}

SQLRETURN SQL_API SQLAllocHandleStd(SQLSMALLINT handletype,
					SQLHANDLE inputhandle,
					SQLHANDLE *outputhandle) {
	debugFunction();
	if (handletype==SQL_HANDLE_ENV) {
		#if (ODBCVER >= 0x0300)
		((ENV *)inputhandle)->odbcversion=SQL_OV_ODBC3;
		#endif
	}
	return SQLR_SQLAllocHandle(handletype,inputhandle,outputhandle);
}

SQLRETURN SQL_API SQLAllocHandle(SQLSMALLINT handletype,
					SQLHANDLE inputhandle,
					SQLHANDLE *outputhandle) {
	debugFunction();
	return SQLR_SQLAllocHandle(handletype,inputhandle,outputhandle);
}

SQLRETURN SQL_API SQLAllocStmt(SQLHDBC connectionhandle,
					SQLHSTMT *statementhandle) {
	debugFunction();
	return SQLR_SQLAllocHandle(SQL_HANDLE_STMT,
				(SQLHANDLE)connectionhandle,
				(SQLHANDLE *)statementhandle);
}

static SQLLEN SQLR_GetCColumnTypeSize(SQLSMALLINT targettype) {
	switch (targettype) {
		case SQL_C_CHAR:
		case SQL_C_BIT:
			return sizeof(SQLCHAR);
		case SQL_C_SHORT:
		case SQL_C_USHORT:
		case SQL_C_SSHORT:
			return sizeof(SQLSMALLINT);
		case SQL_C_TINYINT:
		case SQL_C_UTINYINT:
		case SQL_C_STINYINT:
			return sizeof(SQLCHAR);
		case SQL_C_LONG:
		case SQL_C_ULONG:
		case SQL_C_SLONG:
			return sizeof(SQLINTEGER);
		case SQL_C_SBIGINT:
		case SQL_C_UBIGINT:
			return sizeof(SQLBIGINT);
		case SQL_C_FLOAT:
			return sizeof(SQLREAL);
		case SQL_C_DOUBLE:
			return sizeof(SQLDOUBLE);
		case SQL_C_NUMERIC:
			return sizeof(SQL_NUMERIC_STRUCT);
		case SQL_C_DATE:
		case SQL_C_TYPE_DATE:
			return sizeof(DATE_STRUCT);
		case SQL_C_TIME:
		case SQL_C_TYPE_TIME:
			return sizeof(TIME_STRUCT);
		case SQL_C_TIMESTAMP:
		case SQL_C_TYPE_TIMESTAMP:
			return sizeof(TIMESTAMP_STRUCT);
		case SQL_C_INTERVAL_YEAR:
		case SQL_C_INTERVAL_MONTH:
		case SQL_C_INTERVAL_DAY:
		case SQL_C_INTERVAL_HOUR:
		case SQL_C_INTERVAL_MINUTE:
		case SQL_C_INTERVAL_SECOND:
		case SQL_C_INTERVAL_YEAR_TO_MONTH:
		case SQL_C_INTERVAL_DAY_TO_HOUR:
		case SQL_C_INTERVAL_DAY_TO_MINUTE:
		case SQL_C_INTERVAL_DAY_TO_SECOND:
		case SQL_C_INTERVAL_HOUR_TO_MINUTE:
		case SQL_C_INTERVAL_HOUR_TO_SECOND:
		case SQL_C_INTERVAL_MINUTE_TO_SECOND:
			return sizeof(SQL_INTERVAL_STRUCT);
		case SQL_C_GUID:
			return 36;
		default:
			return 0;
	}
}

#ifdef DEBUG_MESSAGES
static const char *SQLR_GetCColumnTypeName(SQLSMALLINT targettype) {
	switch (targettype) {
		case SQL_C_CHAR:
			return "SQL_C_CHAR";
		case SQL_C_BIT:
			return "SQL_C_BIT";
		case SQL_C_SHORT:
			return "SQL_C_SHORT";
		case SQL_C_USHORT:
			return "SQL_C_USHORT";
		case SQL_C_SSHORT:
			return "SQL_C_SSHORT";
		case SQL_C_TINYINT:
			return "SQL_C_TINYINT";
		case SQL_C_UTINYINT:
			return "SQL_C_UTINYINT";
		case SQL_C_STINYINT:
			return "SQL_C_STINYINT";
		case SQL_C_LONG:
			return "SQL_C_LONG";
		case SQL_C_ULONG:
			return "SQL_C_ULONG";
		case SQL_C_SLONG:
			return "SQL_C_SLONG";
		case SQL_C_SBIGINT:
			return "SQL_C_SBIGINT";
		case SQL_C_UBIGINT:
			return "SQL_C_UBIGINT";
		case SQL_C_FLOAT:
			return "SQL_C_FLOAT";
		case SQL_C_DOUBLE:
			return "SQL_C_DOUBLE";
		case SQL_C_NUMERIC:
			return "SQL_C_NUMERIC";
		case SQL_C_DATE:
			return "SQL_C_DATE";
		case SQL_C_TYPE_DATE:
			return "SQL_C_TYPE_DATE";
		case SQL_C_TIME:
			return "SQL_C_TIME";
		case SQL_C_TYPE_TIME:
			return "SQL_C_TYPE_TIME";
		case SQL_C_TIMESTAMP:
			return "SQL_C_TIMESTAMP";
		case SQL_C_TYPE_TIMESTAMP:
			return "SQL_C_TYPE_TIMESTAMP";
		case SQL_C_INTERVAL_YEAR:
			return "SQL_C_INTERVAL_YEAR";
		case SQL_C_INTERVAL_MONTH:
			return "SQL_C_INTERVAL_MONTH";
		case SQL_C_INTERVAL_DAY:
			return "SQL_C_INTERVAL_DAY";
		case SQL_C_INTERVAL_HOUR:
			return "SQL_C_INTERVAL_HOUR";
		case SQL_C_INTERVAL_MINUTE:
			return "SQL_C_INTERVAL_MINUTE";
		case SQL_C_INTERVAL_SECOND:
			return "SQL_C_INTERVAL_SECOND";
		case SQL_C_INTERVAL_YEAR_TO_MONTH:
			return "SQL_C_INTERVAL_YEAR_TO_MONTH";
		case SQL_C_INTERVAL_DAY_TO_HOUR:
			return "SQL_C_INTERVAL_DAY_TO_HOUR";
		case SQL_C_INTERVAL_DAY_TO_MINUTE:
			return "SQL_C_INTERVAL_DAY_TO_MINUTE";
		case SQL_C_INTERVAL_DAY_TO_SECOND:
			return "SQL_C_INTERVAL_DAY_TO_SECOND";
		case SQL_C_INTERVAL_HOUR_TO_MINUTE:
			return "SQL_C_INTERVAL_HOUR_TO_MINUTE";
		case SQL_C_INTERVAL_HOUR_TO_SECOND:
			return "SQL_C_INTERVAL_HOUR_TO_SECOND";
		case SQL_C_INTERVAL_MINUTE_TO_SECOND:
			return "SQL_C_INTERVAL_MINUTE_TO_SECOND";
		case SQL_C_GUID:
			return "SQL_C_GUID";
		default:
			return "unknown";
	}
}
#endif

SQLRETURN SQL_API SQLBindCol(SQLHSTMT statementhandle,
					SQLUSMALLINT columnnumber,
					SQLSMALLINT targettype,
					SQLPOINTER targetvalue,
					SQLLEN bufferlength,
					SQLLEN *strlen_or_ind) {
	debugFunction();
	debugPrintf("  columnnumber: %d\n",(int)columnnumber);
	debugPrintf("  targettype  : %s\n",SQLR_GetCColumnTypeName(targettype));
	debugPrintf("  bufferlength (supplied) : %lld\n",(uint64_t)bufferlength);

	STMT	*stmt=(STMT *)statementhandle;
	if (statementhandle==SQL_NULL_HSTMT || !stmt || !stmt->cur) {
		debugPrintf("  NULL stmt handle\n");
		return SQL_INVALID_HANDLE;
	}

	if (columnnumber<1) {
		debugPrintf("  invalid column: %d\n",columnnumber);
		SQLR_STMTSetError(stmt,NULL,0,"07009");
		return SQL_ERROR;
	}

	FIELD	*field=new FIELD;
	field->targettype=targettype;
	field->targetvalue=targetvalue;
	if (bufferlength) {
		field->bufferlength=bufferlength;
	} else {
		field->bufferlength=SQLR_GetCColumnTypeSize(targettype);
	}
	field->strlen_or_ind=strlen_or_ind;

	stmt->fieldlist.setValue(columnnumber-1,field);

	debugPrintf("  bufferlength (from type): %lld\n",
				(uint64_t)field->bufferlength);

	return SQL_SUCCESS;
}

static SQLRETURN SQLR_SQLBindParameter(SQLHSTMT statementhandle,
					SQLUSMALLINT parameternumber,
					SQLSMALLINT inputoutputtype,
					SQLSMALLINT valuetype,
					SQLSMALLINT parametertype,
					SQLULEN lengthprecision,
					SQLSMALLINT parameterscale,
					SQLPOINTER parametervalue,
					SQLLEN bufferlength,
					SQLLEN *strlen_or_ind);

SQLRETURN SQL_API SQLBindParam(SQLHSTMT statementhandle,
					SQLUSMALLINT parameternumber,
					SQLSMALLINT valuetype,
					SQLSMALLINT parametertype,
					SQLULEN lengthprecision,
					SQLSMALLINT parameterscale,
					SQLPOINTER parametervalue,
					SQLLEN *strlen_or_ind) {
	debugFunction();
	return SQLR_SQLBindParameter(statementhandle,
					parameternumber,
					SQL_PARAM_INPUT,
					valuetype,
					parametertype,
					lengthprecision,
					parameterscale,
					parametervalue,
					0,
					strlen_or_ind);
}

SQLRETURN SQL_API SQLR_SQLCancelHandle(SQLSMALLINT handletype,
						SQLHANDLE handle) {
	debugFunction();

	if (handletype==SQL_HANDLE_ENV) {
		ENV	*env=(ENV *)handle;
		if (handle==SQL_NULL_HENV || !env) {
			debugPrintf("  NULL env handle\n");
			return SQL_INVALID_HANDLE;
		}
		SQLR_ENVSetError(env,
			"Invalid attribute/option identifier",0,"HY092");
	} else if (handletype==SQL_HANDLE_DBC) {
		CONN	*conn=(CONN *)handle;
		if (handle==SQL_NULL_HANDLE || !conn || !conn->con) {
			debugPrintf("  NULL conn handle\n");
			return SQL_INVALID_HANDLE;
		}
		SQLR_CONNSetError(conn,
			"Driver does not support this function",0,"IM001");
	} else if (handletype==SQL_HANDLE_STMT) {
		STMT	*stmt=(STMT *)handle;
		if (handle==SQL_NULL_HSTMT || !stmt || !stmt->cur) {
			debugPrintf("  NULL stmt handle\n");
			return SQL_INVALID_HANDLE;
		}
		SQLR_STMTSetError(stmt,
			"Driver does not support this function",0,"IM001");
	}
	return SQL_ERROR;
}

SQLRETURN SQL_API SQLCancel(SQLHSTMT statementhandle) {
	debugFunction();
	return SQLR_SQLCancelHandle(SQL_HANDLE_STMT,(SQLHANDLE)statementhandle);
}

SQLRETURN SQL_API SQLCancelHandle(SQLSMALLINT handletype, SQLHANDLE handle) {
	debugFunction();
	return SQLR_SQLCancelHandle(handletype,handle);
}

static void SQLR_ResetParams(STMT *stmt) {
	debugFunction();

	// clear bind variables
	stmt->cur->clearBinds();

	// clear input bind list
	linkedlist<dictionarynode<int32_t, char * > *>
				*ibslist=stmt->inputbindstrings.getList();
	for (linkedlistnode<dictionarynode<int32_t, char * > *>
					*node=ibslist->getFirst();
					node; node=node->getNext()) {
		delete[] node->getValue();
	}
	ibslist->clear();

	// clear output bind list
	linkedlist<dictionarynode<int32_t, outputbind * > *>
				*oblist=stmt->outputbinds.getList();
	for (linkedlistnode<dictionarynode<int32_t, outputbind * > *>
					*node=oblist->getFirst();
					node; node=node->getNext()) {
		delete node->getValue();
	}
	oblist->clear();
}

static SQLRETURN SQLR_SQLCloseCursor(SQLHSTMT statementhandle) {
	debugFunction();

	STMT	*stmt=(STMT *)statementhandle;
	if (statementhandle==SQL_NULL_HSTMT || !stmt || !stmt->cur) {
		debugPrintf("  NULL stmt handle\n");
		return SQL_INVALID_HANDLE;
	}

	SQLR_ResetParams(stmt);
	stmt->cur->closeResultSet();

	return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLCloseCursor(SQLHSTMT statementhandle) {
	debugFunction();
	return SQLR_SQLCloseCursor(statementhandle);
}

static SQLSMALLINT SQLR_MapColumnType(sqlrcursor *cur, uint32_t col) {
	const char	*ctype=cur->getColumnType(col);
	if (!charstring::compare(ctype,"UNKNOWN")) {
		return SQL_UNKNOWN_TYPE;
	}
	if (!charstring::compare(ctype,"CHAR")) {
		return SQL_CHAR;
	}
	if (!charstring::compare(ctype,"INT")) {
		return SQL_INTEGER;
	}
	if (!charstring::compare(ctype,"SMALLINT")) {
		return SQL_SMALLINT;
	}
	if (!charstring::compare(ctype,"TINYINT")) {
		return SQL_TINYINT;
	}
	if (!charstring::compare(ctype,"MONEY")) {
		return SQL_CHAR;
	}
	if (!charstring::compare(ctype,"DATETIME")) {
		return SQL_DATETIME;
	}
	if (!charstring::compare(ctype,"NUMERIC")) {
		return SQL_NUMERIC;
	}
	if (!charstring::compare(ctype,"DECIMAL")) {
		return SQL_DECIMAL;
	}
	if (!charstring::compare(ctype,"SMALLDATETIME")) {
		return SQL_TIMESTAMP;
	}
	if (!charstring::compare(ctype,"SMALLMONEY")) {
		return SQL_CHAR;
	}
	if (!charstring::compare(ctype,"IMAGE")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"BINARY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"BIT")) {
		return SQL_BIT;
	}
	if (!charstring::compare(ctype,"REAL")) {
		return SQL_REAL;
	}
	if (!charstring::compare(ctype,"FLOAT")) {
		return SQL_FLOAT;
	}
	if (!charstring::compare(ctype,"TEXT")) {
		return SQL_CHAR;
	}
	if (!charstring::compare(ctype,"VARCHAR")) {
		return SQL_VARCHAR;
	}
	if (!charstring::compare(ctype,"VARBINARY")) {
		return SQL_VARBINARY;
	}
	if (!charstring::compare(ctype,"LONGCHAR")) {
		return SQL_CHAR;
	}
	if (!charstring::compare(ctype,"LONGBINARY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"LONG")) {
		return SQL_CHAR;
	}
	if (!charstring::compare(ctype,"ILLEGAL")) {
		return SQL_CHAR;
	}
	if (!charstring::compare(ctype,"SENSITIVITY")) {
		return SQL_CHAR;
	}
	if (!charstring::compare(ctype,"BOUNDARY")) {
		return SQL_CHAR;
	}
	if (!charstring::compare(ctype,"VOID")) {
		return SQL_CHAR;
	}
	if (!charstring::compare(ctype,"USHORT")) {
		return SQL_SMALLINT;
	}

	// added by lago
	if (!charstring::compare(ctype,"UNDEFINED")) {
		return SQL_UNKNOWN_TYPE;
	}
	if (!charstring::compare(ctype,"DOUBLE")) {
		return SQL_DOUBLE;
	}
	if (!charstring::compare(ctype,"DATE")) {
		return SQL_DATETIME;
	}
	if (!charstring::compare(ctype,"TIME")) {
		return SQL_TIME;
	}
	if (!charstring::compare(ctype,"TIMESTAMP")) {
		return SQL_TIMESTAMP;
	}

	// added by msql
	if (!charstring::compare(ctype,"UINT")) {
		return SQL_INTEGER;
	}
	if (!charstring::compare(ctype,"LASTREAL")) {
		return SQL_REAL;
	}

	// added by mysql
	if (!charstring::compare(ctype,"STRING")) {
		return SQL_CHAR;
	}
	if (!charstring::compare(ctype,"VARSTRING")) {
		return SQL_VARCHAR;
	}
	if (!charstring::compare(ctype,"LONGLONG")) {
		return SQL_BIGINT;
	}
	if (!charstring::compare(ctype,"MEDIUMINT")) {
		return SQL_INTEGER;
	}
	if (!charstring::compare(ctype,"YEAR")) {
		return SQL_SMALLINT;
	}
	if (!charstring::compare(ctype,"NEWDATE")) {
		return SQL_DATETIME;
	}
	if (!charstring::compare(ctype,"NULL")) {
		return SQL_CHAR;
	}
	if (!charstring::compare(ctype,"ENUM")) {
		return SQL_CHAR;
	}
	if (!charstring::compare(ctype,"SET")) {
		return SQL_CHAR;
	}
	if (!charstring::compare(ctype,"TINYBLOB")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"MEDIUMBLOB")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"LONGBLOB")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"BLOB")) {
		return SQL_BINARY;
	}

	// added by oracle
	if (!charstring::compare(ctype,"VARCHAR2")) {
		return SQL_VARCHAR;
	}
	if (!charstring::compare(ctype,"NUMBER")) {
		return SQL_NUMERIC;
	}
	if (!charstring::compare(ctype,"ROWID")) {
		return SQL_BIGINT;
	}
	if (!charstring::compare(ctype,"RAW")) {
		return SQL_VARBINARY;
	}
	if (!charstring::compare(ctype,"LONG_RAW")) {
		return SQL_LONGVARBINARY;
	}
	if (!charstring::compare(ctype,"MLSLABEL")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"CLOB")) {
		return SQL_LONGVARCHAR;
	}
	if (!charstring::compare(ctype,"BFILE")) {
		return SQL_LONGVARBINARY;
	}

	// added by odbc
	if (!charstring::compare(ctype,"BIGINT")) {
		return SQL_BIGINT;
	}
	if (!charstring::compare(ctype,"INTEGER")) {
		return SQL_INTEGER;
	}
	if (!charstring::compare(ctype,"LONGVARBINARY")) {
		return SQL_LONGVARBINARY;
	}
	if (!charstring::compare(ctype,"LONGVARCHAR")) {
		return SQL_LONGVARCHAR;
	}

	// added by db2
	if (!charstring::compare(ctype,"GRAPHIC")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"VARGRAPHIC")) {
		return SQL_VARBINARY;
	}
	if (!charstring::compare(ctype,"LONGVARGRAPHIC")) {
		return SQL_LONGVARBINARY;
	}
	if (!charstring::compare(ctype,"DBCLOB")) {
		return SQL_LONGVARCHAR;
	}
	if (!charstring::compare(ctype,"DATALINK")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"USER_DEFINED_TYPE")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"SHORT_DATATYPE")) {
		return SQL_SMALLINT;
	}
	if (!charstring::compare(ctype,"TINY_DATATYPE")) {
		return SQL_TINYINT;
	}

	// added by firebird
	if (!charstring::compare(ctype,"D_FLOAT")) {
		return SQL_DOUBLE;
	}
	if (!charstring::compare(ctype,"ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"QUAD")) {
		return SQL_BIGINT;
	}
	if (!charstring::compare(ctype,"INT64")) {
		return SQL_BIGINT;
	}
	if (!charstring::compare(ctype,"DOUBLE PRECISION")) {
		return SQL_DOUBLE;
	}

	// added by postgresql
	if (!charstring::compare(ctype,"BOOL")) {
		return SQL_CHAR;
	}
	if (!charstring::compare(ctype,"BYTEA")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"NAME")) {
		return SQL_CHAR;
	}
	if (!charstring::compare(ctype,"INT8")) {
		return SQL_BIGINT;
	}
	if (!charstring::compare(ctype,"INT2")) {
		return SQL_SMALLINT;
	}
	if (!charstring::compare(ctype,"INT2VECTOR")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"INT4")) {
		return SQL_INTEGER;
	}
	if (!charstring::compare(ctype,"REGPROC")) {
		return SQL_BIGINT;
	}
	if (!charstring::compare(ctype,"OID")) {
		return SQL_BIGINT;
	}
	if (!charstring::compare(ctype,"TID")) {
		return SQL_BIGINT;
	}
	if (!charstring::compare(ctype,"XID")) {
		return SQL_BIGINT;
	}
	if (!charstring::compare(ctype,"CID")) {
		return SQL_BIGINT;
	}
	if (!charstring::compare(ctype,"OIDVECTOR")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"SMGR")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"POINT")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"LSEG")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"PATH")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"BOX")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"POLYGON")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"LINE")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"LINE_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"FLOAT4")) {
		return SQL_FLOAT;
	}
	if (!charstring::compare(ctype,"FLOAT8")) {
		return SQL_DOUBLE;
	}
	if (!charstring::compare(ctype,"ABSTIME")) {
		return SQL_INTEGER;
	}
	if (!charstring::compare(ctype,"RELTIME")) {
		return SQL_INTEGER;
	}
	if (!charstring::compare(ctype,"TINTERVAL")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"CIRCLE")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"CIRCLE_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"MONEY_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"MACADDR")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"INET")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"CIDR")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"BOOL_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"BYTEA_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"CHAR_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"NAME_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"INT2_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"INT2VECTOR_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"INT4_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"REGPROC_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"TEXT_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"OID_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"TID_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"XID_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"CID_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"OIDVECTOR_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"BPCHAR_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"VARCHAR_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"INT8_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"POINT_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"LSEG_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"PATH_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"BOX_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"FLOAT4_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"FLOAT8_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"ABSTIME_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"RELTIME_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"TINTERVAL_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"POLYGON_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"ACLITEM")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"ACLITEM_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"MACADDR_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"INET_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"CIDR_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"BPCHAR")) {
		return SQL_CHAR;
	}
	if (!charstring::compare(ctype,"TIMESTAMP_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"DATE_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"TIME_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"TIMESTAMPTZ")) {
		return SQL_TIMESTAMP;
	}
	if (!charstring::compare(ctype,"TIMESTAMPTZ_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"INTERVAL")) {
		return SQL_INTERVAL;
	}
	if (!charstring::compare(ctype,"INTERVAL_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"NUMERIC_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"TIMETZ")) {
		return SQL_TIME;
	}
	if (!charstring::compare(ctype,"TIMETZ_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"BIT_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"VARBIT")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"VARBIT_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"REFCURSOR")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"REFCURSOR_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"REGPROCEDURE")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"REGOPER")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"REGOPERATOR")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"REGCLASS")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"REGTYPE")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"REGPROCEDURE_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"REGOPER_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"REGOPERATOR_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"REGCLASS_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"REGTYPE_ARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"RECORD")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"CSTRING")) {
		return SQL_CHAR;
	}
	if (!charstring::compare(ctype,"ANY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"ANYARRAY")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"TRIGGER")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"LANGUAGE_HANDLER")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"INTERNAL")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"OPAQUE")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"ANYELEMENT")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"PG_TYPE")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"PG_ATTRIBUTE")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"PG_PROC")) {
		return SQL_BINARY;
	}
	if (!charstring::compare(ctype,"PG_CLASS")) {
		return SQL_BINARY;
	}
	return SQL_CHAR;
}

static SQLSMALLINT SQLR_MapCColumnType(sqlrcursor *cur, uint32_t col) {
	switch (SQLR_MapColumnType(cur,col)) {
		case SQL_UNKNOWN_TYPE:
			return SQL_C_CHAR;
		case SQL_CHAR:
			return SQL_C_CHAR;
		case SQL_NUMERIC:
			return SQL_C_CHAR;
		case SQL_DECIMAL:
			return SQL_C_CHAR;
		case SQL_INTEGER:
			return SQL_C_LONG;
		case SQL_SMALLINT:
			return SQL_C_SHORT;
		case SQL_FLOAT:
			return SQL_C_FLOAT;
		case SQL_REAL:
			return SQL_C_FLOAT;
		case SQL_DOUBLE:
			return SQL_C_DOUBLE;
		case SQL_DATETIME:
			return SQL_C_TIMESTAMP;
		case SQL_VARCHAR:
			return SQL_C_CHAR;
		case SQL_TYPE_DATE:
			return SQL_C_DATE;
		case SQL_TYPE_TIME:
			return SQL_C_TIME;
		case SQL_TYPE_TIMESTAMP:
			return SQL_C_TIMESTAMP;
		// case SQL_INTERVAL:
		// 	(dup of SQL_TIME)
		case SQL_TIME:
			return SQL_C_TIME;
		case SQL_TIMESTAMP:
			return SQL_C_TIMESTAMP;
		case SQL_LONGVARCHAR:
			return SQL_C_CHAR;
		case SQL_BINARY:
			return SQL_C_BINARY;
		case SQL_VARBINARY:
			return SQL_C_BINARY;
		case SQL_LONGVARBINARY:
			return SQL_C_BINARY;
		case SQL_BIGINT:
			return SQL_C_SBIGINT;
		case SQL_TINYINT:
			return SQL_C_TINYINT;
		case SQL_BIT:
			return SQL_C_BIT;
		case SQL_GUID:
			return SQL_C_GUID;
	}
	return SQL_C_CHAR;
}

static SQLULEN SQLR_GetColumnSize(sqlrcursor *cur, uint32_t col) {
	switch (SQLR_MapColumnType(cur,col)) {
		case SQL_UNKNOWN_TYPE:
		case SQL_CHAR:
		case SQL_NUMERIC:
		case SQL_DECIMAL:
		case SQL_VARCHAR:
		case SQL_LONGVARCHAR:
		case SQL_BINARY:
		case SQL_VARBINARY:
		case SQL_LONGVARBINARY:
			{
			// FIXME: this really ought to be sorted out in the
			// connection code, rather than here.
			uint32_t	precision=cur->getColumnPrecision(col);
			uint32_t	length=cur->getColumnLength(col);
			uint32_t	size=(length>precision)?
							length:precision;
			// FIXME: is there a better fallback value?
			return (size)?size:32768;
			}
		case SQL_INTEGER:
			return 10;
		case SQL_SMALLINT:
			return 5;
		case SQL_FLOAT:
			return 15;
		case SQL_REAL:
			return 7;
		case SQL_DOUBLE:
			return 15;
		case SQL_DATETIME:
			return 25;
		case SQL_TYPE_DATE:
			return 10;
		case SQL_TYPE_TIME:
			return 8;
		case SQL_TYPE_TIMESTAMP:
			return 25;
		// case SQL_INTERVAL:
		// 	(dup of SQL_TIME)
		case SQL_TIME:
			return 25;
		case SQL_TIMESTAMP:
			return 25;
		case SQL_BIGINT:
			return 20;
		case SQL_TINYINT:
			return 3;
		case SQL_BIT:
			return 1;
		case SQL_GUID:
			return 36;
	}
	return SQL_C_CHAR;
}

static SQLRETURN SQLR_SQLColAttribute(SQLHSTMT statementhandle,
					SQLUSMALLINT columnnumber,
					SQLUSMALLINT fieldidentifier,
					SQLPOINTER characterattribute,
					SQLSMALLINT bufferlength,
					SQLSMALLINT *stringlength,
					NUMERICATTRIBUTETYPE numericattribute) {
	debugFunction();
	debugPrintf("  columnnumber: %d\n",(int)columnnumber);
	debugPrintf("  bufferlength: %d\n",(int)bufferlength);

	STMT	*stmt=(STMT *)statementhandle;
	if (statementhandle==SQL_NULL_HSTMT || !stmt || !stmt->cur) {
		debugPrintf("  NULL stmt handle\n");
		return SQL_INVALID_HANDLE;
	}

	// initialize the stringlength and buffer to safe return values
	// in case the app uses the result without checking the error code
	charstring::safeCopy((char *)characterattribute,bufferlength,"");
	if (stringlength) {
		*stringlength=0;
	}
	if (numericattribute) {
		// this will cause a problem if something smaller than 
		// SQLSMALLINT is passed in but nobody should be doing that
		*(SQLSMALLINT *)numericattribute=0;
	}

	// make sure we're attempting to get a valid column
	uint32_t	colcount=stmt->cur->colCount();
	if (columnnumber<1 || columnnumber>colcount) {
		debugPrintf("  invalid column: %d\n",columnnumber);
		SQLR_STMTSetError(stmt,NULL,0,"07009");
		return SQL_ERROR;
	}

	// get a zero-based version of the columnnumber
	uint32_t	col=columnnumber-1;

	switch (fieldidentifier) {
		case SQL_DESC_COUNT:
		case SQL_COLUMN_COUNT:
			debugPrintf("  fieldidentifier: "
					"SQL_DESC/COLUMN_COUNT\n");
			*(SQLSMALLINT *)numericattribute=colcount;
			debugPrintf("  count: %lld\n",
				(int64_t)*(SQLSMALLINT *)numericattribute);
			break;
		case SQL_DESC_TYPE:
		//case SQL_DESC_CONCISE_TYPE:
		//	(dup of SQL_COLUMN_TYPE)
		case SQL_COLUMN_TYPE:
			debugPrintf("  fieldidentifier: "
					"SQL_DESC_TYPE/"
					"SQL_DESC_CONCISE_TYPE/"
					"COLUMN_TYPE\n");
			*(SQLSMALLINT *)numericattribute=
					SQLR_MapColumnType(stmt->cur,col);
			debugPrintf("  type: %lld\n",
				(int64_t)*(SQLSMALLINT *)numericattribute);
			break;
		case SQL_DESC_LENGTH:
		case SQL_DESC_OCTET_LENGTH:
		case SQL_COLUMN_LENGTH:
			debugPrintf("  fieldidentifier: "
					"SQL_DESC_LENGTH/COLUMN_LENGTH/"
					"SQL_DESC_OCTET_LENGTH\n");
			*(SQLINTEGER *)numericattribute=
					SQLR_GetColumnSize(stmt->cur,col);
			debugPrintf("  length: %lld\n",
				(int64_t)*(SQLSMALLINT *)numericattribute);
			break;
		case SQL_DESC_PRECISION:
		case SQL_COLUMN_PRECISION:
			debugPrintf("  fieldidentifier: "
					"SQL_DESC/COLUMN_PRECISION\n");
			*(SQLSMALLINT *)numericattribute=
					stmt->cur->getColumnPrecision(col);
			debugPrintf("  precision: %lld\n",
				(int64_t)*(SQLSMALLINT *)numericattribute);
			break;
		case SQL_DESC_SCALE:
		case SQL_COLUMN_SCALE:
			debugPrintf("  fieldidentifier: "
					"SQL_DESC/COLUMN_SCALE\n");
			*(SQLSMALLINT *)numericattribute=
					stmt->cur->getColumnScale(col);
			debugPrintf("  scale: %lld\n",
				(int64_t)*(SQLSMALLINT *)numericattribute);
			break;
		case SQL_DESC_NULLABLE:
		case SQL_COLUMN_NULLABLE:
			debugPrintf("  fieldidentifier: "
					"SQL_DESC/COLUMN_NULLABLE\n");
			*(SQLSMALLINT *)numericattribute=
					(stmt->cur->getColumnIsNullable(col))?
						SQL_NULLABLE:SQL_NO_NULLS;
			debugPrintf("  nullable: %lld\n",
				(int64_t)*(SQLSMALLINT *)numericattribute);
			break;
		case SQL_DESC_NAME:
		case SQL_COLUMN_NAME:
			debugPrintf("  fieldidentifier: "
					"SQL_DESC/COLUMN_NAME\n");
			{
			// SQL Relay doesn't know about column aliases,
			// just return the name.
			const char *name=stmt->cur->getColumnName(col);
			charstring::safeCopy((char *)characterattribute,
							bufferlength,name);
			debugPrintf("  name: \"%s\"\n",
					(const char *)characterattribute);
			if (stringlength) {
				*stringlength=charstring::length(name);
				debugPrintf("  length: %d\n",(int)*stringlength);
			}
			}
			break;
		case SQL_DESC_UNNAMED:
			debugPrintf("  fieldidentifier: "
					"SQL_DESC_UNNAMED\n");
			if (charstring::length(stmt->cur->getColumnName(col))) {
				*(SQLSMALLINT *)numericattribute=SQL_NAMED;
			} else {
				*(SQLSMALLINT *)numericattribute=SQL_UNNAMED;
			} 
			debugPrintf("  unnamed: %lld\n",
				(int64_t)*(SQLSMALLINT *)numericattribute);
			break;
		//case SQL_DESC_AUTO_UNIQUE_VALUE:
		//	(dup of SQL_COLUMN_AUTO_INCREMENT)
		case SQL_COLUMN_AUTO_INCREMENT:
			debugPrintf("  fieldidentifier: "
					"SQL_DESC_AUTO_UNIQUE_VALUE/"
					"SQL_COLUMN_AUTO_INCREMENT\n");
			*(SQLINTEGER *)numericattribute=stmt->cur->
					getColumnIsAutoIncrement(col);
			debugPrintf("  auto-increment: %lld\n",
				(int64_t)*(SQLSMALLINT *)numericattribute);
			break;
		case SQL_DESC_BASE_COLUMN_NAME:
			debugPrintf("  fieldidentifier: "
					"SQL_DESC_BASE_COLUMN_NAME\n");
			// SQL Relay doesn't know this, in particular, return
			// an empty string.
			charstring::safeCopy((char *)characterattribute,
							bufferlength,"");
			debugPrintf("  base column name: \"%s\"\n",
					(const char *)characterattribute);
			if (stringlength) {
				*stringlength=0;
				debugPrintf("  length: %d\n",(int)*stringlength);
			}
			break;
		case SQL_DESC_BASE_TABLE_NAME:
			debugPrintf("  fieldidentifier: "
					"SQL_DESC_BASE_TABLE_NAME\n");
			// SQL Relay doesn't know this, return an empty string.
			charstring::safeCopy((char *)characterattribute,
							bufferlength,"");
			debugPrintf("  base table name: \"%s\"\n",
					(const char *)characterattribute);
			if (stringlength) {
				*stringlength=0;
				debugPrintf("  length: %d\n",(int)*stringlength);
			}
			break;
		//case SQL_DESC_CASE_SENSITIVE:
		//	(dup of SQL_COLUMN_CASE_SENSITIVE)
		case SQL_COLUMN_CASE_SENSITIVE:
			debugPrintf("  fieldidentifier: "
					"SQL_DESC/COLUMN_CASE_SENSITIVE\n");
			// not supported, return true
			*(SQLSMALLINT *)numericattribute=SQL_TRUE;
			debugPrintf("  case sensitive: %lld\n",
				(int64_t)*(SQLSMALLINT *)numericattribute);
			break;
		//case SQL_DESC_CATALOG_NAME:
		//	(dup of SQL_COLUMN_QUALIFIER_NAME)
		case SQL_COLUMN_QUALIFIER_NAME:
			debugPrintf("  fieldidentifier: "
					"SQL_DESC_CATALOG_NAME/"
					"SQL_COLUMN_QUALIFIER_NAME\n");
			// not supported, return empty string
			charstring::safeCopy((char *)characterattribute,
							bufferlength,"");
			debugPrintf("  column qualifier name: \"%s\"\n",
					(const char *)characterattribute);
			if (stringlength) {
				*stringlength=0;
				debugPrintf("  length: %d\n",(int)*stringlength);
			}
			break;
		//case SQL_DESC_DISPLAY_SIZE:
		//	(dup of SQL_COLUMN_DISPLAY_SIZE)
		case SQL_COLUMN_DISPLAY_SIZE:
			debugPrintf("  fieldidentifier: "
					"SQL_DESC/COLUMN_DISPLAY_SIZE\n");
			*(SQLLEN *)numericattribute=stmt->cur->getLongest(col);
			debugPrintf("  display size: %lld\n",
				(int64_t)*(SQLSMALLINT *)numericattribute);
			break;
		//case SQL_DESC_FIXED_PREC_SCALE
		//	(dup of SQL_COLUMN_MONEY)
		case SQL_COLUMN_MONEY:
			{
			debugPrintf("  fieldidentifier: "
					"SQL_DESC_FIXED_PREC_SCALE/"
					"SQL_COLUMN_MONEY\n");
			const char	*type=stmt->cur->getColumnType(col);
			debugPrintf("  fixed prec scale: ");
			if (!charstring::compareIgnoringCase(
							type,"money") ||
				!charstring::compareIgnoringCase(
							type,"smallmoney")) {
				*(SQLSMALLINT *)numericattribute=SQL_TRUE;
				debugPrintf("  true\n");
			} else {
				*(SQLSMALLINT *)numericattribute=SQL_FALSE;
				debugPrintf("  false\n");
			}
			}
			break;
		//case SQL_DESC_LABEL
		//	(dup of SQL_COLUMN_LABEL)
		case SQL_COLUMN_LABEL:
			{
			debugPrintf("  fieldidentifier: "
					"SQL_DESC/COLUMN_LABEL\n");
			const char *name=stmt->cur->getColumnName(col);
			charstring::safeCopy((char *)characterattribute,
							bufferlength,name);
			debugPrintf("  label: \"%s\"\n",
					(const char *)characterattribute);
			if (stringlength) {
				*stringlength=charstring::length(name);
				debugPrintf("  length: %d\n",(int)*stringlength);
			}
			}
			break;
		case SQL_DESC_LITERAL_PREFIX:
			{
			debugPrintf("  fieldidentifier: "
					"SQL_DESC_LITERAL_PREFIX\n");
			// single-quote for char, 0x for binary
			SQLSMALLINT	type=SQLR_MapColumnType(stmt->cur,col);
			if (type==SQL_CHAR ||
				type==SQL_VARCHAR ||
				type==SQL_LONGVARCHAR) {
				charstring::safeCopy((char *)characterattribute,
							bufferlength,"'");
				if (stringlength) {
					*stringlength=1;
				}
			} else if (type==SQL_BINARY ||
					type==SQL_VARBINARY ||
					type==SQL_LONGVARBINARY) {
				charstring::safeCopy((char *)characterattribute,
							bufferlength,"0x");
				if (stringlength) {
					*stringlength=2;
				}
			} else {
				charstring::safeCopy((char *)characterattribute,
							bufferlength,"");
				if (stringlength) {
					*stringlength=0;
				}
			}
			debugPrintf("  literal prefix: %s\n",
					(const char *)characterattribute);
			if (stringlength) {
				debugPrintf("  length: %d\n",(int)*stringlength);
			}
			}
			break;
		case SQL_DESC_LITERAL_SUFFIX:
			{
			debugPrintf("  fieldidentifier: "
					"SQL_DESC_LITERAL_SUFFIX\n");
			// single-quote for char
			SQLSMALLINT	type=SQLR_MapColumnType(stmt->cur,col);
			if (type==SQL_CHAR ||
				type==SQL_VARCHAR ||
				type==SQL_LONGVARCHAR) {
				charstring::safeCopy((char *)characterattribute,
							bufferlength,"'");
				if (stringlength) {
					*stringlength=1;
				}
			} else {
				charstring::safeCopy((char *)characterattribute,
							bufferlength,"");
				if (stringlength) {
					*stringlength=0;
				}
			}
			debugPrintf("  literal prefix: %s\n",
					(const char *)characterattribute);
			if (stringlength) {
				debugPrintf("  length: %d\n",(int)*stringlength);
			}
			}
			break;
		case SQL_DESC_LOCAL_TYPE_NAME:
			{
			debugPrintf("  fieldidentifier: "
					"SQL_DESC_LOCAL_TYPE_NAME\n");
			const char *name=stmt->cur->getColumnType(col);
			charstring::safeCopy((char *)characterattribute,
							bufferlength,name);
			debugPrintf("  local type name: \"%s\"\n",
					(const char *)characterattribute);
			if (stringlength) {
				*stringlength=charstring::length(name);
				debugPrintf("  length: %d\n",(int)*stringlength);
			}
			}
			break;
		case SQL_DESC_NUM_PREC_RADIX:
			debugPrintf("  fieldidentifier: "
					"SQL_DESC_NUM_PREC_RADIX\n");
			// FIXME: 2 for approximate numeric types,
			// 10 for exact numeric types, 0 otherwise
			*(SQLINTEGER *)numericattribute=0;
			debugPrintf("  num prec radix: %lld\n",
				(int64_t)*(SQLSMALLINT *)numericattribute);
			break;
		//case SQL_DESC_SCHEMA_NAME
		//	(dup of SQL_COLUMN_OWNER_NAME)
		case SQL_COLUMN_OWNER_NAME:
			debugPrintf("  fieldidentifier: "
					"SQL_DESC_SCHEMA_NAME/"
					"SQL_COLUMN_OWNER_NAME\n");
			// SQL Relay doesn't know this, return an empty string.
			charstring::safeCopy((char *)characterattribute,
							bufferlength,"");
			debugPrintf("  owner name: \"%s\"\n",
					(const char *)characterattribute);
			if (stringlength) {
				*stringlength=0;
				debugPrintf("  length: %d\n",(int)*stringlength);
			}
			break;
		//case SQL_DESC_SEARCHABLE
		//	(dup of SQL_COLUMN_SEARCHABLE)
		case SQL_COLUMN_SEARCHABLE:
			debugPrintf("  fieldidentifier: "
					"SQL_DESC/COLUMN_SEARCHABLE\n");
			// not supported, return searchable
			*(SQLINTEGER *)numericattribute=SQL_SEARCHABLE;
			debugPrintf("  updatable: SQL_SEARCHABLE\n");
			break;
		//case SQL_DESC_TYPE_NAME
		//	(dup of SQL_COLUMN_TYPE_NAME)
		case SQL_COLUMN_TYPE_NAME:
			debugPrintf("  fieldidentifier: "
					"SQL_DESC/COLUMN_TYPE_NAME\n");
			{
			debugPrintf("  fieldidentifier: "
					"SQL_DESC_LOCAL_TYPE_NAME\n");
			const char *name=stmt->cur->getColumnType(col);
			charstring::safeCopy((char *)characterattribute,
							bufferlength,name);
			debugPrintf("  type name: \"%s\"\n",
					(const char *)characterattribute);
			if (stringlength) {
				*stringlength=charstring::length(name);
				debugPrintf("  length: %d\n",(int)*stringlength);
			}
			}
			break;
		//case SQL_DESC_TABLE_NAME
		//	(dup of SQL_COLUMN_TABLE_NAME)
		case SQL_COLUMN_TABLE_NAME:
			debugPrintf("  fieldidentifier: "
					"SQL_DESC/COLUMN_TABLE_NAME\n");
			// not supported, return an empty string
			charstring::safeCopy((char *)characterattribute,
							bufferlength,"");
			debugPrintf("  table name: \"%s\"\n",
					(const char *)characterattribute);
			if (stringlength) {
				*stringlength=0;
				debugPrintf("  length: %d\n",(int)*stringlength);
			}
			break;
		//case SQL_DESC_UNSIGNED
		//	(dup of SQL_COLUMN_UNSIGNED)
		case SQL_COLUMN_UNSIGNED:
			debugPrintf("  fieldidentifier: "
					"SQL_DESC/COLUMN_UNSIGNED\n");
			*(SQLSMALLINT *)numericattribute=
					stmt->cur->getColumnIsUnsigned(col);
			debugPrintf("  unsigned: %lld\n",
				(int64_t)*(SQLSMALLINT *)numericattribute);
			break;
		//case SQL_DESC_UPDATABLE
		//	(dup of SQL_COLUMN_UPDATABLE)
		case SQL_COLUMN_UPDATABLE:
			debugPrintf("  fieldidentifier: "
					"SQL_DESC/COLUMN_UPDATEABLE\n");
			// not supported, return unknown
			*(SQLINTEGER *)numericattribute=
					SQL_ATTR_READWRITE_UNKNOWN;
			debugPrintf("  updatable: SQL_ATTR_READWRITE_UNKNOWN\n");
			break;
		#if (ODBCVER < 0x0300)
		case SQL_COLUMN_DRIVER_START:
			debugPrintf("  fieldidentifier: "
					"SQL_COLUMN_DRIVER_START\n");
			// not supported, return 0
			*(SQLINTEGER *)numericattribute=0;
			debugPrintf("  driver start: %lld\n",
				(int64_t)*(SQLSMALLINT *)numericattribute);
			break;
		#endif
		default:
			debugPrintf("  invalid valuetype\n");
			return SQL_ERROR;
	}

	return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLColAttribute(SQLHSTMT statementhandle,
					SQLUSMALLINT columnnumber,
					SQLUSMALLINT fieldidentifier,
					SQLPOINTER characterattribute,
					SQLSMALLINT bufferlength,
					SQLSMALLINT *stringlength,
					NUMERICATTRIBUTETYPE numericattribute) {
	debugFunction();
	return SQLR_SQLColAttribute(statementhandle,
					columnnumber,
					fieldidentifier,
					characterattribute,
					bufferlength,
					stringlength,
					numericattribute);
}

static void SQLR_BuildTableName(stringbuffer *table,
				SQLCHAR *catalogname,
				SQLSMALLINT namelength1,
				SQLCHAR *schemaname,
				SQLSMALLINT namelength2,
				SQLCHAR *tablename,
				SQLSMALLINT namelength3) {
	debugFunction();
	if (namelength1) {
		if (namelength1==SQL_NTS) {
			table->append(catalogname);
		} else {
			table->append(catalogname,namelength1);
		}
	}
	if (namelength2) {
		if (table->getStringLength()) {
			table->append('.');
		}
		if (namelength2==SQL_NTS) {
			table->append(schemaname);
		} else {
			table->append(schemaname,namelength2);
		}
	}
	if (namelength3) {
		if (table->getStringLength()) {
			table->append('.');
		}
		if (namelength3==SQL_NTS) {
			table->append(tablename);
		} else {
			table->append(tablename,namelength3);
		}
	}
}

SQLRETURN SQL_API SQLColumns(SQLHSTMT statementhandle,
					SQLCHAR *catalogname,
					SQLSMALLINT namelength1,
					SQLCHAR *schemaname,
					SQLSMALLINT namelength2,
					SQLCHAR *tablename,
					SQLSMALLINT namelength3,
					SQLCHAR *columnname,
					SQLSMALLINT namelength4) {
	debugFunction();

	STMT	*stmt=(STMT *)statementhandle;
	if (statementhandle==SQL_NULL_HSTMT || !stmt || !stmt->cur) {
		debugPrintf("  NULL stmt handle\n");
		return SQL_INVALID_HANDLE;
	}

	// FIXME: I suspect I'll be revisiting his in the future...
	//
	// SQLGetConnectAttr(SQL_ATTR_CURRENT_CATALOG) returns the
	// "current db name".  In most db's, this is the instance but in others
	// (Oracle) it's the schema.  Most db's don't have a concept of
	// instance.schema.table though, just instance.table or schema.table
	// so the two are usually interchangeable.
	//
	// Since this function supports all three (but calls the instance the
	// catalog), an app might pass in "the current catalog" as either the
	// catalog name or the schema name.
	//
	// If it's passed in as the catalog, what would the app pass in as the
	// schema?  Maybe nothing.  But maybe, erroneously, the user it used
	// to log into SQL Relay.  The Oracle Heterogenous Agent does this.
	//
	// A workaround it to use either the catalog, or the schema, but not
	// both, and prefer the catalog.
	//
	// Unfortunately, I'll bet that there are apps out there that need to
	// use both, and I'll bet that I'll be revisiting this code someday.
	stringbuffer	table;
	if (catalogname && catalogname[0]) {
		SQLR_BuildTableName(&table,catalogname,namelength1,
					NULL,0,tablename,namelength3);
	} else if (schemaname && schemaname[0]) {
		SQLR_BuildTableName(&table,NULL,0,
					schemaname,namelength2,
					tablename,namelength3);
	}

	char	*wild=NULL;
	if (namelength4==SQL_NTS) {
		wild=charstring::duplicate((const char *)columnname);
	} else {
		wild=charstring::duplicate((const char *)columnname,
							namelength4);
	}

	debugPrintf("  table: %s\n",table.getString());
	debugPrintf("  wild: %s\n",(wild)?wild:"");

	SQLRETURN	retval=
		(stmt->cur->getColumnList(table.getString(),wild,
						SQLRCLIENTLISTFORMAT_ODBC))?
							SQL_SUCCESS:SQL_ERROR;
	delete[] wild;
	return retval;
}


static SQLRETURN SQLR_SQLConnect(SQLHDBC connectionhandle,
					SQLCHAR *dsn,
					SQLSMALLINT dsnlength,
					SQLCHAR *user,
					SQLSMALLINT userlength,
					SQLCHAR *password,
					SQLSMALLINT passwordlength) {
	debugFunction();

	CONN	*conn=(CONN *)connectionhandle;
	if (connectionhandle==SQL_NULL_HANDLE || !conn) {
		debugPrintf("  NULL conn handle\n");
		return SQL_INVALID_HANDLE;
	}

	// copy the dsn, sometimes it's not NULL-terminated
	if (dsnlength==SQL_NTS) {
		dsnlength=charstring::length(dsn);
	}
	if (dsnlength>=sizeof(conn->dsn)) {
		dsnlength=sizeof(conn->dsn)-1;
	}
	charstring::safeCopy(conn->dsn,sizeof(conn->dsn),
					(const char *)dsn,dsnlength);
	conn->dsn[dsnlength]='\0';

	// get data from dsn
	SQLGetPrivateProfileString((const char *)conn->dsn,"Server","",
					conn->server,sizeof(conn->server),
					ODBC_INI);
	char	portbuf[6];
	SQLGetPrivateProfileString((const char *)conn->dsn,"Port","",
					portbuf,sizeof(portbuf),ODBC_INI);
	conn->port=(uint16_t)charstring::toUnsignedInteger(portbuf);
	SQLGetPrivateProfileString((const char *)conn->dsn,"Socket","",
					conn->socket,sizeof(conn->socket),
					ODBC_INI);
	if (charstring::length(user)) {
		if (userlength==SQL_NTS) {
			charstring::safeCopy(conn->user,
						sizeof(conn->user),
						(const char *)user);
		} else {
			charstring::safeCopy(conn->user,
						sizeof(conn->user),
						(const char *)user,
						userlength);
		}
	} else {
		SQLGetPrivateProfileString((const char *)conn->dsn,
						"User","",
						conn->user,
						sizeof(conn->user),
						ODBC_INI);
	}
	if (charstring::length(password)) {
		if (passwordlength==SQL_NTS) {
			charstring::safeCopy(conn->password,
						sizeof(conn->password),
						(const char *)password,
						passwordlength);
		} else {
			charstring::safeCopy(conn->password,
						sizeof(conn->password),
						(const char *)password);
		}
	} else {
		SQLGetPrivateProfileString((const char *)conn->dsn,
						"Password","",
						conn->password,
						sizeof(conn->password),
						ODBC_INI);
	}
	char	retrytimebuf[6];
	SQLGetPrivateProfileString((const char *)conn->dsn,"RetryTime","0",
					retrytimebuf,sizeof(retrytimebuf),
					ODBC_INI);
	conn->retrytime=(int32_t)charstring::toInteger(retrytimebuf);
	char	triesbuf[6];
	SQLGetPrivateProfileString((const char *)conn->dsn,"Tries","1",
					triesbuf,sizeof(triesbuf),
					ODBC_INI);
	conn->tries=(int32_t)charstring::toInteger(triesbuf);
	char	debugbuf[6];
	SQLGetPrivateProfileString((const char *)conn->dsn,"Debug","0",
					debugbuf,sizeof(debugbuf),
					ODBC_INI);
	conn->debug=(charstring::toInteger(debugbuf)!=0);

	debugPrintf("  DSN: %s\n",conn->dsn);
	debugPrintf("  DSN Length: %d\n",dsnlength);
	debugPrintf("  Server: %s\n",conn->server);
	debugPrintf("  Port: %d\n",(int)conn->port);
	debugPrintf("  Socket: %s\n",conn->socket);
	debugPrintf("  User: %s\n",conn->user);
	debugPrintf("  Password: %s\n",conn->password);
	debugPrintf("  RetryTime: %d\n",(int)conn->retrytime);
	debugPrintf("  Tries: %d\n",(int)conn->tries);
	debugPrintf("  Debug: %d\n",(int)conn->debug);

	// create connection
	conn->con=new sqlrconnection(conn->server,
					conn->port,
					conn->socket,
					conn->user,
					conn->password,
					conn->retrytime,
					conn->tries,
					true);

	#ifdef DEBUG_MESSAGES
	conn->con->debugOn();
	#endif

	if (conn->debug) {
		conn->con->debugOn();
	}

	return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLConnect(SQLHDBC connectionhandle,
					SQLCHAR *dsn,
					SQLSMALLINT dsnlength,
					SQLCHAR *user,
					SQLSMALLINT userlength,
					SQLCHAR *password,
					SQLSMALLINT passwordlength) {
	debugFunction();
	return SQLR_SQLConnect(connectionhandle,dsn,dsnlength,
				user,userlength,password,passwordlength);
}

SQLRETURN SQL_API SQLCopyDesc(SQLHDESC SourceDescHandle,
					SQLHDESC TargetDescHandle) {
	debugFunction();
	// FIXME: do something?
	// I guess the desc handles are ARD, APD, IRD and IPD's.
	return SQL_SUCCESS;
}

#if (ODBCVER < 0x0300)
SQLRETURN SQL_API SQLDataSources(SQLHENV environmenthandle,
					SQLUSMALLINT Direction,
					SQLCHAR *ServerName,
					SQLSMALLINT BufferLength1,
					SQLSMALLINT *NameLength1,
					SQLCHAR *Description,
					SQLSMALLINT BufferLength2,
					SQLSMALLINT *NameLength2) {
	debugFunction();
	// FIXME: this is allegedly mapped in ODBC3 but I can't tell what to
	return SQL_ERROR;
}
#endif

SQLRETURN SQL_API SQLDescribeCol(SQLHSTMT statementhandle,
					SQLUSMALLINT columnnumber,
					SQLCHAR *columnname,
					SQLSMALLINT bufferlength,
					SQLSMALLINT *namelength,
					SQLSMALLINT *datatype,
					SQLULEN *columnsize,
					SQLSMALLINT *decimaldigits,
					SQLSMALLINT *nullable) {
	debugFunction();
	debugPrintf("  columnnumber : %d\n",(int)columnnumber);

	STMT	*stmt=(STMT *)statementhandle;
	if (statementhandle==SQL_NULL_HSTMT || !stmt || !stmt->cur) {
		debugPrintf("  NULL stmt handle\n");
		return SQL_INVALID_HANDLE;
	}

	// make sure we're attempting to get a valid column
	uint32_t	colcount=stmt->cur->colCount();
	if (columnnumber<1 || columnnumber>colcount) {
		debugPrintf("  invalid column: %d\n",columnnumber);
		SQLR_STMTSetError(stmt,NULL,0,"07009");
		return SQL_ERROR;
	}

	// get a zero-based version of the columnnumber
	uint32_t	col=columnnumber-1;

	if (columnname) {
		charstring::safeCopy((char *)columnname,bufferlength,
					stmt->cur->getColumnName(col));
		debugPrintf("  columnname   : %s\n",columnname);
	}
	if (namelength) {
		*namelength=charstring::length(columnname);
		debugPrintf("  namelength   : %d\n",*namelength);
	}
	if (datatype) {
		*datatype=SQLR_MapColumnType(stmt->cur,col);
		debugPrintf("  datatype     : %s\n",
					stmt->cur->getColumnType(col));
	}
	if (columnsize) {
		*columnsize=SQLR_GetColumnSize(stmt->cur,col);
		debugPrintf("  columnsize   : %lld\n",(uint64_t)*columnsize);
	}
	if (decimaldigits) {
		*decimaldigits=(SQLSMALLINT)stmt->cur->getColumnScale(col);
		debugPrintf("  decimaldigits: %d\n",*decimaldigits);
	}
	if (nullable) {
		*nullable=(stmt->cur->getColumnIsNullable(col))?
						SQL_NULLABLE:SQL_NO_NULLS;
		debugPrintf("  nullable     : %d\n",
				stmt->cur->getColumnIsNullable(col));
	}

	return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLDisconnect(SQLHDBC connectionhandle) {
	debugFunction();

	CONN	*conn=(CONN *)connectionhandle;
	if (connectionhandle==SQL_NULL_HANDLE || !conn || !conn->con) {
		debugPrintf("  NULL conn handle\n");
		return SQL_INVALID_HANDLE;
	}

	conn->con->endSession();

	return SQL_SUCCESS;
}

static SQLRETURN SQLR_SQLEndTran(SQLSMALLINT handletype,
					SQLHANDLE handle,
					SQLSMALLINT completiontype) {
	debugFunction();

	switch (handletype) {
		case SQL_HANDLE_ENV:
		{
			debugPrintf("  handletype: SQL_HANDLE_ENV\n");

			ENV	*env=(ENV *)handle;
			if (handle==SQL_NULL_HENV || !env) {
				debugPrintf("  NULL env handle\n");
				return SQL_INVALID_HANDLE;
			}

			for (singlylinkedlistnode<CONN *>	*node=
						env->connlist.getFirst();
						node; node=node->getNext()) {

				if (completiontype==SQL_COMMIT) {
					node->getValue()->con->commit();
				} else if (completiontype==SQL_ROLLBACK) {
					node->getValue()->con->rollback();
				}
			}

			return SQL_SUCCESS;
		}
		case SQL_HANDLE_DBC:
		{
			debugPrintf("  handletype: SQL_HANDLE_DBC\n");

			CONN	*conn=(CONN *)handle;
			if (handle==SQL_NULL_HANDLE || !conn || !conn->con) {
				debugPrintf("  NULL conn handle\n");
				return SQL_INVALID_HANDLE;
			}

			if (completiontype==SQL_COMMIT) {
				conn->con->commit();
			} else if (completiontype==SQL_ROLLBACK) {
				conn->con->rollback();
			}

			return SQL_SUCCESS;
		}
		default:
			debugPrintf("  invalid handletype\n");
			return SQL_ERROR;
	}
}

SQLRETURN SQL_API SQLEndTran(SQLSMALLINT handletype,
					SQLHANDLE handle,
					SQLSMALLINT completiontype) {
	debugFunction();
	return SQLR_SQLEndTran(handletype,handle,completiontype);
}

static SQLRETURN SQLR_SQLGetDiagRec(SQLSMALLINT handletype,
					SQLHANDLE handle,
					SQLSMALLINT recnumber,
					SQLCHAR *sqlstate,
					SQLINTEGER *nativeerror,
					SQLCHAR *messagetext,
					SQLSMALLINT bufferlength,
					SQLSMALLINT *textlength);

SQLRETURN SQL_API SQLError(SQLHENV environmenthandle,
					SQLHDBC connectionhandle,
					SQLHSTMT statementhandle,
					SQLCHAR *sqlstate,
					SQLINTEGER *nativeerror,
					SQLCHAR *messagetext,
					SQLSMALLINT bufferlength,
					SQLSMALLINT *textlength) {
	debugFunction();

	if (environmenthandle && environmenthandle!=SQL_NULL_HENV) {
		return SQLR_SQLGetDiagRec(SQL_HANDLE_ENV,
					(SQLHANDLE)environmenthandle,
					1,sqlstate,
					nativeerror,messagetext,
					bufferlength,textlength);
	} else if (connectionhandle && connectionhandle!=SQL_NULL_HANDLE) {
		return SQLR_SQLGetDiagRec(SQL_HANDLE_DBC,
					(SQLHANDLE)connectionhandle,
					1,sqlstate,
					nativeerror,messagetext,
					bufferlength,textlength);
	} else if (statementhandle && statementhandle!=SQL_NULL_HSTMT) {
		return SQLR_SQLGetDiagRec(SQL_HANDLE_STMT,
					(SQLHANDLE)statementhandle,
					1,sqlstate,
					nativeerror,messagetext,
					bufferlength,textlength);
	}
	debugPrintf("  no valid handle\n");
	return SQL_INVALID_HANDLE;
}

static void SQLR_ParseNumeric(SQL_NUMERIC_STRUCT *ns,
				const char *value, uint32_t valuesize) {
	debugFunction();

	// find the negative sign and decimal, if there are any
	const char	*negative=charstring::findFirst(value,'-');
	const char	*decimal=charstring::findFirst(value,'.');

	ns->precision=valuesize-((negative!=NULL)?1:0)-((decimal!=NULL)?1:0);
	ns->scale=(value+valuesize)-decimal;

	// 1=positive, 0=negative
	ns->sign=(negative==NULL);

	//  A number is stored in the val field of the SQL_NUMERIC_STRUCT
	//  structure as a scaled integer, in little endian mode (the leftmost
	//  byte being the least-significant byte). For example, the number
	//  10.001 base 10, with a scale of 4, is scaled to an integer of
	//  100010. Because this is 186AA in hexadecimal format, the value in
	//  SQL_NUMERIC_STRUCT would be "AA 86 01 00 00 ... 00", with the number
	//  of bytes defined by the SQL_MAX_NUMERIC_LEN #define...

	// Get the number as a positive integer by skipping negative signs and
	// decimals.  It should be OK to convert it to a 64-bit integer as
	// SQL_MAX_NUMERIC_LEN should be 16 or less.
	char		*newnumber=new char[valuesize+1];
	const char	*ptr=value;
	uint32_t	index=0;
	for (; *ptr && index<valuesize; index++) {
		if (*ptr=='-' || *ptr=='.') {
			ptr++;
		}
		newnumber[index]=*ptr;
	}
	newnumber[index]='\0';
	int64_t	newinteger=charstring::toInteger(newnumber);
	delete[] newnumber;
	
	// convert to hex, LSB first
	for (uint8_t i=0; i<SQL_MAX_NUMERIC_LEN; i++) {
		ns->val[i]=newinteger%16;
		newinteger=newinteger/16;
	}
}

static void SQLR_ParseInterval(SQL_INTERVAL_STRUCT *is,
				const char *value, uint32_t valuesize) {
	debugFunction();

	// FIXME: implement
	is->interval_type=(SQLINTERVAL)0;
	is->interval_sign=0;
	is->intval.day_second.day=0;
	is->intval.day_second.hour=0;
	is->intval.day_second.minute=0;
	is->intval.day_second.second=0;
	is->intval.day_second.fraction=0;

	//typedef struct tagSQL_INTERVAL_STRUCT
	//   {
	//   SQLINTERVAL interval_type;
	//   SQLSMALLINT   interval_sign;
	//   union
	//      {
	//      SQL_YEAR_MONTH_STRUCT year_month;
	//      SQL_DAY_SECOND_STRUCT day_second;
	//      } intval;
	//   }SQLINTERVAL_STRUCT;
	//
	//typedef enum
	//   {
	//   SQL_IS_YEAR=1,
	//   SQL_IS_MONTH=2,
	//   SQL_IS_DAY=3,
	//   SQL_IS_HOUR=4,
	//   SQL_IS_MINUTE=5,
	//   SQL_IS_SECOND=6,
	//   SQL_IS_YEAR_TO_MONTH=7,
	//   SQL_IS_DAY_TO_HOUR=8,
	//   SQL_IS_DAY_TO_MINUTE=9,
	//   SQL_IS_DAY_TO_SECOND=10,
	//   SQL_IS_HOUR_TO_MINUTE=11,
	//   SQL_IS_HOUR_TO_SECOND=12,
	//   SQL_IS_MINUTE_TO_SECOND=13,
	//   }SQLINTERVAL;
	//
	//typedef struct tagSQL_YEAR_MONTH
	//   {
	//   SQLUINTEGER year;
	//   SQLUINTEGER month;
	//   }SQL_YEAR_MOHTH_STRUCT;
	//
	//typedef struct tagSQL_DAY_SECOND
	//   {
	//   SQLUINTEGER day;
	//   SQLUNINTEGER hour;
	//   SQLUINTEGER minute;
	//   SQLUINTEGER second;
	//   SQLUINTEGER fraction;
	//   }SQL_DAY_SECOND_STRUCT;
}

static char SQLR_CharToHex(const char input) {
	debugFunction();
	char	ch=input;
	character::toUpperCase(ch);
	if (ch>='0' && ch<='9') {
		ch=ch-'0';
	} else if (ch>='A' && ch<='F') {
		ch=ch-'A'+10;
	} else {
		ch=0;
	}
	return ch;
}

static void SQLR_ParseGuid(SQLGUID *guid,
				const char *value, uint32_t valuesize) {
	debugFunction();

	// GUID:
	// 8 digits - 4 digits - 4 digits - 4 digits - 12 digits
	// (all digits hex)
	// XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX

	// sanity check
	if (valuesize!=36 ||
		value[8]!='-' || value[13]!='-' ||
		value[18]!='-' || value[23]!='-') {
		bytestring::zero(guid,sizeof(SQLGUID));
		return;
	}

	// 8 hex digits (uint32_t)
	for (uint16_t i=0; i<8; i++) {
		guid->Data1=guid->Data1*16+SQLR_CharToHex(value[i]);
	}

	// dash

	// 4 hex digits (uint16_t)
	for (uint16_t i=9; i<13; i++) {
		guid->Data2=guid->Data2*16+SQLR_CharToHex(value[i]);
	}

	// dash

	// 4 hex digits (uint16_t)
	for (uint16_t i=14; i<18; i++) {
		guid->Data3=guid->Data3*16+SQLR_CharToHex(value[i]);
	}

	// dash

	// 4 hex digits (unsigned char)
	guid->Data4[0]=SQLR_CharToHex(value[19])*16+SQLR_CharToHex(value[20]);
	guid->Data4[1]=SQLR_CharToHex(value[21])*16+SQLR_CharToHex(value[22]);

	// dash

	// 12 hex digits (unsigned char)
	guid->Data4[2]=SQLR_CharToHex(value[24])*16+SQLR_CharToHex(value[25]);
	guid->Data4[3]=SQLR_CharToHex(value[26])*16+SQLR_CharToHex(value[27]);
	guid->Data4[4]=SQLR_CharToHex(value[28])*16+SQLR_CharToHex(value[29]);
	guid->Data4[5]=SQLR_CharToHex(value[30])*16+SQLR_CharToHex(value[31]);
	guid->Data4[6]=SQLR_CharToHex(value[32])*16+SQLR_CharToHex(value[33]);
	guid->Data4[7]=SQLR_CharToHex(value[34])*16+SQLR_CharToHex(value[35]);
}

static void SQLR_FetchOutputBinds(SQLHSTMT statementhandle) {
	debugFunction();

	STMT	*stmt=(STMT *)statementhandle;

	linkedlist<dictionarynode<int32_t, outputbind *> *>
				*list=stmt->outputbinds.getList();
	for (linkedlistnode<dictionarynode<int32_t, outputbind *> *>
					*node=list->getFirst();
					node; node=node->getNext()) {

		outputbind	*ob=node->getValue()->getValue();

		// convert parameternumber to a string
		char	*parametername=charstring::parseNumber(
						ob->parameternumber);

		switch (ob->valuetype) {
			case SQL_C_CHAR:
				debugPrintf("  valuetype: SQL_C_CHAR\n");
				// make sure to null-terminate
				charstring::safeCopy(
					(char *)ob->parametervalue,
					ob->bufferlength,
					stmt->cur->getOutputBindString(
							parametername),
					stmt->cur->getOutputBindLength(
							parametername)+1);
				break;
			case SQL_C_SLONG:
			case SQL_C_LONG:
				debugPrintf("  valuetype: "
					"SQL_C_SLONG/SQL_C_LONG\n");
				*((long *)ob->parametervalue)=
					(long)stmt->cur->getOutputBindInteger(
								parametername);
				break;
			//case SQL_C_BOOKMARK:
			//	(dup of SQL_C_ULONG)
			case SQL_C_ULONG:
				debugPrintf("  valuetype: "
					"SQL_C_ULONG/SQL_C_BOOKMARK\n");
				*((unsigned long *)ob->parametervalue)=
					(unsigned long)
					stmt->cur->getOutputBindInteger(
								parametername);
				break;
			case SQL_C_SSHORT:
			case SQL_C_SHORT:
				debugPrintf("  valuetype: "
					"SQL_C_SSHORT/SQL_C_SHORT\n");
				*((short *)ob->parametervalue)=
					(short)stmt->cur->getOutputBindInteger(
								parametername);
				break;
			case SQL_C_USHORT:
				debugPrintf("  valuetype: SQL_C_USHORT\n");
				*((unsigned short *)ob->parametervalue)=
					(unsigned short)
					stmt->cur->getOutputBindInteger(
								parametername);
				break;
			case SQL_C_FLOAT:
				debugPrintf("  valuetype: SQL_C_FLOAT\n");
				*((float *)ob->parametervalue)=
					(float)stmt->cur->getOutputBindDouble(
								parametername);
				break;
			case SQL_C_DOUBLE:
				debugPrintf("  valuetype: SQL_C_DOUBLE\n");
				*((double *)ob->parametervalue)=
					(double)stmt->cur->getOutputBindDouble(
								parametername);
				break;
			case SQL_C_NUMERIC:
				debugPrintf("  valuetype: SQL_C_NUMERIC\n");
				SQLR_ParseNumeric(
					(SQL_NUMERIC_STRUCT *)
							ob->parametervalue,
					stmt->cur->getOutputBindString(
							parametername),
					stmt->cur->getOutputBindLength(
							parametername));
				break;
			case SQL_C_DATE:
			case SQL_C_TYPE_DATE:
				{
				debugPrintf("  valuetype: "
					"SQL_C_DATE/SQL_C_TYPE_DATE\n");
				int16_t	year;
				int16_t	month;
				int16_t	day;
				int16_t	hour;
				int16_t	minute;
				int16_t	second;
				int32_t	microsecond;
				const char	*tz;
				stmt->cur->getOutputBindDate(parametername,
							&year,&month,&day,
							&hour,&minute,&second,
							&microsecond,&tz);
				DATE_STRUCT	*ds=
					(DATE_STRUCT *)ob->parametervalue;
				ds->year=year;
				ds->month=month;
				ds->day=day;
				}
				break;
			case SQL_C_TIME:
			case SQL_C_TYPE_TIME:
				{
				debugPrintf("  valuetype: "
					"SQL_C_TIME/SQL_C_TYPE_TIME\n");
				int16_t	year;
				int16_t	month;
				int16_t	day;
				int16_t	hour;
				int16_t	minute;
				int16_t	second;
				int32_t	microsecond;
				const char	*tz;
				stmt->cur->getOutputBindDate(parametername,
							&year,&month,&day,
							&hour,&minute,&second,
							&microsecond,&tz);
				TIME_STRUCT	*ts=
					(TIME_STRUCT *)ob->parametervalue;
				ts->hour=hour;
				ts->minute=minute;
				ts->second=second;
				}
				break;
			case SQL_C_TIMESTAMP:
			case SQL_C_TYPE_TIMESTAMP:
				{
				debugPrintf("  valuetype: "
					"SQL_C_TIMESTAMP/"
					"SQL_C_TYPE_TIMESTAMP\n");
				int16_t	year;
				int16_t	month;
				int16_t	day;
				int16_t	hour;
				int16_t	minute;
				int16_t	second;
				int32_t	microsecond;
				const char	*tz;
				stmt->cur->getOutputBindDate(parametername,
							&year,&month,&day,
							&hour,&minute,&second,
							&microsecond,&tz);
				TIMESTAMP_STRUCT	*ts=
					(TIMESTAMP_STRUCT *)ob->parametervalue;
				ts->year=year;
				ts->month=month;
				ts->day=day;
				ts->hour=hour;
				ts->minute=minute;
				ts->second=second;
				ts->fraction=microsecond*10;
				}
				break;
			case SQL_C_INTERVAL_YEAR:
			case SQL_C_INTERVAL_MONTH:
			case SQL_C_INTERVAL_DAY:
			case SQL_C_INTERVAL_HOUR:
			case SQL_C_INTERVAL_MINUTE:
			case SQL_C_INTERVAL_SECOND:
			case SQL_C_INTERVAL_YEAR_TO_MONTH:
			case SQL_C_INTERVAL_DAY_TO_HOUR:
			case SQL_C_INTERVAL_DAY_TO_MINUTE:
			case SQL_C_INTERVAL_DAY_TO_SECOND:
			case SQL_C_INTERVAL_HOUR_TO_MINUTE:
			case SQL_C_INTERVAL_HOUR_TO_SECOND:
			case SQL_C_INTERVAL_MINUTE_TO_SECOND:
				debugPrintf("  valuetype: SQL_C_INTERVAL_XXX\n");
				SQLR_ParseInterval(
					(SQL_INTERVAL_STRUCT *)
							ob->parametervalue,
					stmt->cur->getOutputBindString(
							parametername),
					stmt->cur->getOutputBindLength(
							parametername));
				break;
			//case SQL_C_VARBOOKMARK:
			//	(dup of SQL_C_BINARY)
			case SQL_C_BINARY:
				{
				debugPrintf("  valuetype: "
					"SQL_C_BINARY/SQL_C_VARBOOKMARK\n");
				charstring::safeCopy(
					(char *)ob->parametervalue,
					ob->bufferlength,
					stmt->cur->getOutputBindBlob(
							parametername),
					stmt->cur->getOutputBindLength(
							parametername));
				break;
				}
			case SQL_C_BIT:
				{
				debugPrintf("  valuetype: SQL_C_BIT\n");
				const char	*val=
					stmt->cur->getOutputBindString(
								parametername);
				((unsigned char *)ob->parametervalue)[0]=
					(charstring::contains("YyTt",val) ||
					charstring::toInteger(val))?'1':'0';
				}
				break;
			case SQL_C_SBIGINT:
				debugPrintf("  valuetype: SQL_C_SBIGINT\n");
				*((int64_t *)ob->parametervalue)=
				(int64_t)stmt->cur->getOutputBindInteger(
								parametername);
				break;
			case SQL_C_UBIGINT:
				debugPrintf("  valuetype: SQL_C_UBIGINT\n");
				*((uint64_t *)ob->parametervalue)=
				(uint64_t)stmt->cur->getOutputBindInteger(
								parametername);
				break;
			case SQL_C_TINYINT:
			case SQL_C_STINYINT:
				debugPrintf("  valuetype: "
					"SQL_C_TINYINT/SQL_C_STINYINT\n");
				*((char *)ob->parametervalue)=
				(char)stmt->cur->getOutputBindInteger(
								parametername);
				break;
			case SQL_C_UTINYINT:
				debugPrintf("  valuetype: SQL_C_UTINYINT\n");
				*((unsigned char *)ob->parametervalue)=
				(unsigned char)stmt->cur->getOutputBindInteger(
								parametername);
				break;
			case SQL_C_GUID:
				debugPrintf("  valuetype: SQL_C_GUID\n");
				SQLR_ParseGuid(
					(SQLGUID *)ob->parametervalue,
					stmt->cur->getOutputBindString(
							parametername),
					stmt->cur->getOutputBindLength(
							parametername));
				break;
			default:
				debugPrintf("  invalue valuetype\n");
				break;
		}
	}
}

uint32_t SQLR_TrimQuery(SQLCHAR *statementtext, SQLINTEGER textlength) {

	// find the length of the string
	uint32_t	length=0;
	if (textlength==SQL_NTS) {
		length=charstring::length((const char *)statementtext);
	} else {
		length=textlength;
	}

	// trim trailing whitespace and semicolons
	for (;;) {
		char	ch=statementtext[length-1];
		if (ch==' ' || ch=='	' || ch=='\n' || ch=='\r' || ch==';') {
			length--;
			if (length==0) {
				return length;
			}
		} else {
			return length;
		}
	}
}

SQLRETURN SQL_API SQLExecDirect(SQLHSTMT statementhandle,
					SQLCHAR *statementtext,
					SQLINTEGER textlength) {
	debugFunction();

	STMT	*stmt=(STMT *)statementhandle;
	if (statementhandle==SQL_NULL_HSTMT || !stmt || !stmt->cur) {
		debugPrintf("  NULL stmt handle\n");
		return SQL_INVALID_HANDLE;
	}

	// reinit row indices
	stmt->currentfetchrow=0;
	stmt->currentstartrow=0;
	stmt->currentgetdatarow=0;

	// clear the error
	SQLR_STMTClearError(stmt);

	// trim query
	uint32_t	statementtextlength=SQLR_TrimQuery(
						statementtext,textlength);

	// run the query
	#ifdef DEBUG_MESSAGES
	stringbuffer	debugstr;
	debugstr.append(statementtext,statementtextlength);
	debugPrintf("  statement: \"%s\",%d)\n",
			debugstr.getString(),(int)statementtextlength);
	#endif
	bool	result=stmt->cur->sendQuery((const char *)statementtext,
							statementtextlength);

	// the statement has been executed
	stmt->executed=true;

	// handle success
	if (result) {
		debugPrintf("  success\n");
		SQLR_FetchOutputBinds(stmt);
		return SQL_SUCCESS;
	}

	// handle error
	debugPrintf("  error\n");
	SQLR_STMTSetError(stmt,stmt->cur->errorMessage(),
				stmt->cur->errorNumber(),NULL);
	return SQL_ERROR;
}

SQLRETURN SQL_API SQLR_SQLExecute(SQLHSTMT statementhandle) {
	debugFunction();

	STMT	*stmt=(STMT *)statementhandle;
	if (statementhandle==SQL_NULL_HSTMT || !stmt || !stmt->cur) {
		debugPrintf("  NULL stmt handle\n");
		return SQL_INVALID_HANDLE;
	}

	// don't actually do anything if the statement
	// was already executed by SQLNumResultCols
	if (stmt->executedbynumresultcols) {
		debugPrintf("  already executed by SQLNumResultCols...\n");
		stmt->executedbynumresultcols=false;
		return stmt->executedbynumresultcolsresult;
	}

	// reinit row indices
	stmt->currentfetchrow=0;
	stmt->currentgetdatarow=0;
	stmt->currentgetdatarow=0;

	// clear the error
	SQLR_STMTClearError(stmt);

	// run the query
	bool	result=stmt->cur->executeQuery();

	// the statement has been executed
	stmt->executed=true;

	// handle success
	if (result) {
		SQLR_FetchOutputBinds(stmt);
		return SQL_SUCCESS;
	}

	// handle error
	SQLR_STMTSetError(stmt,stmt->cur->errorMessage(),
				stmt->cur->errorNumber(),NULL);
	return SQL_ERROR;
}

SQLRETURN SQL_API SQLExecute(SQLHSTMT statementhandle) {
	debugFunction();
	return SQLR_SQLExecute(statementhandle);
}

static SQLRETURN SQLR_SQLGetData(SQLHSTMT statementhandle,
					SQLUSMALLINT columnnumber,
					SQLSMALLINT targettype,
					SQLPOINTER targetvalue,
					SQLLEN bufferlength,
					SQLLEN *strlen_or_ind);

static SQLRETURN SQLR_Fetch(SQLHSTMT statementhandle, SQLULEN *pcrow,
						SQLUSMALLINT *rgfrowstatus) {
	debugFunction();

	STMT	*stmt=(STMT *)statementhandle;
	if (statementhandle==SQL_NULL_HSTMT || !stmt || !stmt->cur) {
		debugPrintf("  NULL stmt handle\n");
		return SQL_INVALID_HANDLE;
	}

	// fetch the row
	SQLRETURN	fetchresult=
			(stmt->cur->getRow(stmt->currentfetchrow))?
					SQL_SUCCESS:SQL_NO_DATA_FOUND;

	// Update the number of rows that were fetched in this operation.
	// (hide the fact that SQL Relay caches the entire result set unless
	// we're explicitly fetching more than 1 row at a time)
	uint64_t	rowstofetch=stmt->cur->getResultSetBufferSize();
	uint64_t	rowsfetched=0;
	if (fetchresult==SQL_NO_DATA_FOUND) {
		rowsfetched=0;
	} else if (rowstofetch) {
		uint64_t	firstrowindex=stmt->cur->firstRowIndex();
		uint64_t	rowcount=stmt->cur->rowCount();
		uint64_t	lastrowindex=(rowcount)?rowcount-1:0;
		uint64_t	bufferedrowcount=lastrowindex-firstrowindex+1;
		rowsfetched=(firstrowindex==stmt->currentfetchrow)?
							bufferedrowcount:0;
	} else {
		rowstofetch=1;
		rowsfetched=1;
	}

	debugPrintf("  rowstofetch: %lld\n",rowstofetch);
	debugPrintf("  rowsfetched: %lld\n",rowsfetched);

	// FIXME: set pcrow if SQLExtendedFetch is called,
	// and don't set rowsfetchedptr
	if (pcrow) {
		*pcrow=rowsfetched;
	}
	if (stmt->rowsfetchedptr) {
		*(stmt->rowsfetchedptr)=rowsfetched;
	}

	// update row statuses
	for (SQLULEN i=0; i<rowstofetch; i++) {
		SQLUSMALLINT	status=(i<rowsfetched)?
					SQL_ROW_SUCCESS:SQL_ROW_NOROW;

		// FIXME: set rgfrowstatus if SQLExtendedFetch
		// is called, and don't set rowstatusptr
		if (rgfrowstatus) {
			rgfrowstatus[i]=status;
		}
		if (stmt->rowstatusptr && stmt->rowstatusptr[i]) {
			stmt->rowstatusptr[i]=status;
		}
	}

	// update column binds
	uint32_t	colcount=stmt->cur->colCount();
	for (uint64_t row=0; row<rowstofetch; row++) {

		for (uint32_t index=0; index<colcount; index++) {

			// get the bound field, if this field isn't bound,
			// move on
			FIELD	*field=NULL;
			if (!stmt->fieldlist.getValue(index,&field)) {
				continue;
			}

			// get the data into the bound column
			SQLRETURN	getdataresult=
					SQLR_SQLGetData(
						statementhandle,
						index+1,
						field->targettype,
						((unsigned char *)
							field->targetvalue)+
							(field->bufferlength*
							row),
						field->bufferlength,
						&(field->strlen_or_ind[row]));
			if (getdataresult!=SQL_SUCCESS) {
				return getdataresult;
			}
		}

		// move on to the next row
		stmt->currentgetdatarow++;
	}

	// reset the current SQLGetData row
	stmt->currentgetdatarow=stmt->currentfetchrow;

	// move on to the next rowset
	stmt->currentstartrow=stmt->currentfetchrow;
	stmt->currentfetchrow=stmt->currentfetchrow+rowsfetched;

	return fetchresult;
}

SQLRETURN SQL_API SQLFetch(SQLHSTMT statementhandle) {
	debugFunction();
	return SQLR_Fetch(statementhandle,NULL,NULL);
}

SQLRETURN SQL_API SQLFetchScroll(SQLHSTMT statementhandle,
					SQLSMALLINT fetchorientation,
					SQLLEN fetchoffset) {
	debugFunction();

	STMT	*stmt=(STMT *)statementhandle;
	if (statementhandle==SQL_NULL_HSTMT || !stmt || !stmt->cur) {
		debugPrintf("  NULL stmt handle\n");
		return SQL_INVALID_HANDLE;
	}

	// for now we only support SQL_FETCH_NEXT
	if (fetchorientation!=SQL_FETCH_NEXT) {
		debugPrintf("  invalid fetchorientation\n");
		stmt->sqlstate="HY106";
		return SQL_ERROR;
	}

	return SQLR_Fetch(statementhandle,NULL,NULL);
}

static SQLRETURN SQLR_SQLFreeHandle(SQLSMALLINT handletype, SQLHANDLE handle);

SQLRETURN SQL_API SQLFreeConnect(SQLHDBC connectionhandle) {
	debugFunction();
	return SQLR_SQLFreeHandle(SQL_HANDLE_DBC,connectionhandle);
}

SQLRETURN SQL_API SQLFreeEnv(SQLHENV environmenthandle) {
	debugFunction();
	return SQLR_SQLFreeHandle(SQL_HANDLE_ENV,environmenthandle);
}

static SQLRETURN SQLR_SQLFreeHandle(SQLSMALLINT handletype, SQLHANDLE handle) {
	debugFunction();

	switch (handletype) {
		case SQL_HANDLE_ENV:
			{
			debugPrintf("  handletype: SQL_HANDLE_ENV\n");
			ENV	*env=(ENV *)handle;
			if (handle==SQL_NULL_HENV || !env) {
				debugPrintf("  NULL env handle\n");
				return SQL_INVALID_HANDLE;
			}
			env->connlist.clear();
			delete[] env->error;
			delete env;
			return SQL_SUCCESS;
			}
		case SQL_HANDLE_DBC:
			{
			debugPrintf("  handletype: SQL_HANDLE_DBC\n");
			CONN	*conn=(CONN *)handle;
			if (handle==SQL_NULL_HANDLE || !conn || !conn->con) {
				debugPrintf("  NULL conn handle\n");
				return SQL_INVALID_HANDLE;
			}
			conn->env->connlist.removeAll(conn);
			conn->stmtlist.clear();
			delete conn->con;
			delete[] conn->error;
			delete conn;
			return SQL_SUCCESS;
			}
		case SQL_HANDLE_STMT:
			{
			debugPrintf("  handletype: SQL_HANDLE_STMT\n");
			STMT	*stmt=(STMT *)handle;
			if (handle==SQL_NULL_HSTMT || !stmt || !stmt->cur) {
				debugPrintf("  NULL stmt handle\n");
				return SQL_INVALID_HANDLE;
			}
			stmt->conn->stmtlist.removeAll(stmt);
			delete stmt->improwdesc;
			delete stmt->impparamdesc;
			delete stmt->cur;
			delete stmt;
			return SQL_SUCCESS;
			}
		case SQL_HANDLE_DESC:
			debugPrintf("  handletype: SQL_HANDLE_DESC\n");
			// FIXME: no idea what to do here,
			// for now just report success
			return SQL_SUCCESS;
		default:
			debugPrintf("  invalid handletype\n");
			return SQL_ERROR;
	}
}

SQLRETURN SQL_API SQLFreeHandle(SQLSMALLINT handletype, SQLHANDLE handle) {
	debugFunction();
	return SQLR_SQLFreeHandle(handletype,handle);
}

SQLRETURN SQL_API SQLFreeStmt(SQLHSTMT statementhandle, SQLUSMALLINT option) {
	debugFunction();

	STMT	*stmt=(STMT *)statementhandle;
	if (statementhandle==SQL_NULL_HSTMT || !stmt || !stmt->cur) {
		debugPrintf("  NULL stmt handle\n");
		return SQL_INVALID_HANDLE;
	}

	switch (option) {
		case SQL_CLOSE:
			debugPrintf("  option: SQL_CLOSE\n");
			return SQLR_SQLCloseCursor(statementhandle);
		case SQL_DROP:
			debugPrintf("  option: SQL_DROP\n");
			return SQLR_SQLFreeHandle(SQL_HANDLE_STMT,
						(SQLHANDLE)statementhandle);
		case SQL_UNBIND:
			debugPrintf("  option: SQL_UNBIND\n");
			stmt->fieldlist.clear();
			return SQL_SUCCESS;
		case SQL_RESET_PARAMS:
			debugPrintf("  option: SQL_RESET_PARAMS\n");
			SQLR_ResetParams(stmt);
			return SQL_SUCCESS;
		default:
			debugPrintf("  invalid option\n");
			return SQL_ERROR;
	}
}

static SQLRETURN SQLR_SQLGetConnectAttr(SQLHDBC connectionhandle,
					SQLINTEGER attribute,
					SQLPOINTER value,
					SQLINTEGER bufferlength,
					SQLINTEGER *stringlength) {
	debugFunction();

	CONN	*conn=(CONN *)connectionhandle;
	if (connectionhandle==SQL_NULL_HANDLE || !conn || !conn->con) {
		debugPrintf("  NULL conn handle\n");
		return SQL_INVALID_HANDLE;
	}

	// FIXME: implement
	switch (attribute) {
		/*case SQL_ACCESS_MODE:
		case SQL_AUTOCOMMIT:
			// settable but not gettable
		case SQL_LOGIN_TIMEOUT:
		case SQL_OPT_TRACE:
		case SQL_OPT_TRACEFILE:
		case SQL_TRANSLATE_DLL:
		case SQL_TRANSLATE_OPTION:
		case SQL_TXN_ISOLATION:*/

		//case SQL_ATTR_CURRENT_CATALOG:
		//	(dup of SQL_CURRENT_QUALIFIER)
		case SQL_CURRENT_QUALIFIER:
			{
			debugPrintf("  attribute: SQL_CURRENT_QUALIFIER/"
						"SQL_ATTR_CURRENT_CATALOG\n");
			const char	*db=conn->con->getCurrentDatabase();
			*stringlength=charstring::length(db);
			charstring::safeCopy((char *)value,bufferlength,
							db,*stringlength);
			debugPrintf("    current catalog: %s\n",db);
			return SQL_SUCCESS;
			}

		/*case SQL_ODBC_CURSORS:
		case SQL_QUIET_MODE:
		case SQL_PACKET_SIZE:
	#if (ODBCVER >= 0x0300)
		case SQL_ATTR_CONNECTION_TIMEOUT:
		case SQL_ATTR_DISCONNECT_BEHAVIOR:
		case SQL_ATTR_ENLIST_IN_DTC:
		case SQL_ATTR_ENLIST_IN_XA:
		case SQL_ATTR_AUTO_IPD:
		case SQL_ATTR_METADATA_ID:
	#endif*/
		default:
			debugPrintf("  unsupported attribute: %d\n",attribute);
			return SQL_SUCCESS;
	}

	return SQL_ERROR;
}

SQLRETURN SQL_API SQLGetConnectAttr(SQLHDBC connectionhandle,
					SQLINTEGER attribute,
					SQLPOINTER value,
					SQLINTEGER bufferlength,
					SQLINTEGER *stringlength) {
	debugFunction();
	return SQLR_SQLGetConnectAttr(connectionhandle,attribute,
					value,bufferlength,stringlength);
}

SQLRETURN SQL_API SQLGetConnectOption(SQLHDBC connectionhandle,
					SQLUSMALLINT option,
					SQLPOINTER value) {
	debugFunction();
	return SQLR_SQLGetConnectAttr(connectionhandle,option,value,256,NULL);
}

SQLRETURN SQL_API SQLGetCursorName(SQLHSTMT statementhandle,
					SQLCHAR *cursorname,
					SQLSMALLINT bufferlength,
					SQLSMALLINT *namelength) {
	debugFunction();

	STMT	*stmt=(STMT *)statementhandle;
	if (statementhandle==SQL_NULL_HSTMT || !stmt || !stmt->cur) {
		debugPrintf("  NULL stmt handle\n");
		return SQL_INVALID_HANDLE;
	}

	if (!stmt->name) {
		stmt->name=charstring::parseNumber(stmtid);
		stmtid++;
	}
	if (cursorname) {
		charstring::safeCopy((char *)cursorname,
					bufferlength,stmt->name);
	}
	if (namelength) {
		*namelength=charstring::length(stmt->name);
	}

	return SQL_SUCCESS;
}

static void SQLR_ParseDate(DATE_STRUCT *ds, const char *value) {

	// result variables
	int16_t	year=-1;
	int16_t	month=-1;
	int16_t	day=-1;
	int16_t	hour=-1;
	int16_t	minute=-1;
	int16_t	second=-1;
	int16_t	fraction=-1;

	// get day/month format
	bool	ddmm=!charstring::compareIgnoringCase(
					environment::getValue(
					"SQLR_ODBC_DATE_DDMM"),
					"yes");
	bool		yyyyddmm=ddmm;
	const char	*yyyyddmmstr=environment::getValue(
					"SQLR_ODBC_DATE_YYYYDDMM");
	if (yyyyddmmstr) {
		yyyyddmm=!charstring::compare(yyyyddmmstr,"yes");
	}

	// parse
	parseDateTime(value,ddmm,yyyyddmm,false,&year,&month,&day,
					&hour,&minute,&second,&fraction);

	// copy data out
	ds->year=(year!=-1)?year:0;
	ds->month=(month!=-1)?month:0;
	ds->day=(day!=-1)?day:0;
}

static void SQLR_ParseTime(TIME_STRUCT *ts, const char *value) {

	// result variables
	int16_t	year=-1;
	int16_t	month=-1;
	int16_t	day=-1;
	int16_t	hour=-1;
	int16_t	minute=-1;
	int16_t	second=-1;
	int16_t	fraction=-1;

	// get day/month format
	bool	ddmm=!charstring::compareIgnoringCase(
					environment::getValue(
					"SQLR_ODBC_DATE_DDMM"),
					"yes");
	bool		yyyyddmm=ddmm;
	const char	*yyyyddmmstr=environment::getValue(
					"SQLR_ODBC_DATE_YYYYDDMM");
	if (yyyyddmmstr) {
		yyyyddmm=!charstring::compare(yyyyddmmstr,"yes");
	}

	// parse
	parseDateTime(value,ddmm,yyyyddmm,false,&year,&month,&day,
					&hour,&minute,&second,&fraction);

	// copy data out
	ts->hour=(hour!=-1)?hour:0;
	ts->minute=(minute!=-1)?minute:0;
	ts->second=(second!=-1)?second:0;
}

static void SQLR_ParseTimeStamp(TIMESTAMP_STRUCT *tss, const char *value) {

	// result variables
	int16_t	year=-1;
	int16_t	month=-1;
	int16_t	day=-1;
	int16_t	hour=-1;
	int16_t	minute=-1;
	int16_t	second=-1;
	int16_t	fraction=-1;

	// get day/month format
	bool	ddmm=!charstring::compareIgnoringCase(
					environment::getValue(
					"SQLR_ODBC_DATE_DDMM"),
					"yes");
	bool		yyyyddmm=ddmm;
	const char	*yyyyddmmstr=environment::getValue(
					"SQLR_ODBC_DATE_YYYYDDMM");
	if (yyyyddmmstr) {
		yyyyddmm=!charstring::compare(yyyyddmmstr,"yes");
	}

	// parse
	parseDateTime(value,ddmm,yyyyddmm,false,&year,&month,&day,
					&hour,&minute,&second,&fraction);

	// copy data out
	tss->year=(year!=-1)?year:0;
	tss->month=(month!=-1)?month:0;
	tss->day=(day!=-1)?day:0;
	tss->hour=(hour!=-1)?hour:0;
	tss->minute=(minute!=-1)?minute:0;
	tss->second=(second!=-1)?second:0;
	tss->fraction=(fraction!=-1)?fraction:0;
}

static SQLRETURN SQLR_SQLGetData(SQLHSTMT statementhandle,
					SQLUSMALLINT columnnumber,
					SQLSMALLINT targettype,
					SQLPOINTER targetvalue,
					SQLLEN bufferlength,
					SQLLEN *strlen_or_ind) {
	debugFunction();

	STMT	*stmt=(STMT *)statementhandle;
	if (statementhandle==SQL_NULL_HSTMT || !stmt || !stmt->cur) {
		debugPrintf("  NULL stmt handle\n");
		return SQL_INVALID_HANDLE;
	}

	debugPrintf("  row   : %d\n",(int)stmt->currentgetdatarow);
	debugPrintf("  column: %d\n",(int)columnnumber);
	debugPrintf("  bufferlength: %d\n",(int)bufferlength);

	// make sure we're attempting to get a valid column
	uint32_t	colcount=stmt->cur->colCount();
	if (columnnumber<1 || columnnumber>colcount) {
		debugPrintf("  invalid column: %d\n",columnnumber);
		SQLR_STMTSetError(stmt,NULL,0,"07009");
		return SQL_ERROR;
	}

	// get a zero-based version of the columnnumber
	uint32_t	col=columnnumber-1;

	// get the field
	const char	*field=stmt->cur->getField(
					stmt->currentgetdatarow,col);
	uint32_t	fieldlength=stmt->cur->getFieldLength(
					stmt->currentgetdatarow,col);
	debugPrintf("  field      : %s\n",field);
	debugPrintf("  fieldlength: %d\n",fieldlength);

	// handle NULL fields
	if (!field) {
		if (strlen_or_ind) {
			*strlen_or_ind=SQL_NULL_DATA;
		}
		debugPrintf("  null field\n");
		return SQL_SUCCESS;
	}

	// reset targettype based on column type
	if (targettype==SQL_C_DEFAULT) {
		targettype=SQLR_MapCColumnType(stmt->cur,col);
	}

	// initialize strlen indicator
	if (strlen_or_ind) {
		*strlen_or_ind=SQLR_GetCColumnTypeSize(targettype);
	}

	// get the field data
	switch (targettype) {
		case SQL_C_CHAR:
			{
			debugPrintf("  targettype: SQL_C_CHAR\n");
			if (strlen_or_ind) {
				*strlen_or_ind=fieldlength;
			}
			// make sure to null-terminate
			charstring::safeCopy((char *)targetvalue,
						bufferlength,
						field,fieldlength+1);
			debugPrintf("  value: %s\n",(char *)targetvalue);
			}
			break;
		case SQL_C_SSHORT:
		case SQL_C_SHORT:
		case SQL_C_USHORT:
			debugPrintf("  targettype: SQL_C_(X)SHORT\n");
			*((short *)targetvalue)=
				(short)charstring::toInteger(field);
			debugPrintf("  value: %d\n",*((short *)targetvalue));
			break;
		case SQL_C_SLONG:
		case SQL_C_LONG:
		//case SQL_C_BOOKMARK:
		//	(dup of SQL_C_ULONG)
		case SQL_C_ULONG:
			debugPrintf("  targettype: SQL_C_(X)LONG\n");
			*((long *)targetvalue)=
				(long)charstring::toInteger(field);
			debugPrintf("  value: %ld\n",*((long *)targetvalue));
			break;
		case SQL_C_FLOAT:
			debugPrintf("  targettype: SQL_C_FLOAT\n");
			*((float *)targetvalue)=
				(float)charstring::toFloat(field);
			debugPrintf("  value: %f\n",*((float *)targetvalue));
			break;
		case SQL_C_DOUBLE:
			debugPrintf("  targettype: SQL_C_DOUBLE\n");
			*((double *)targetvalue)=
				(double)charstring::toFloat(field);
			debugPrintf("  value: %f\n",*((double *)targetvalue));
			break;
		case SQL_C_BIT:
			debugPrintf("  targettype: SQL_C_BIT\n");
			((unsigned char *)targetvalue)[0]=
				(charstring::contains("YyTt",field) ||
				charstring::toInteger(field))?'1':'0';
			debugPrintf("  value: %c\n",
					*((unsigned char *)targetvalue));
			break;
		case SQL_C_STINYINT:
		case SQL_C_TINYINT:
		case SQL_C_UTINYINT:
			debugPrintf("  targettype: SQL_C_(X)TINYINT\n");
			*((char *)targetvalue)=
				charstring::toInteger(field);
			debugPrintf("  value: %c\n",*((char *)targetvalue));
			break;
		case SQL_C_SBIGINT:
		case SQL_C_UBIGINT:
			debugPrintf("  targettype: SQL_C_(X)BIGINT\n");
			*((int64_t *)targetvalue)=
				charstring::toInteger(field);
			debugPrintf("  value: %lld\n",*((int64_t *)targetvalue));
			break;
		//case SQL_C_VARBOOKMARK:
		//	(dup of SQL_C_BINARY)
		case SQL_C_BINARY:
			{
			debugPrintf("  targettype: "
				"SQL_C_BINARY/SQL_C_VARBOOKMARK\n");
			uint32_t	sizetocopy=
					((uint32_t)bufferlength<fieldlength)?
						bufferlength:fieldlength;
			if (strlen_or_ind) {
				*strlen_or_ind=fieldlength;
			}
			bytestring::copy((void *)targetvalue,
					(const void *)field,sizetocopy);
			debugPrintf("  value: ");
			debugSafePrint((char *)targetvalue,sizetocopy);
			debugPrintf("\n");
			}
			break;
		case SQL_C_DATE:
		case SQL_C_TYPE_DATE:
			debugPrintf("  targettype: SQL_C_DATE/SQL_C_TYPE_DATE\n");
			SQLR_ParseDate((DATE_STRUCT *)targetvalue,field);
			break;
		case SQL_C_TIME:
		case SQL_C_TYPE_TIME:
			debugPrintf("  targettype: SQL_C_TIME/SQL_C_TYPE_TIME\n");
			SQLR_ParseTime((TIME_STRUCT *)targetvalue,field);
			break;
		case SQL_C_TIMESTAMP:
		case SQL_C_TYPE_TIMESTAMP:
			debugPrintf("  targettype: "
				"SQL_C_TIMESTAMP/SQL_C_TYPE_TIMESTAMP\n");
			SQLR_ParseTimeStamp(
				(TIMESTAMP_STRUCT *)targetvalue,field);
			break;
		case SQL_C_NUMERIC:
			debugPrintf("  targettype: SQL_C_NUMERIC\n");
			SQLR_ParseNumeric((SQL_NUMERIC_STRUCT *)targetvalue,
							field,fieldlength);
			break;
		case SQL_C_GUID:
			debugPrintf("  targettype: SQL_C_GUID\n");
			SQLR_ParseGuid((SQLGUID *)targetvalue,
						field,fieldlength);
			break;
		case SQL_C_INTERVAL_YEAR:
		case SQL_C_INTERVAL_MONTH:
		case SQL_C_INTERVAL_DAY:
		case SQL_C_INTERVAL_HOUR:
		case SQL_C_INTERVAL_MINUTE:
		case SQL_C_INTERVAL_SECOND:
		case SQL_C_INTERVAL_YEAR_TO_MONTH:
		case SQL_C_INTERVAL_DAY_TO_HOUR:
		case SQL_C_INTERVAL_DAY_TO_MINUTE:
		case SQL_C_INTERVAL_DAY_TO_SECOND:
		case SQL_C_INTERVAL_HOUR_TO_MINUTE:
		case SQL_C_INTERVAL_HOUR_TO_SECOND:
		case SQL_C_INTERVAL_MINUTE_TO_SECOND:
			debugPrintf("  targettype: SQL_C_INTERVAL_XXX\n");
			SQLR_ParseInterval((SQL_INTERVAL_STRUCT *)
						targetvalue,
						field,fieldlength);
			break;
		default:
			debugPrintf("  invalid targettype\n");
			return SQL_ERROR;
	}

	debugPrintf("  strlen_or_ind: %d\n",(strlen_or_ind)?*strlen_or_ind:0);

	return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLGetData(SQLHSTMT statementhandle,
					SQLUSMALLINT columnnumber,
					SQLSMALLINT targettype,
					SQLPOINTER targetvalue,
					SQLLEN bufferlength,
					SQLLEN *strlen_or_ind) {
	debugFunction();
	return SQLR_SQLGetData(statementhandle,columnnumber,
				targettype,targetvalue,bufferlength,
				strlen_or_ind);
}

SQLRETURN SQL_API SQLGetDescField(SQLHDESC DescriptorHandle,
					SQLSMALLINT RecNumber,
					SQLSMALLINT FieldIdentifier,
					SQLPOINTER Value,
					SQLINTEGER BufferLength,
					SQLINTEGER *StringLength) {
	debugFunction();
	// not supported
	return SQL_ERROR;
}

SQLRETURN SQL_API SQLGetDescRec(SQLHDESC DescriptorHandle,
					SQLSMALLINT RecNumber,
					SQLCHAR *Name,
					SQLSMALLINT BufferLength,
					SQLSMALLINT *StringLength,
					SQLSMALLINT *Type,
					SQLSMALLINT *SubType,
					SQLLEN *Length,
					SQLSMALLINT *Precision,
					SQLSMALLINT *Scale,
					SQLSMALLINT *Nullable) {
	debugFunction();
	// not supported
	return SQL_ERROR;
}

static const char *odbc3states[]={
	"01S00","01S01","01S02","01S06","01S07","07S01","08S01",
	"21S01","21S02","25S01","25S02","25S03",
	"42S01","42S02","42S11","42S12","42S21","42S22",
	"HY095","HY097","HY098","HY099","HY100","HY101","HY105",
	"HY107","HY109","HY110","HY111","HYT00","HYT01",
	"IM001","IM002","IM003","IM004","IM005","IM006","IM007",
	"IM008","IM010","IM011","IM012",NULL
};

SQLRETURN SQL_API SQLGetDiagField(SQLSMALLINT handletype,
					SQLHANDLE handle,
					SQLSMALLINT recnumber,
					SQLSMALLINT diagidentifier,
					SQLPOINTER diaginfo,
					SQLSMALLINT bufferlength,
					SQLSMALLINT *stringlength) {
	debugFunction();

	debugPrintf("  recnumber: %d\n",(int)recnumber);

	// SQL Relay doesn't have more than 1 error record
	if (recnumber>1) {
		return SQL_NO_DATA;
	}

	switch (handletype) {
		case SQL_HANDLE_ENV:
			{
			debugPrintf("  handletype: SQL_HANDLE_ENV\n");
			debugPrintf("  diagidentifier: %d\n",diagidentifier);
			ENV	*env=(ENV *)handle;
			if (handle==SQL_NULL_HENV || !env) {
				debugPrintf("  NULL env handle\n");
				return SQL_INVALID_HANDLE;
			}
			// nothing currently supported
			debugPrintf("  diagidentifier: %d\n",diagidentifier);
			return SQL_NO_DATA;
			}
		case SQL_HANDLE_DBC:
			{
			debugPrintf("  handletype: SQL_HANDLE_DBC\n");

			// invalid handle...
			CONN	*conn=(CONN *)handle;
			if (handle==SQL_NULL_HSTMT || !conn) {
				debugPrintf("  NULL conn handle\n");
				return SQL_INVALID_HANDLE;
			}

			// get the requested data
			const char	*di=NULL;
			switch (diagidentifier) {
				case SQL_DIAG_CLASS_ORIGIN:
					debugPrintf("  diagidentifier: "
						"SQL_DIAG_CLASS_ORIGIN\n");
					if (!charstring::compare(
						conn->sqlstate,"IM",2)) {
						di="ODBC 3.0";
					} else {
						di="ISO 9075";
					}
					break;
				case SQL_DIAG_SUBCLASS_ORIGIN:
					debugPrintf("  diagidentifier: "
						"SQL_DIAG_SUBCLASS_ORIGIN\n");
					if (charstring::inSet(
							conn->sqlstate,
							odbc3states)) {
						di="ODBC 3.0";
					} else {
						di="ISO 9075";
					}
					break;
				case SQL_DIAG_CONNECTION_NAME:
					debugPrintf("  diagidentifier: "
						"SQL_DIAG_CONNECTION_NAME\n");
					// return the server name for this too
					di=conn->server;
					break;
				case SQL_DIAG_SERVER_NAME:
					debugPrintf("  diagidentifier: "
						"SQL_DIAG_SERVER_NAME\n");
					di=conn->server;
					break;
				default:
					// anything else is not supported
					debugPrintf("  diagidentifier: %d\n",
								diagidentifier);
					return SQL_NO_DATA;
			}

			// copy out the data
			charstring::copy((char *)diaginfo,di);

			debugPrintf("  diaginfo: %s\n",(char *)diaginfo);

			return SQL_SUCCESS;
			}
		case SQL_HANDLE_STMT:
			{
			debugPrintf("  handletype: SQL_HANDLE_STMT\n");
			STMT	*stmt=(STMT *)handle;
			if (handle==SQL_NULL_HSTMT || !stmt) {
				debugPrintf("  NULL stmt handle\n");
				return SQL_INVALID_HANDLE;
			}
			if (diagidentifier==SQL_DIAG_ROW_COUNT) {
				debugPrintf("  diagidentifier: "
						"SQL_DIAG_ROW_COUNT\n");
				*(SQLLEN *)diaginfo=stmt->cur->affectedRows();
				return SQL_SUCCESS;
			}
			// anything else is not supported
			return SQL_NO_DATA;
			}
		case SQL_HANDLE_DESC:
			debugPrintf("  handletype: SQL_HANDLE_DESC\n");
			debugPrintf("  diagidentifier: %d\n",diagidentifier);
			// not supported
			return SQL_NO_DATA;
	}
	debugPrintf("  invalid handletype\n");
	return SQL_ERROR;
}

static SQLRETURN SQLR_SQLGetDiagRec(SQLSMALLINT handletype,
					SQLHANDLE handle,
					SQLSMALLINT recnumber,
					SQLCHAR *sqlstate,
					SQLINTEGER *nativeerror,
					SQLCHAR *messagetext,
					SQLSMALLINT bufferlength,
					SQLSMALLINT *textlength) {
	debugFunction();

	debugPrintf("  recnumber: %d\n",(int)recnumber);

	// SQL Relay doesn't have more than 1 error record
	if (recnumber>1) {
		return SQL_NO_DATA;
	}

	// initialize error and sqlstate
	const char	*error=NULL;
	const char	*sqlst=NULL;
	SQLINTEGER	errn=0;

	switch (handletype) {
		case SQL_HANDLE_ENV:
			{
			debugPrintf("  handletype: SQL_HANDLE_ENV\n");
			ENV	*env=(ENV *)handle;
			if (handle==SQL_NULL_HSTMT || !env) {
				debugPrintf("  NULL env handle\n");
				return SQL_INVALID_HANDLE;
			}
			error=env->error;
			errn=env->errn;
			sqlst=env->sqlstate;
			}
			break;
		case SQL_HANDLE_DBC:
			{
			debugPrintf("  handletype: SQL_HANDLE_DBC\n");
			CONN	*conn=(CONN *)handle;
			if (handle==SQL_NULL_HSTMT || !conn) {
				debugPrintf("  NULL conn handle\n");
				return SQL_INVALID_HANDLE;
			}
			error=conn->error;
			errn=conn->errn;
			sqlst=conn->sqlstate;
			}
			break;
		case SQL_HANDLE_STMT:
			{
			debugPrintf("  handletype: SQL_HANDLE_STMT\n");
			STMT	*stmt=(STMT *)handle;
			if (handle==SQL_NULL_HSTMT || !stmt) {
				debugPrintf("  NULL stmt handle\n");
				return SQL_INVALID_HANDLE;
			}
			error=stmt->error;
			errn=stmt->errn;
			sqlst=stmt->sqlstate;
			}
			break;
		case SQL_HANDLE_DESC:
			debugPrintf("  handletype: SQL_HANDLE_DESC\n");
			// not supported
			return SQL_ERROR;
		default:
			debugPrintf("  invalid handletype\n");
			return SQL_ERROR;
	}

	// finagle sqlst
	if (!sqlst || !sqlst[0]) {
		if (error && error[0]) {
			// General error
			sqlst="HY000";
		} else {
			// success
			sqlst="00000";
		}
	}

	// copy out the data
	charstring::safeCopy((char *)messagetext,(size_t)bufferlength,error);
	*textlength=charstring::length(error);
	if (*textlength>bufferlength) {
		*textlength=bufferlength;
	}
	if (nativeerror) {
		*nativeerror=errn;
	}
	charstring::copy((char *)sqlstate,sqlst);

	debugPrintf("  sqlstate: %s\n",(sqlst)?sqlst:"");
	debugPrintf("  nativeerror: %lld\n",(int64_t)errn);
	debugPrintf("  messagetext: %s\n",(error)?error:"");
	debugPrintf("  bufferlength: %d\n",bufferlength);
	debugPrintf("  textlength: %d\n",*textlength);

	return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLGetDiagRec(SQLSMALLINT handletype,
					SQLHANDLE handle,
					SQLSMALLINT recnumber,
					SQLCHAR *sqlstate,
					SQLINTEGER *nativeerror,
					SQLCHAR *messagetext,
					SQLSMALLINT bufferlength,
					SQLSMALLINT *textlength) {
	debugFunction();
	return SQLR_SQLGetDiagRec(handletype,handle,recnumber,sqlstate,
					nativeerror,messagetext,bufferlength,
					textlength);
}

SQLRETURN SQL_API SQLGetEnvAttr(SQLHENV environmenthandle,
					SQLINTEGER attribute,
					SQLPOINTER value,
					SQLINTEGER bufferlength,
					SQLINTEGER *stringlength) {
	debugFunction();

	ENV	*env=(ENV *)environmenthandle;
	if (environmenthandle==SQL_NULL_HENV || !env) {
		debugPrintf("  NULL env handle\n");
		return SQL_INVALID_HANDLE;
	}

	switch (attribute) {
		case SQL_ATTR_OUTPUT_NTS:
			debugPrintf("  attribute: SQL_ATTR_OUTPUT_NTS\n");
			// this one is hardcoded to true
			// and can't be set to false
			*((SQLINTEGER *)value)=SQL_TRUE;
			break;
		case SQL_ATTR_ODBC_VERSION:
			debugPrintf("  attribute: SQL_ATTR_ODBC_VERSION\n");
			*((SQLINTEGER *)value)=env->odbcversion;
			debugPrintf("    odbcversion: %d\n",
						(int)env->odbcversion);
			break;
		case SQL_ATTR_CONNECTION_POOLING:
			debugPrintf("  attribute: SQL_ATTR_CONNECTION_POOLING\n");
			// this one is hardcoded to "off"
			// and can't be changed
			*((SQLUINTEGER *)value)=SQL_CP_OFF;
			break;
		case SQL_ATTR_CP_MATCH:
			debugPrintf("  attribute: SQL_ATTR_CP_MATCH\n");
			// this one is hardcoded to "default"
			// and can't be changed
			*((SQLUINTEGER *)value)=SQL_CP_MATCH_DEFAULT;
			break;
		default:
			debugPrintf("  unsupported attribute: %d\n",attribute);
			break;
	}
	return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLR_SQLGetFunctions(SQLHDBC connectionhandle,
					SQLUSMALLINT functionid,
					SQLUSMALLINT *supported) {
	debugFunction();

	CONN	*conn=(CONN *)connectionhandle;
	if (connectionhandle==SQL_NULL_HANDLE || !conn || !conn->con) {
		debugPrintf("  NULL conn handle\n");
		return SQL_INVALID_HANDLE;
	}

	switch (functionid) {
		case SQL_API_ALL_FUNCTIONS:
			debugPrintf("  functionid: "
				"SQL_API_ALL_FUNCTIONS "
				"- true\n");

			for (uint16_t i=0; i<100; i++) {
				if (i==SQL_API_ALL_FUNCTIONS
					#if (ODBCVER >= 0x0300)
					|| i==SQL_API_ODBC3_ALL_FUNCTIONS
					#endif
					) {
					supported[i]=SQL_TRUE;
				} else {
					SQLR_SQLGetFunctions(
							connectionhandle,
							i,&supported[i]);
				}
			}

			// clear any error that might have been set during
			// the recursive call
			SQLR_CONNClearError(conn);

			break;
		case SQL_API_SQLALLOCCONNECT:
			debugPrintf("  functionid: "
				"SQL_API_SQLALLOCCONNECT "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		case SQL_API_SQLALLOCENV:
			debugPrintf("  functionid: "
				"SQL_API_SQLALLOCENV "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		#if (ODBCVER >= 0x0300)
		case SQL_API_SQLALLOCHANDLE:
			debugPrintf("  functionid: "
				"SQL_API_SQLALLOCHANDLE "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		#endif
		case SQL_API_SQLALLOCSTMT:
			debugPrintf("  functionid: "
				"SQL_API_SQLALLOCSTMT "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		case SQL_API_SQLBINDCOL:
			debugPrintf("  functionid: "
				"SQL_API_SQLBINDCOL "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		#if (ODBCVER >= 0x0300)
		case SQL_API_SQLBINDPARAM:
			debugPrintf("  functionid: "
				"SQL_API_SQLBINDPARAM "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		case SQL_API_SQLCLOSECURSOR:
			debugPrintf("  functionid: "
				"SQL_API_SQLCLOSECURSOR "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		case SQL_API_SQLCOLATTRIBUTE:
		//case SQL_API_SQLCOLATTRIBUTES:
			debugPrintf("  functionid: "
				"SQL_API_SQLCOLATTRIBUTE "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		#endif
		case SQL_API_SQLCONNECT:
			debugPrintf("  functionid: "
				"SQL_API_SQLCONNECT "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		#if (ODBCVER >= 0x0300)
		case SQL_API_SQLCOPYDESC:
			debugPrintf("  functionid: "
				"SQL_API_SQLCOPYDESC "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		#endif
		case SQL_API_SQLDESCRIBECOL:
			debugPrintf("  functionid: "
				"SQL_API_SQLDESCRIBECOL "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		case SQL_API_SQLDISCONNECT:
			debugPrintf("  functionid: "
				"SQL_API_SQLDISCONNECT "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		#if (ODBCVER >= 0x0300)
		case SQL_API_SQLENDTRAN:
			debugPrintf("  functionid: "
				"SQL_API_SQLENDTRAN "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		#endif
		case SQL_API_SQLERROR:
			debugPrintf("  functionid: "
				"SQL_API_SQLERROR "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		case SQL_API_SQLEXECDIRECT:
			debugPrintf("  functionid: "
				"SQL_API_SQLEXECDIRECT "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		case SQL_API_SQLEXECUTE:
			debugPrintf("  functionid: "
				"SQL_API_SQLEXECUTE "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		case SQL_API_SQLFETCH:
			debugPrintf("  functionid: "
				"SQL_API_SQLFETCH "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		#if (ODBCVER >= 0x0300)
		case SQL_API_SQLFETCHSCROLL:
			debugPrintf("  functionid: "
				"SQL_API_SQLFETCHSCROLL "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		#endif
		case SQL_API_SQLFREECONNECT:
			debugPrintf("  functionid: "
				"SQL_API_SQLFREECONNECT "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		case SQL_API_SQLFREEENV:
			debugPrintf("  functionid: "
				"SQL_API_SQLFREEENV "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		#if (ODBCVER >= 0x0300)
		case SQL_API_SQLFREEHANDLE:
			debugPrintf("  functionid: "
				"SQL_API_SQLFREEHANDLE "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		#endif
		case SQL_API_SQLFREESTMT:
			debugPrintf("  functionid: "
				"SQL_API_SQLFREESTMT "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		case SQL_API_SQLGETCONNECTOPTION:
			debugPrintf("  functionid: "
				"SQL_API_SQLGETCONNECTOPTION "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		case SQL_API_SQLGETCURSORNAME:
			debugPrintf("  functionid: "
				"SQL_API_SQLGETCURSORNAME "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		case SQL_API_SQLGETDATA:
			debugPrintf("  functionid: "
				"SQL_API_SQLGETDATA "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		#if (ODBCVER >= 0x0300)
		case SQL_API_SQLGETDIAGFIELD:
			debugPrintf("  functionid: "
				"SQL_API_SQLGETDIAGFIELD "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		case SQL_API_SQLGETDIAGREC:
			debugPrintf("  functionid: "
				"SQL_API_SQLGETDIAGREC "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		case SQL_API_SQLGETENVATTR:
			debugPrintf("  functionid: "
				"SQL_API_SQLGETENVATTR "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		#endif
		case SQL_API_SQLGETFUNCTIONS:
			debugPrintf("  functionid: "
				"SQL_API_SQLGETFUNCTIONS "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		case SQL_API_SQLGETINFO:
			debugPrintf("  functionid: "
				"SQL_API_SQLGETINFO "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		#if (ODBCVER >= 0x0300)
		case SQL_API_SQLGETSTMTATTR:
			debugPrintf("  functionid: "
				"SQL_API_SQLGETSTMTATTR "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		#endif
		case SQL_API_SQLGETSTMTOPTION:
			debugPrintf("  functionid: "
				"SQL_API_SQLGETSTMTOPTION "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		case SQL_API_SQLGETTYPEINFO:
			debugPrintf("  functionid: "
				"SQL_API_SQLGETTYPEINFO "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		case SQL_API_SQLNUMRESULTCOLS:
			debugPrintf("  functionid: "
				"SQL_API_SQLNUMRESULTCOLS "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		case SQL_API_SQLPREPARE:
			debugPrintf("  functionid: "
				"SQL_API_SQLPREPARE "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		case SQL_API_SQLROWCOUNT:
			debugPrintf("  functionid: "
				"SQL_API_SQLROWCOUNT "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		#if (ODBCVER >= 0x0300)
		case SQL_API_SQLSETCONNECTATTR:
			debugPrintf("  functionid: "
				"SQL_API_SQLSETCONNECTATTR "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		#endif
		case SQL_API_SQLSETCONNECTOPTION:
			debugPrintf("  functionid: "
				"SQL_API_SQLSETCONNECTOPTION "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		case SQL_API_SQLSETCURSORNAME:
			debugPrintf("  functionid: "
				"SQL_API_SQLSETCURSORNAME "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		#if (ODBCVER >= 0x0300)
		case SQL_API_SQLSETENVATTR:
			debugPrintf("  functionid: "
				"SQL_API_SQLSETENVATTR "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		#endif
		case SQL_API_SQLSETPARAM:
			debugPrintf("  functionid: "
				"SQL_API_SQLSETPARAM "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		#if (ODBCVER >= 0x0300)
		case SQL_API_SQLSETSTMTATTR:
			debugPrintf("  functionid: "
				"SQL_API_SQLSETSTMTATTR "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		#endif
		case SQL_API_SQLSETSTMTOPTION:
			debugPrintf("  functionid: "
				"SQL_API_SQLSETSTMTOPTION "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		case SQL_API_SQLTRANSACT:
			debugPrintf("  functionid: "
				"SQL_API_SQLTRANSACT "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		#if (ODBCVER >= 0x0300)
		case SQL_API_SQLALLOCHANDLESTD:
			debugPrintf("  functionid: "
				"SQL_API_SQLALLOCHANDLESTD "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		#endif
		case SQL_API_SQLBINDPARAMETER:
			debugPrintf("  functionid: "
				"SQL_API_SQLBINDPARAMETER "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		case SQL_API_SQLBROWSECONNECT:
			debugPrintf("  functionid: "
				"SQL_API_SQLBROWSECONNECT "
				"- false\n");
			*supported=SQL_FALSE;
			break;
		case SQL_API_SQLDRIVERCONNECT:
			debugPrintf("  functionid: "
				"SQL_API_SQLDRIVERCONNECT "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		case SQL_API_SQLEXTENDEDFETCH:
			debugPrintf("  functionid: "
				"SQL_API_SQLEXTENDEDFETCH "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		case SQL_API_SQLMORERESULTS:
			debugPrintf("  functionid: "
				"SQL_API_SQLMORERESULTS "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		case SQL_API_SQLNUMPARAMS:
			debugPrintf("  functionid: "
				"SQL_API_SQLNUMPARAMS "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		case SQL_API_SQLPARAMOPTIONS:
			debugPrintf("  functionid: "
				"SQL_API_SQLPARAMOPTIONS "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		case SQL_API_SQLSETPOS:
			debugPrintf("  functionid: "
				"SQL_API_SQLSETPOS "
				"- false\n");
			// FIXME: this is implemented, sort-of...
			*supported=SQL_FALSE;
			break;
		case SQL_API_SQLSETSCROLLOPTIONS:
			debugPrintf("  functionid: "
				"SQL_API_SQLSETSCROLLOPTIONS "
				"- false\n");
			*supported=SQL_FALSE;
			break;
		case SQL_API_SQLCANCEL:
			debugPrintf("  functionid: "
				"SQL_API_SQLCANCEL "
				"- false\n");
			*supported=SQL_FALSE;
			break;
		case SQL_API_SQLCOLUMNS:
			debugPrintf("  functionid: "
				"SQL_API_SQLCOLUMNS "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		case SQL_API_SQLDATASOURCES:
			debugPrintf("  functionid: "
				"SQL_API_SQLDATASOURCES "
				"- false\n");
			*supported=SQL_FALSE;
			break;
		case SQL_API_SQLPUTDATA:
			debugPrintf("  functionid: "
				"SQL_API_SQLPUTDATA "
				"- false\n");
			*supported=SQL_FALSE;
			break;
		case SQL_API_SQLPARAMDATA:
			debugPrintf("  functionid: "
				"SQL_API_SQLPARAMDATA "
				"- false\n");
			*supported=SQL_FALSE;
			break;
		#if (ODBCVER >= 0x0300)
		case SQL_API_SQLGETCONNECTATTR:
			debugPrintf("  functionid: "
				"SQL_API_SQLGETCONNECTATTR "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		#endif
		case SQL_API_SQLSPECIALCOLUMNS:
			debugPrintf("  functionid: "
				"SQL_API_SQLSPECIALCOLUMNS "
				"- false\n");
			*supported=SQL_FALSE;
			break;
		case SQL_API_SQLSTATISTICS:
			debugPrintf("  functionid: "
				"SQL_API_SQLSTATISTICS "
				"- false\n");
			*supported=SQL_FALSE;
			break;
		case SQL_API_SQLTABLES:
			debugPrintf("  functionid: "
				"SQL_API_SQLTABLES "
				"- true\n");
			*supported=SQL_TRUE;
			break;
		#if (ODBCVER >= 0x0300)
		case SQL_API_SQLBULKOPERATIONS:
			debugPrintf("  functionid: "
				"SQL_API_SQLBULKOPERATIONS "
				"- false\n");
			*supported=SQL_FALSE;
			break;
		#endif
		case SQL_API_SQLCOLUMNPRIVILEGES:
			debugPrintf("  functionid: "
				"SQL_API_SQLCOLUMNPRIVILEGES "
				"- false\n");
			*supported=SQL_FALSE;
			break;
		case SQL_API_SQLDESCRIBEPARAM:
			debugPrintf("  functionid: "
				"SQL_API_SQLDESCRIBEPARAM "
				"- false\n");
			*supported=SQL_FALSE;
			break;
		case SQL_API_SQLFOREIGNKEYS:
			debugPrintf("  functionid: "
				"SQL_API_SQLFOREIGNKEYS "
				"- false\n");
			*supported=SQL_FALSE;
			break;
		case SQL_API_SQLNATIVESQL:
			debugPrintf("  functionid: "
				"SQL_API_SQLNATIVESQL "
				"- false\n");
			*supported=SQL_FALSE;
			break;
		case SQL_API_SQLPRIMARYKEYS:
			debugPrintf("  functionid: "
				"SQL_API_SQLPRIMARYKEYS "
				"- false\n");
			*supported=SQL_FALSE;
			break;
		case SQL_API_SQLPROCEDURECOLUMNS:
			debugPrintf("  functionid: "
				"SQL_API_SQLPROCEDURECOLUMNS "
				"- false\n");
			*supported=SQL_FALSE;
			break;
		case SQL_API_SQLPROCEDURES:
			debugPrintf("  functionid: "
				"SQL_API_SQLPROCEDURES "
				"- false\n");
			*supported=SQL_FALSE;
			break;
		case SQL_API_SQLTABLEPRIVILEGES:
			debugPrintf("  functionid: "
				"SQL_API_SQLTABLEPRIVILEGES "
				"- false\n");
			*supported=SQL_FALSE;
			break;
		case SQL_API_SQLDRIVERS:
			debugPrintf("  functionid: "
				"SQL_API_SQLDRIVERS "
				"- false\n");
			*supported=SQL_FALSE;
			break;
		#if (ODBCVER >= 0x0300)
		case SQL_API_SQLGETDESCFIELD:
			debugPrintf("  functionid: "
				"SQL_API_SQLGETDESCFIELD "
				"- false\n");
			*supported=SQL_FALSE;
			break;
		case SQL_API_SQLGETDESCREC:
			debugPrintf("  functionid: "
				"SQL_API_SQLGETDESCREC "
				"- false\n");
			*supported=SQL_FALSE;
			break;
		case SQL_API_SQLSETDESCFIELD:
			debugPrintf("  functionid: "
				"SQL_API_SQLSETDESCFIELD "
				"- false\n");
			*supported=SQL_FALSE;
			break;
		case SQL_API_SQLSETDESCREC:
			debugPrintf("  functionid: "
				"SQL_API_SQLSETDESCREC "
				"- false\n");
			*supported=SQL_FALSE;
			break;
		case SQL_API_ODBC3_ALL_FUNCTIONS:
			debugPrintf("  functionid: "
				"SQL_API_ODBC3_ALL_FUNCTIONS "
				"- true\n");

			// populate the bitmap...
			for (uint16_t i=0;
				i<SQL_API_ODBC3_ALL_FUNCTIONS_SIZE*16; i++) {

				// determine the bitmap element
				// and position within the element
				uint16_t	element=i/16;
				uint16_t	position=i%16;

				// init the bitmap element
				if (!position) {
					supported[element]=0;
				}

				// is this function supported?
				SQLUSMALLINT	sup=SQL_FALSE;
				if (i==SQL_API_ALL_FUNCTIONS ||
					i==SQL_API_ODBC3_ALL_FUNCTIONS) {
					sup=SQL_TRUE;
				} else {
					SQLR_SQLGetFunctions(
							connectionhandle,
							i,&sup);
				}

				// update the bitmap
				supported[element]|=sup<<position;

				// debug...
				debugPrintf("%d(%d:%d) = %d  ",
						i,element,position,sup);
				debugPrintf("(%d = ",element);
				debugPrintBits((uint16_t)supported[element]);
				debugPrintf(")\n");
			}

			// clear any error that might have been set during
			// the recursive call
			SQLR_CONNClearError(conn);
			break;
		#endif
		#if (ODBCVER >= 0x0380)
		case SQL_API_SQLCANCELHANDLE:
			debugPrintf("  functionid: "
				"SQL_API_SQLCANCELHANDLE "
				"- false\n");
			*supported=SQL_FALSE;
			break;
		#endif
		default:
			debugPrintf("  invalid functionid: %d\n",functionid);
			*supported=SQL_FALSE;
			SQLR_CONNSetError(conn,"Function type out of range",
								0,"HY095");
			return SQL_ERROR;
	}

	return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLGetFunctions(SQLHDBC connectionhandle,
					SQLUSMALLINT functionid,
					SQLUSMALLINT *supported) {
	debugFunction();
	return SQLR_SQLGetFunctions(connectionhandle,functionid,supported);
}

SQLRETURN SQL_API SQLGetInfo(SQLHDBC connectionhandle,
					SQLUSMALLINT infotype,
					SQLPOINTER infovalue,
					SQLSMALLINT bufferlength,
					SQLSMALLINT *stringlength) {
	debugFunction();

	// some bits of info need a valid conn handle, but others don't
	CONN	*conn=(CONN *)connectionhandle;
	if ((connectionhandle==SQL_NULL_HANDLE || !conn || !conn->con) &&
		(infotype==SQL_DATA_SOURCE_NAME ||
			infotype==SQL_SERVER_NAME ||
			infotype==SQL_DRIVER_VER ||
			infotype==SQL_DBMS_NAME ||
			infotype==SQL_DBMS_VER ||
			infotype==SQL_ODBC_VER ||
			infotype==SQL_DATABASE_NAME ||
			infotype==SQL_USER_NAME)) {
		debugPrintf("  NULL conn handle\n");
		return SQL_INVALID_HANDLE;
	}

	const char	*strval=NULL;

	switch (infotype) {
		case SQL_ACTIVE_CONNECTIONS:
			// aka SQL_MAX_DRIVER_CONNECTIONS
			// aka SQL_MAXIMUM_DRIVER_CONNECTIONS
			debugPrintf("  infotype: "
					"SQL_ACTIVE_CONNECTIONS/"
					"SQL_MAX_DRIVER_CONNECTIONS/"
					"SQL_MAXIMUM_DRIVER_CONNECTIONS\n");
			// 0 means no max or unknown
			*(SQLUSMALLINT *)infovalue=0;
			break;
		case SQL_ACTIVE_STATEMENTS:
			// aka SQL_MAX_CONCURRENT_ACTIVITIES 
			// aka SQL_MAXIMUM_CONCURRENT_ACTIVITIES
			debugPrintf("  infotype: "
					"SQL_ACTIVE_STATEMENTS/"
					"SQL_MAX_CONCURRENT_ACTIVITIES/"
					"SQL_MAXIMUM_CONCURRENT_ACTIVITIES\n");
			// 0 means no max or unknown
			*(SQLUSMALLINT *)infovalue=0;
			break;
		case SQL_DATA_SOURCE_NAME:
			debugPrintf("  infotype: "
					"SQL_DATA_SOURCE_NAME\n");
			strval=conn->dsn;
			break;
		case SQL_FETCH_DIRECTION:
			debugPrintf("  infotype: "
					"SQL_FETCH_DIRECTION\n");
			// FIXME: for now...
			*(SQLINTEGER *)infovalue=SQL_FD_FETCH_NEXT;
			break;
		case SQL_SERVER_NAME:
			debugPrintf("  infotype: "
					"SQL_SERVER_NAME\n");
			strval=conn->server;
			break;
		case SQL_SEARCH_PATTERN_ESCAPE:
			debugPrintf("  infotype: "
					"SQL_SEARCH_PATTERN_ESCAPE\n");
			// FIXME: what about _ and ?
			strval="%";
			break;
		case SQL_DATABASE_NAME:
			debugPrintf("  infotype: "
					"SQL_DATABASE_NAME\n");
			strval=conn->con->getCurrentDatabase();
			break;
		case SQL_DBMS_NAME:
			debugPrintf("  infotype: "
					"SQL_DBMS_NAME\n");
			strval=conn->con->identify();
			break;
		case SQL_DBMS_VER:
			debugPrintf("  infotype: "
					"SQL_DBMS_VER\n");
			strval=conn->con->dbVersion();
			break;
		case SQL_ACCESSIBLE_TABLES:
			debugPrintf("  infotype: "
					"SQL_ACCESSIBLE_TABLES\n");
			strval="N";
			break;
		case SQL_ACCESSIBLE_PROCEDURES:
			debugPrintf("  infotype: "
					"SQL_ACCESSIBLE_PROCEDURES\n");
			strval="N";
			break;
		case SQL_CURSOR_COMMIT_BEHAVIOR:
			debugPrintf("  infotype: "
					"SQL_CURSOR_COMMIT_BEHAVIOR\n");
			// FIXME: is this true for all db's?
			*(SQLUINTEGER *)infovalue=SQL_CB_CLOSE;
			break;
		case SQL_DATA_SOURCE_READ_ONLY:
			debugPrintf("  infotype: "
					"SQL_DATA_SOURCE_READ_ONLY\n");
			// FIXME: this isn't always true
			strval="N";
			break;
		case SQL_DEFAULT_TXN_ISOLATION:
			debugPrintf("  infotype: "
					"SQL_DEFAULT_TXN_ISOLATION\n");
			// FIXME: this isn't always true, especially for mysql
			*(SQLUINTEGER *)infovalue=SQL_TXN_READ_COMMITTED;
			break;
		case SQL_IDENTIFIER_CASE:
			debugPrintf("  infotype: "
					"SQL_IDENTIFIER_CASE\n");
			// FIXME: this isn't true for all db's
			*(SQLUINTEGER *)infovalue=SQL_IC_MIXED;
			break;
		case SQL_IDENTIFIER_QUOTE_CHAR:
			debugPrintf("  infotype: "
					"SQL_IDENTIFIER_QUOTE_CHAR\n");
			// FIXME: is this true for all db's?
			strval="\"";
			break;
		case SQL_MAX_COLUMN_NAME_LEN:
			// aka SQL_MAXIMUM_COLUMN_NAME_LENGTH
			debugPrintf("  infotype: "
					"SQL_MAX_COLUMN_NAME_LEN/"
					"SQL_MAXIMUM_COLUMN_NAME_LEN\n");
			// 0 means no max or unknown
			*(SQLUINTEGER *)infovalue=0;
			break;
		case SQL_MAX_CURSOR_NAME_LEN:
			// aka SQL_MAXIMUM_CURSOR_NAME_LENGTH
			debugPrintf("  infotype: "
					"SQL_MAX_CURSOR_NAME_LEN/"
					"SQL_MAXIMUM_CURSOR_NAME_LEN\n");
			// 0 means no max or unknown
			*(SQLUINTEGER *)infovalue=0;
			break;
		case SQL_MAX_OWNER_NAME_LEN:
			// aka SQL_MAX_SCHEMA_NAME_LEN
			// aka SQL_MAXIMUM_SCHEMA_NAME_LENGTH
			debugPrintf("  infotype: "
					"SQL_MAX_OWNER_NAME_LEN/"
					"SQL_MAX_SCHEMA_NAME_LEN/"
					"SQL_MAXIMUM_SCHEMA_NAME_LEN\n");
			// 0 means no max or unknown
			*(SQLUINTEGER *)infovalue=0;
			break;
		case SQL_MAX_CATALOG_NAME_LEN:
			// aka SQL_MAXIMUM_CATALOG_NAME_LENGTH
			debugPrintf("  infotype: "
					"SQL_MAX_CATALOG_NAME_LEN\n");
			// 0 means no max or unknown
			*(SQLUINTEGER *)infovalue=0;
			break;
		case SQL_MAX_TABLE_NAME_LEN:
			debugPrintf("  infotype: "
					"SQL_MAX_TABLE_NAME_LEN\n");
			// 0 means no max or unknown
			*(SQLUINTEGER *)infovalue=0;
			break;
		case SQL_SCROLL_CONCURRENCY:
			debugPrintf("  infotype: "
					"SQL_SCROLL_CONCURRENCY\n");
			*(SQLINTEGER *)infovalue=SQL_SCCO_READ_ONLY;
			break;
		case SQL_TXN_CAPABLE:
			// aka SQL_TRANSACTION_CAPABLE
			debugPrintf("  infotype: "
					"SQL_TXN_CAPABLE\n");
			// FIXME: this isn't true for all db's
			*(SQLUSMALLINT *)infovalue=SQL_TC_ALL;
			break;
		case SQL_USER_NAME:
			debugPrintf("  infotype: "
					"SQL_USER_NAME\n");
			strval=conn->user;
			break;
		case SQL_TXN_ISOLATION_OPTION:
			// aka SQL_TRANSACTION_ISOLATION_OPTION
			debugPrintf("  infotype: "
					"SQL_TXN_ISOLATION_OPTION/"
					"SQL_TRANSACTION_ISOLATION_OPTION\n");
			// FIXME: this isn't true for all db's
			*(SQLUINTEGER *)infovalue=
					SQL_TXN_READ_UNCOMMITTED|
					SQL_TXN_READ_COMMITTED|
					SQL_TXN_REPEATABLE_READ|
					SQL_TXN_SERIALIZABLE;
			break;
		case SQL_INTEGRITY:
			// aka SQL_ODBC_SQL_OPT_IEF
			debugPrintf("  infotype: "
					"SQL_INTEGRITY/"
					"SQL_ODBC_SQL_OPT_IEF\n");
			// FIXME: this isn't true for all db's
			strval="Y";
			break;
		case SQL_GETDATA_EXTENSIONS:
			debugPrintf("  infotype: "
					"SQL_GETDATA_EXTENSIONS\n");
			*(SQLUINTEGER *)infovalue=SQL_GD_BLOCK;
			break;
		case SQL_NULL_COLLATION:
			debugPrintf("  infotype: "
					"SQL_NULL_COLLATION\n");
			// FIXME: is this true for all db's?
			*(SQLUSMALLINT *)infovalue=SQL_NC_LOW;
			break;
		case SQL_ALTER_TABLE:
			debugPrintf("  infotype: "
					"SQL_ALTER_TABLE\n");
			// FIXME: this isn't true for all db's
			*(SQLUINTEGER *)infovalue=0
					#if (ODBCVER >= 0x0200)
					|SQL_AT_ADD_COLUMN
					|SQL_AT_DROP_COLUMN
					#endif
					#if (ODBCVER >= 0x0300)
					|SQL_AT_ADD_COLUMN_SINGLE
					|SQL_AT_ADD_COLUMN_DEFAULT
					|SQL_AT_ADD_COLUMN_COLLATION
					|SQL_AT_SET_COLUMN_DEFAULT
					|SQL_AT_DROP_COLUMN_DEFAULT
					|SQL_AT_DROP_COLUMN_CASCADE
					|SQL_AT_DROP_COLUMN_RESTRICT
					|SQL_AT_ADD_TABLE_CONSTRAINT
					|SQL_AT_DROP_TABLE_CONSTRAINT_CASCADE
					|SQL_AT_DROP_TABLE_CONSTRAINT_RESTRICT
					|SQL_AT_CONSTRAINT_NAME_DEFINITION
					|SQL_AT_CONSTRAINT_INITIALLY_DEFERRED
					|SQL_AT_CONSTRAINT_INITIALLY_IMMEDIATE
					|SQL_AT_CONSTRAINT_DEFERRABLE
					|SQL_AT_CONSTRAINT_NON_DEFERRABLE
					#endif
					;
			break;
		case SQL_ORDER_BY_COLUMNS_IN_SELECT:
			debugPrintf("  infotype: "
					"SQL_ORDER_BY_COLUMNS_IN_SELECT\n");
			// FIXME: is this true for all db's?
			strval="N";
			break;
		case SQL_SPECIAL_CHARACTERS:
			debugPrintf("  infotype: "
					"SQL_SPECIAL_CHARACTERS\n");
			strval="#$_";
			break;
		case SQL_MAX_COLUMNS_IN_GROUP_BY:
			// aka SQL_MAXIMUM_COLUMNS_IN_GROUP_BY
			debugPrintf("  infotype: "
					"SQL_MAX_COLUMNS_IN_GROUP_BY/"
					"SQL_MAXIMUM_COLUMNS_IN_GROUP_BY\n");
			// 0 means no max or unknown
			*(SQLUSMALLINT *)infovalue=0;
			break;
		case SQL_MAX_COLUMNS_IN_INDEX:
			// aka SQL_MAXIMUM_COLUMNS_IN_INDEX
			debugPrintf("  infotype: "
					"SQL_MAX_COLUMNS_IN_INDEX/"
					"SQL_MAXIMUM_COLUMNS_IN_INDEX\n");
			// 0 means no max or unknown
			*(SQLUSMALLINT *)infovalue=0;
			break;
		case SQL_MAX_COLUMNS_IN_ORDER_BY:
			// aka SQL_MAXIMUM_COLUMNS_IN_ORDER_BY
			debugPrintf("  infotype: "
					"SQL_MAX_COLUMNS_IN_ORDER_BY/"
					"SQL_MAXIMUM_COLUMNS_IN_ORDER_BY\n");
			// 0 means no max or unknown
			*(SQLUSMALLINT *)infovalue=0;
			break;
		case SQL_MAX_COLUMNS_IN_SELECT:
			// aka SQL_MAXIMUM_COLUMNS_IN_SELECT
			debugPrintf("  infotype: "
					"SQL_MAX_COLUMNS_IN_SELECT/"
					"SQL_MAXIMUM_COLUMNS_IN_SELECT\n");
			// 0 means no max or unknown
			*(SQLUSMALLINT *)infovalue=0;
			break;
		case SQL_MAX_COLUMNS_IN_TABLE:
			debugPrintf("  infotype: "
					"SQL_MAX_COLUMNS_IN_TABLE\n");
			// 0 means no max or unknown
			*(SQLUSMALLINT *)infovalue=0;
			break;
		case SQL_MAX_INDEX_SIZE:
			// aka SQL_MAXIMUM_INDEX_SIZE
			debugPrintf("  infotype: "
					"SQL_MAX_INDEX_SIZE/"
					"SQL_MAXIMUM_INDEX_SIZE\n");
			// 0 means no max or unknown
			*(SQLUINTEGER *)infovalue=0;
			break;
		case SQL_MAX_ROW_SIZE:
			// aka SQL_MAXIMUM_ROW_SIZE
			debugPrintf("  infotype: "
					"SQL_MAX_ROW_SIZE/"
					"SQL_MAXIMUM_ROW_SIZE\n");
			// 0 means no max or unknown
			*(SQLUINTEGER *)infovalue=0;
			break;
		case SQL_MAX_STATEMENT_LEN:
			// aka SQL_MAXIMUM_STATEMENT_LENGTH
			debugPrintf("  infotype: "
					"SQL_MAX_STATEMENT_LEN/"
					"SQL_MAXIMUM_STATEMENT_LENGTH\n");
			// 0 means no max or unknown
			*(SQLUINTEGER *)infovalue=0;
			break;
		case SQL_MAX_TABLES_IN_SELECT:
			// aka SQL_MAXIMUM_TABLES_IN_SELECT
			debugPrintf("  infotype: "
					"SQL_MAX_TABLES_IN_SELECT/"
					"SQL_MAXIMUM_TABLES_IN_SELECT\n");
			// 0 means no max or unknown
			*(SQLUSMALLINT *)infovalue=0;
			break;
		case SQL_MAX_USER_NAME_LEN:
			// aka SQL_MAXIMUM_USER_NAME_LENGTH
			debugPrintf("  infotype: "
					"SQL_MAX_USER_NAME_LEN/"
					"SQL_MAXIMUM_USER_NAME_LENGTH\n");
			// 0 means no max or unknown
			*(SQLUSMALLINT *)infovalue=0;
			break;
		#if (ODBCVER >= 0x0300)
		case SQL_OJ_CAPABILITIES:
			// aka SQL_OUTER_JOIN_CAPABILITIES
			debugPrintf("  infotype: "
					"SQL_OJ_CAPABILITIES/"
					"SQL_OUTER_JOIN_CAPABILITIES\n");
			// FIXME: this isn't true for all db's
			*(SQLUINTEGER *)infovalue=
					SQL_OJ_LEFT|
					SQL_OJ_RIGHT|
					SQL_OJ_FULL|
					SQL_OJ_NESTED|
					SQL_OJ_NOT_ORDERED|
					SQL_OJ_INNER|
					SQL_OJ_ALL_COMPARISON_OPS;
			break;
		case SQL_XOPEN_CLI_YEAR:
			debugPrintf("  infotype: "
					"SQL_XOPEN_CLI_YEAR\n");
			// FIXME: what actual year?
			strval="1996";
			break;
		case SQL_CURSOR_SENSITIVITY:
			debugPrintf("  infotype: "
					"SQL_CURSOR_SENSITIVITY\n");
			*(SQLUINTEGER *)infovalue=SQL_UNSPECIFIED;
			break;
		case SQL_DESCRIBE_PARAMETER:
			debugPrintf("  infotype: "
					"SQL_DESCRIBE_PARAMETER\n");
			strval="N";
			break;
		case SQL_CATALOG_NAME:
			debugPrintf("  infotype: "
					"SQL_CATALOG_NAME\n");
			// FIXME: is this true for all db's?
			strval="Y";
			break;
		case SQL_COLLATION_SEQ:
			debugPrintf("  infotype: "
					"SQL_COLLATION_SEQ\n");
			strval="";
			break;
		case SQL_MAX_IDENTIFIER_LEN:
			// aka SQL_MAXIMUM_IDENTIFIER_LENGTH
			debugPrintf("  infotype: "
					"SQL_MAX_IDENTIFIER_LEN/"
					"SQL_MAXIMUM_IDENTIFIER_LENGTH\n");
			// FIXME: is this true for all db's?
			*(SQLUSMALLINT *)infovalue=128;
			break;
		#endif
		case SQL_DRIVER_HDBC:
			debugPrintf("  unsupported infotype: "
						"SQL_DRIVER_HDBC\n");
			break;
		case SQL_DRIVER_HENV:
			debugPrintf("  unsupported infotype: "
						"SQL_DRIVER_HENV\n");
			break;
		case SQL_DRIVER_HSTMT:
			debugPrintf("  unsupported infotype: "
						"SQL_DRIVER_HSTMT\n");
			break;
		case SQL_DRIVER_NAME:
			debugPrintf("  infotype: "
					"SQL_DRIVER_NAME\n");
			strval="SQL Relay";
			break;
		case SQL_DRIVER_VER:
			debugPrintf("  infotype: "
					"SQL_DRIVER_VER\n");
			strval=conn->con->clientVersion();
			break;
		case SQL_ODBC_API_CONFORMANCE:
			debugPrintf("  infotype: "
					"SQL_ODBC_API_CONFORMANCE\n");
			*(SQLUSMALLINT *)infovalue=SQL_OAC_LEVEL2;
			break;
		case SQL_ODBC_VER:
			debugPrintf("  infotype: "
					"SQL_ODBC_VER\n");
			// FIXME: this should be of format ##.##.####
			// (major.minor.release)
			strval=conn->con->clientVersion();
			break;
		case SQL_ROW_UPDATES:
			debugPrintf("  infotype: "
					"SQL_ROW_UPDATES\n");
			strval="N";
			break;
		case SQL_ODBC_SAG_CLI_CONFORMANCE:
			debugPrintf("  unsupported infotype: "
					"SQL_ODBC_SAG_CLI_CONFORMANCE\n");
			break;
		case SQL_ODBC_SQL_CONFORMANCE:
			debugPrintf("  infotype: "
					"SQL_ODBC_SQL_CONFORMANCE\n");
			*(SQLUSMALLINT *)infovalue=SQL_OSC_EXTENDED;
			break;
		case SQL_PROCEDURES:
			debugPrintf("  infotype: "
					"SQL_PROCEDURES\n");
			// FIXME: this isn't true for all db's
			strval="Y";
			break;
		case SQL_CONCAT_NULL_BEHAVIOR:
			debugPrintf("  infotype: "
					"SQL_CONCAT_NULL_BEHAVIOR\n");
			// FIXME: is this true for all db's?
			*(SQLUSMALLINT *)infovalue=SQL_CB_NON_NULL;
			break;
		case SQL_CURSOR_ROLLBACK_BEHAVIOR:
			debugPrintf("  infotype: "
					"SQL_CURSOR_ROLLBACK_BEHAVIOR\n");
			// FIXME: is this true for all db's?
			*(SQLUINTEGER *)infovalue=SQL_CB_CLOSE;
			break;
		case SQL_EXPRESSIONS_IN_ORDERBY:
			debugPrintf("  infotype: "
					"SQL_EXPRESSIONS_IN_ORDERBY\n");
			// FIXME: is this true for all db's?
			strval="Y";
			break;
		case SQL_MAX_PROCEDURE_NAME_LEN:
			debugPrintf("  infotype: "
					"SQL_MAX_PROCEDURE_NAME_LEN\n");
			// 0 means no max or unknown
			*(SQLUINTEGER *)infovalue=0;
			break;
		case SQL_MULT_RESULT_SETS:
			debugPrintf("  infotype: "
					"SQL_MULT_RESULT_SETS\n");
			strval="N";
			break;
		case SQL_MULTIPLE_ACTIVE_TXN:
			debugPrintf("  infotype: "
					"SQL_MULTIPLE_ACTIVE_TXN\n");
			strval="N";
			break;
		case SQL_OUTER_JOINS:
			debugPrintf("  infotype: "
					"SQL_OUTER_JOINS\n");
			// FIXME: is this true for all db's?
			strval="Y";
			break;
		case SQL_OWNER_TERM:
			// aka SQL_SCHEMA_TERM
			debugPrintf("  infotype: "
					"SQL_OWNER_TERM/"
					"SQL_SCHEMA_TERM\n");
			strval="schema";
			break;
		case SQL_PROCEDURE_TERM:
			debugPrintf("  infotype: "
					"SQL_PROCEDURE_TERM\n");
			strval="stored procedure";
			break;
		case SQL_QUALIFIER_NAME_SEPARATOR:
			// aka SQL_CATALOG_NAME_SEPARATOR
			debugPrintf("  infotype: "
					"SQL_QUALIFIER_NAME_SEPARATOR/"
					"SQL_CATALOG_NAME_SEPARATOR/");
			// FIXME: is this true for all db's?
			strval=".";
			break;
		case SQL_QUALIFIER_TERM:
			// aka SQL_CATALOG_TERM
			debugPrintf("  infotype: "
					"SQL_QUALIFIER_TERM/"
					"SQL_CATALOG_TERM\n");
			strval="catalog";
			break;
		case SQL_SCROLL_OPTIONS:
			debugPrintf("  infotype: "
					"SQL_SCROLL_OPTIONS\n");
			*(SQLUINTEGER *)infovalue=SQL_SO_FORWARD_ONLY;
			break;
		case SQL_TABLE_TERM:
			debugPrintf("  infotype: "
					"SQL_TABLE_TERM\n");
			strval="table";
			break;
		case SQL_CONVERT_FUNCTIONS:
			debugPrintf("  infotype: "
					"SQL_CONVERT_FUNCTIONS\n");
			// FIXME: this isn't true for all db's
			*(SQLUINTEGER *)infovalue=SQL_FN_CVT_CAST;
						//SQL_FN_CVT_CONVERT;
			break;
		case SQL_NUMERIC_FUNCTIONS:
			debugPrintf("  unsupported infotype: "
					"SQL_NUMERIC_FUNCTIONS\n");
			break;
		case SQL_STRING_FUNCTIONS:
			debugPrintf("  unsupported infotype: "
					"SQL_STRING_FUNCTIONS\n");
			break;
		case SQL_SYSTEM_FUNCTIONS:
			debugPrintf("  unsupported infotype: "
					"SQL_SYSTEM_FUNCTIONS\n");
			break;
		case SQL_TIMEDATE_FUNCTIONS:
			debugPrintf("  unsupported infotype: "
					"SQL_TIMEDATE_FUNCTIONS\n");
			break;
		case SQL_CONVERT_BIGINT:
			debugPrintf("  unsupported infotype: "
					"SQL_CONVERT_BIGINT\n");
			break;
		case SQL_CONVERT_BINARY:
			debugPrintf("  unsupported infotype: "
					"SQL_CONVERT_BINARY\n");
			break;
		case SQL_CONVERT_BIT:
			debugPrintf("  unsupported infotype: "
					"SQL_CONVERT_BIT\n");
			break;
		case SQL_CONVERT_CHAR:
			debugPrintf("  unsupported infotype: "
					"SQL_CONVERT_CHAR\n");
			break;
		case SQL_CONVERT_DATE:
			debugPrintf("  unsupported infotype: "
					"SQL_CONVERT_DATE\n");
			break;
		case SQL_CONVERT_DECIMAL:
			debugPrintf("  unsupported infotype: "
					"SQL_CONVERT_DECIMAL\n");
			break;
		case SQL_CONVERT_DOUBLE:
			debugPrintf("  unsupported infotype: "
					"SQL_CONVERT_DOUBLE\n");
			break;
		case SQL_CONVERT_FLOAT:
			debugPrintf("  unsupported infotype: "
					"SQL_CONVERT_FLOAT\n");
			break;
		case SQL_CONVERT_INTEGER:
			debugPrintf("  unsupported infotype: "
					"SQL_CONVERT_INTEGER\n");
			break;
		case SQL_CONVERT_LONGVARCHAR:
			debugPrintf("  unsupported infotype: "
					"SQL_CONVERT_LONGVARCHAR\n");
			break;
		case SQL_CONVERT_NUMERIC:
			debugPrintf("  unsupported infotype: "
					"SQL_CONVERT_NUMERIC\n");
			break;
		case SQL_CONVERT_REAL:
			debugPrintf("  unsupported infotype: "
					"SQL_CONVERT_REAL\n");
			break;
		case SQL_CONVERT_SMALLINT:
			debugPrintf("  unsupported infotype: "
					"SQL_CONVERT_SMALLINT\n");
			break;
		case SQL_CONVERT_TIME:
			debugPrintf("  unsupported infotype: "
					"SQL_CONVERT_TIME\n");
			break;
		case SQL_CONVERT_TIMESTAMP:
			debugPrintf("  unsupported infotype: "
					"SQL_CONVERT_TIMESTAMP\n");
			break;
		case SQL_CONVERT_TINYINT:
			debugPrintf("  unsupported infotype: "
					"SQL_CONVERT_TINYINT\n");
			break;
		case SQL_CONVERT_VARBINARY:
			debugPrintf("  unsupported infotype: "
					"SQL_CONVERT_VARBINARY\n");
			break;
		case SQL_CONVERT_VARCHAR:
			debugPrintf("  unsupported infotype: "
					"SQL_CONVERT_VARCHAR\n");
			break;
		case SQL_CONVERT_LONGVARBINARY:
			debugPrintf("  unsupported infotype: "
					"SQL_CONVERT_LONGVARBINARY\n");
			break;
		case SQL_CORRELATION_NAME:
			debugPrintf("  infotype: "
					"SQL_CORRELATION_NAME\n");
			*(SQLUSMALLINT *)infovalue=SQL_CN_ANY;
			break;
		case SQL_NON_NULLABLE_COLUMNS:
			debugPrintf("  infotype: "
					"SQL_NON_NULLABLE_COLUMNS\n");
			*(SQLUSMALLINT *)infovalue=SQL_NNC_NON_NULL;
			break;
		case SQL_DRIVER_HLIB:
			debugPrintf("  unsupported infotype: "
					"SQL_DRIVER_HLIB\n");
			break;
		case SQL_DRIVER_ODBC_VER:
			debugPrintf("  infotype: "
					"SQL_DRIVER_ODBC_VER\n");
			strval="03.00";
			break;
		case SQL_LOCK_TYPES:
			debugPrintf("  infotype: "
					"SQL_LOCK_TYPES\n");
			// FIXME: is this true for all db's?
			*(SQLINTEGER *)infovalue=SQL_LCK_NO_CHANGE|
							SQL_LCK_EXCLUSIVE|
							SQL_LCK_UNLOCK;
			break;
		case SQL_POS_OPERATIONS:
			debugPrintf("  infotype: "
					"SQL_POS_OPERATIONS\n");
			// FIXME: for now...
			*(SQLUSMALLINT *)infovalue=SQL_POS_POSITION;
			break;
		case SQL_POSITIONED_STATEMENTS:
			debugPrintf("  infotype: "
					"SQL_POSITIONED_STATEMENTS\n");
			// none, for now...
			*(SQLINTEGER *)infovalue=0;
			break;
		case SQL_BOOKMARK_PERSISTENCE:
			debugPrintf("  infotype: "
					"SQL_BOOKMARK_PERSISTENCE\n");
			// FIXME: none, for now...
			*(SQLUINTEGER *)infovalue=0;
			break;
		case SQL_STATIC_SENSITIVITY:
			debugPrintf("  infotype: "
					"SQL_STATIC_SENSITIVITY\n");
			*(SQLINTEGER *)infovalue=0;
			break;
		case SQL_FILE_USAGE:
			debugPrintf("  infotype: "
					"SQL_FILE_USAGE\n");
			*(SQLUINTEGER *)infovalue=SQL_FILE_NOT_SUPPORTED;
			break;
		case SQL_COLUMN_ALIAS:
			debugPrintf("  infotype: "
					"SQL_COLUMN_ALIAS\n");
			// FIXME: this isn't true for all db's
			strval="Y";
			break;
		case SQL_GROUP_BY:
			debugPrintf("  infotype: "
					"SQL_GROUP_BY\n");
			// FIXME: is this true for all db's?
			*(SQLUSMALLINT *)infovalue=
					#if (ODBCVER >= 0x0300)
					SQL_GB_COLLATE
					#else
					SQL_GB_GROUP_BY_EQUALS_SELECT
					#endif
					;
			break;
		case SQL_KEYWORDS:
			debugPrintf("  unsupported infotype: "
					"SQL_KEYWORDS\n");
			break;
		case SQL_OWNER_USAGE:
			// aka SQL_SCHEMA_USAGE
			debugPrintf("  infotype: "
					"SQL_OWNSER_USAGE/"
					"SQL_SCHEMA_USAGE\n");
			// FIXME: this isn't true for all db's
			*(SQLUINTEGER *)infovalue=
					SQL_SU_DML_STATEMENTS|
					SQL_SU_PROCEDURE_INVOCATION|
					SQL_SU_TABLE_DEFINITION|
					SQL_SU_INDEX_DEFINITION|
					SQL_SU_PRIVILEGE_DEFINITION;
			break;
		case SQL_QUALIFIER_USAGE:
			// aka SQL_CATALOG_USAGE
			debugPrintf("  unsupported infotype: %d\n",infotype);
			break;
		case SQL_QUOTED_IDENTIFIER_CASE:
			debugPrintf("  infotype: "
					"SQL_QUOTED_IDENTIFIER_CASE\n");
			// FIXME: is this true for all db's?
			*(SQLUINTEGER *)infovalue=SQL_IC_SENSITIVE;
			break;
		case SQL_SUBQUERIES:
			debugPrintf("  infotype: "
					"SQL_SUBQUERIES\n");
			// FIXME: is this true for all db's?
			*(SQLUINTEGER *)infovalue=
						SQL_SQ_CORRELATED_SUBQUERIES|
						SQL_SQ_COMPARISON|
						SQL_SQ_EXISTS|
						SQL_SQ_IN|
						SQL_SQ_QUANTIFIED;
			break;
		case SQL_UNION:
			// aka SQL_UNION_STATEMENT
			debugPrintf("  infotype: "
					"SQL_UNION/"
					"SQL_UNION_STATEMENT\n");
			// FIXME: this isn't true for all db's
			*(SQLUINTEGER *)infovalue=SQL_U_UNION|SQL_U_UNION_ALL;
			break;
		case SQL_MAX_ROW_SIZE_INCLUDES_LONG:
			debugPrintf("  infotype: "
					"SQL_MAX_ROW_SIZE_INCLUDES_LONG\n");
			strval="N";
			break;
		case SQL_MAX_CHAR_LITERAL_LEN:
			debugPrintf("  infotype: "
					"SQL_MAX_CHAR_LITERAL_LEN\n");
			// 0 means no max or unknown
			*(SQLUINTEGER *)infovalue=0;
			break;
		case SQL_TIMEDATE_ADD_INTERVALS:
			debugPrintf("  infotype: "
					"SQL_TIMEDATE_ADD_INTERVALS\n");
			// FIXME: this isn't true for all db's
			// I think Oracle 12c supports intervals
			*(SQLUINTEGER *)infovalue=0;
					/*SQL_FN_TSI_FRAC_SECOND|
					SQL_FN_TSI_SECOND|
					SQL_FN_TSI_MINUTE|
					SQL_FN_TSI_HOUR|
					SQL_FN_TSI_DAY|
					SQL_FN_TSI_WEEK|
					SQL_FN_TSI_MONTH|
					SQL_FN_TSI_QUARTER|
					SQL_FN_TSI_YEAR;*/
			break;
		case SQL_TIMEDATE_DIFF_INTERVALS:
			debugPrintf("  infotype: "
					"SQL_TIMEDATE_DIFF_INTERVALS\n");
			// FIXME: this isn't true for all db's
			// I think Oracle 12c supports intervals
			*(SQLUINTEGER *)infovalue=0;
					/*SQL_FN_TSI_FRAC_SECOND|
					SQL_FN_TSI_SECOND|
					SQL_FN_TSI_MINUTE|
					SQL_FN_TSI_HOUR|
					SQL_FN_TSI_DAY|
					SQL_FN_TSI_WEEK|
					SQL_FN_TSI_MONTH|
					SQL_FN_TSI_QUARTER|
					SQL_FN_TSI_YEAR;*/
			break;
		case SQL_NEED_LONG_DATA_LEN:
			debugPrintf("  infotype: "
					"SQL_NEED_LONG_DATA_LEN\n");
			strval="Y";
			break;
		case SQL_MAX_BINARY_LITERAL_LEN:
			debugPrintf("  infotype: "
					"SQL_MAX_BINARY_LITERAL_LEN\n");
			// 0 means no max or unknown
			*(SQLUINTEGER *)infovalue=0;
			break;
		case SQL_LIKE_ESCAPE_CLAUSE:
			debugPrintf("  infotype: "
					"SQL_LIKE_ESCAPE_CLAUSE\n");
			strval="Y";
			break;
		case SQL_QUALIFIER_LOCATION:
			// aka SQL_CATALOG_LOCATION
			debugPrintf("  unsupported infotype: %d\n",infotype);
			break;
		#if (ODBCVER >= 0x0300)
		case SQL_ACTIVE_ENVIRONMENTS:
			debugPrintf("  infotype: "
					"SQL_ACTIVE_ENVIRONMENTS\n");
			// 0 means no max or unknown
			*(SQLUSMALLINT *)infovalue=0;
			break;
		case SQL_ALTER_DOMAIN:
			debugPrintf("  infotype: "
					"SQL_ALTER_DOMAIN\n");
			// FIXME: no idea
			*(SQLUINTEGER *)infovalue=
					SQL_AD_ADD_DOMAIN_CONSTRAINT|
					SQL_AD_ADD_DOMAIN_DEFAULT|
					SQL_AD_CONSTRAINT_NAME_DEFINITION|
					SQL_AD_DROP_DOMAIN_CONSTRAINT|
					SQL_AD_DROP_DOMAIN_DEFAULT;
			break;
		case SQL_SQL_CONFORMANCE:
			debugPrintf("  infotype: "
					"SQL_SQL_CONFORMANCE\n");
			// FIXME: no idea, conservative guess...
			*(SQLUINTEGER *)infovalue=SQL_SC_SQL92_ENTRY;
			/**(SQLUINTEGER *)infovalue=
					SQL_SC_FIPS127_2_TRANSITIONAL;
			*(SQLUINTEGER *)infovalue=
					SQL_SC_SQL92_FULL;
			*(SQLUINTEGER *)infovalue=
					SQL_SC_SQL92_INTERMEDIATE;*/
			break;
		case SQL_DATETIME_LITERALS:
			debugPrintf("  infotype: "
					"SQL_DATETIME_LITERALS\n");
			// FIXME: this isn't true for all db's
			// I think Oracle 12c supports intervals
			*(SQLUINTEGER *)infovalue=0;
				/*SQL_DL_SQL92_DATE|
				SQL_DL_SQL92_TIME|
				SQL_DL_SQL92_TIMESTAMP|
				SQL_DL_SQL92_INTERVAL_YEAR|
				SQL_DL_SQL92_INTERVAL_MONTH|
				SQL_DL_SQL92_INTERVAL_DAY|
				SQL_DL_SQL92_INTERVAL_HOUR|
				SQL_DL_SQL92_INTERVAL_MINUTE|
				SQL_DL_SQL92_INTERVAL_SECOND|
				SQL_DL_SQL92_INTERVAL_YEAR_TO_MONTH|
				SQL_DL_SQL92_INTERVAL_DAY_TO_HOUR
				SQL_DL_SQL92_INTERVAL_DAY_TO_MINUTE|
				SQL_DL_SQL92_INTERVAL_DAY_TO_SECOND|
				SQL_DL_SQL92_INTERVAL_HOUR_TO_MINUTE|
				SQL_DL_SQL92_INTERVAL_HOUR_TO_SECOND|
				SQL_DL_SQL92_INTERVAL_MINUTE_TO_SECOND;*/
			break;
		case SQL_ASYNC_MODE:
			debugPrintf("  infotype: "
					"SQL_ASYNC_MODE\n");
			*(SQLUINTEGER *)infovalue=SQL_AM_NONE;
			break;
		case SQL_BATCH_ROW_COUNT:
			debugPrintf("  infotype: "
					"SQL_BATCH_ROW_COUNT\n");
			// batch sql is not supported
			*(SQLUINTEGER *)infovalue=0;
			break;
		case SQL_BATCH_SUPPORT:
			debugPrintf("  infotype: "
					"SQL_BATCH_SUPPORT\n");
			// batch sql is not supported
			*(SQLUINTEGER *)infovalue=0;
			break;
		case SQL_CONVERT_WCHAR:
			debugPrintf("  unsupported infotype: "
					"SQL_CONVERT_WCHAR\n");
			break;
		case SQL_CONVERT_INTERVAL_DAY_TIME:
			debugPrintf("  unsupported infotype: "
					"SQL_CONVERT_INTERVAL_DAY_TIME\n");
			break;
		case SQL_CONVERT_INTERVAL_YEAR_MONTH:
			debugPrintf("  unsupported infotype: "
					"SQL_CONVERT_INTERVAL_YEAR_MONTH\n");
			break;
		case SQL_CONVERT_WLONGVARCHAR:
			debugPrintf("  unsupported infotype: "
					"SQL_CONVERT_WLONGVARCHAR\n");
			break;
		case SQL_CONVERT_WVARCHAR:
			debugPrintf("  unsupported infotype: "
					"SQL_CONVERT_WVARCHAR\n");
			break;
		case SQL_CREATE_ASSERTION:
			debugPrintf("  infotype: "
					"SQL_CREATE_ASSERTION\n");
			// FIXME: not sure about this...
			*(SQLUINTEGER *)infovalue=0;
					/*SQL_CA_CREATE_ASSERTION|
					SQL_CA_CONSTRAINT_INITIALLY_DEFERRED|
					SQL_CA_CONSTRAINT_INITIALLY_IMMEDIATE|
					SQL_CA_CONSTRAINT_DEFERRABLE|
					SQL_CA_CONSTRAINT_NON_DEFERRABLE;*/
			break;
		case SQL_CREATE_CHARACTER_SET:
			debugPrintf("  infotype: "
					"SQL_CREATE_CHARACTER_SET\n");
			// FIXME: not sure about this...
			*(SQLUINTEGER *)infovalue=0;
					/*SQL_CCS_CREATE_CHARACTER_SET|
					SQL_CCS_COLLATE_CLAUSE|
					SQL_CCS_LIMITED_COLLATION;*/
			break;
		case SQL_CREATE_COLLATION:
			debugPrintf("  infotype: "
					"SQL_CREATE_COLLATION\n");
			// FIXME: not sure about this...
			*(SQLUINTEGER *)infovalue=0;
					//SQL_CCOL_CREATE_COLLATION;
			break;
		case SQL_CREATE_DOMAIN:
			debugPrintf("  infotype: "
					"SQL_CREATE_DOMAIN\n");
			// FIXME: not sure about this...
			*(SQLUINTEGER *)infovalue=0;
					/*SQL_CDO_CREATE_DOMAIN|
					SQL_CDO_CONSTRAINT_NAME_DEFINITION|
					SQL_CDO_DEFAULT|
					SQL_CDO_CONSTRAINT|
					SQL_CDO_COLLATION|
					SQL_CDO_CONSTRAINT_INITIALLY_DEFERRED|
					SQL_CDO_CONSTRAINT_INITIALLY_IMMEDIATE|
					SQL_CDO_CONSTRAINT_DEFERRABLE|
					SQL_CDO_CONSTRAINT_NON_DEFERRABLE;*/
			break;
		case SQL_CREATE_SCHEMA:
			debugPrintf("  infotype: "
					"SQL_CREATE_SCHEMA\n");
			// FIXME: is this true for all db's?
			*(SQLUINTEGER *)infovalue=SQL_CS_CREATE_SCHEMA|
						SQL_CS_AUTHORIZATION|
						SQL_CS_DEFAULT_CHARACTER_SET;
			break;
		case SQL_CREATE_TABLE:
			debugPrintf("  infotype: "
					"SQL_CREATE_TABLE\n");
			// FIXME: this isn't true for all db's
			*(SQLUINTEGER *)infovalue=
					SQL_CT_CREATE_TABLE|
					SQL_CT_TABLE_CONSTRAINT|
					SQL_CT_CONSTRAINT_NAME_DEFINITION|
					SQL_CT_COMMIT_DELETE|
					SQL_CT_GLOBAL_TEMPORARY|
					SQL_CT_COLUMN_CONSTRAINT|
					SQL_CT_COLUMN_DEFAULT|
					SQL_CT_COLUMN_COLLATION|
					SQL_CT_CONSTRAINT_INITIALLY_IMMEDIATE|
					SQL_CT_CONSTRAINT_NON_DEFERRABLE;
			break;
		case SQL_CREATE_TRANSLATION:
			debugPrintf("  infotype: "
					"SQL_CREATE_TRANSLATION\n");
			// FIXME: not sure about this...
			*(SQLUINTEGER *)infovalue=0;
					//SQL_CTR_CREATE_TRANSLATION;
			break;
		case SQL_CREATE_VIEW:
			debugPrintf("  infotype: "
					"SQL_CREATE_VIEW\n");
			// FIXME: is this true for all db's?
			*(SQLUINTEGER *)infovalue=SQL_CV_CREATE_VIEW|
							SQL_CV_CHECK_OPTION|
							SQL_CV_CASCADED|
							SQL_CV_LOCAL ;
			break;
		case SQL_DRIVER_HDESC:
			debugPrintf("  unsupported infotype: "
					"SQL_DRIVER_HDESC\n");
			break;
		case SQL_DROP_ASSERTION:
			debugPrintf("  infotype: "
					"SQL_DROP_ASSERTION\n");
			// FIXME: not sure about this...
			*(SQLUINTEGER *)infovalue=0;
					//SQL_DA_DROP_ASSERTION;
			break;
		case SQL_DROP_CHARACTER_SET:
			debugPrintf("  infotype: "
					"SQL_DROP_CHARACTER_SET\n");
			*(SQLUINTEGER *)infovalue=0;
					//SQL_DCS_DROP_CHARACTER_SET;
			break;
		case SQL_DROP_COLLATION:
			debugPrintf("  infotype: "
					"SQL_DROP_COLLATION\n");
			*(SQLUINTEGER *)infovalue=0;
					//SQL_DC_DROP_COLLATION;
			break;
		case SQL_DROP_DOMAIN:
			debugPrintf("  infotype: "
					"SQL_DROP_DOMAIN\n");
			*(SQLUINTEGER *)infovalue=0;
					//SQL_DD_DROP_DOMAIN|
					//SQL_DD_CASCADE|
					//SQL_DD_RESTRICT;
			break;
		case SQL_DROP_SCHEMA:
			debugPrintf("  infotype: "
					"SQL_DROP_SCHEMA\n");
			// FIXME: is this true for all db's?
			*(SQLUINTEGER *)infovalue=SQL_DS_DROP_SCHEMA|
						SQL_DS_CASCADE|
						SQL_DS_RESTRICT;
			break;
		case SQL_DROP_TABLE:
			debugPrintf("  infotype: "
					"SQL_DROP_TABLE\n");
			// FIXME: is this true for all db's?
			*(SQLUINTEGER *)infovalue=SQL_DT_DROP_TABLE|
						SQL_DT_CASCADE|
						SQL_DT_RESTRICT;
			break;
		case SQL_DROP_TRANSLATION:
			debugPrintf("  infotype: "
					"SQL_DROP_TRANSLATION\n");
			// FIXME: not sure about this...
			*(SQLUINTEGER *)infovalue=0;
					//SQL_DTR_DROP_TRANSLATION;
			break;
		case SQL_DROP_VIEW:
			debugPrintf("  infotype: "
					"SQL_DROP_VIEW\n");
			// FIXME: is this true for all db's?
			*(SQLUINTEGER *)infovalue=SQL_DV_DROP_VIEW|
							SQL_DV_CASCADE|
							SQL_DV_RESTRICT;
			break;
		case SQL_DYNAMIC_CURSOR_ATTRIBUTES1:
			debugPrintf("  infotype: "
					"SQL_DYNAMIC_CURSOR_ATTRIBUTES1\n");
			// for now...
			*(SQLUINTEGER *)infovalue=SQL_CA1_NEXT|
						SQL_CA1_POS_POSITION;
			break;
		case SQL_DYNAMIC_CURSOR_ATTRIBUTES2:
			debugPrintf("  infotype: "
					"SQL_DYNAMIC_CURSOR_ATTRIBUTES2\n");
			*(SQLUINTEGER *)infovalue=SQL_CA2_READ_ONLY_CONCURRENCY;
			break;
		case SQL_FORWARD_ONLY_CURSOR_ATTRIBUTES1:
			debugPrintf("  infotype: "
				"SQL_FORWARD_ONLY_CURSOR_ATTRIBUTES1\n");
			// for now...
			*(SQLUINTEGER *)infovalue=SQL_CA1_NEXT|
						SQL_CA1_POS_POSITION;
			break;
		case SQL_FORWARD_ONLY_CURSOR_ATTRIBUTES2:
			debugPrintf("  infotype: "
				"SQL_FORWARD_ONLY_CURSOR_ATTRIBUTES2\n");
			*(SQLUINTEGER *)infovalue=SQL_CA2_READ_ONLY_CONCURRENCY;
			break;
		case SQL_INDEX_KEYWORDS:
			debugPrintf("  infotype: "
					"SQL_INDEX_KEYWORDS\n");
			// FIXME: is this true for all db's?
			*(SQLUINTEGER *)infovalue=SQL_IK_ALL;
			break;
		case SQL_INFO_SCHEMA_VIEWS:
			debugPrintf("  infotype: "
					"SQL_INFO_SCHEMA_VIEWS\n");
			// FIXME: this isn't true for all db's
			*(SQLUINTEGER *)infovalue=0;
					/*SQL_ISV_ASSERTIONS
					SQL_ISV_CHARACTER_SETS
					SQL_ISV_CHECK_CONSTRAINTS
					SQL_ISV_COLLATIONS
					SQL_ISV_COLUMN_DOMAIN_USAGE
					SQL_ISV_COLUMN_PRIVILEGES
					SQL_ISV_COLUMNS
					SQL_ISV_CONSTRAINT_COLUMN_USAGE
					SQL_ISV_CONSTRAINT_TABLE_USAGE
					SQL_ISV_DOMAIN_CONSTRAINTS
					SQL_ISV_DOMAINS
					SQL_ISV_KEY_COLUMN_USAGE
					SQL_ISV_REFERENTIAL_CONSTRAINTS
					SQL_ISV_SCHEMATA
					SQL_ISV_SQL_LANGUAGES
					SQL_ISV_TABLE_CONSTRAINTS
					SQL_ISV_TABLE_PRIVILEGES
					SQL_ISV_TABLES
					SQL_ISV_TRANSLATIONS
					SQL_ISV_USAGE_PRIVILEGES
					SQL_ISV_VIEW_COLUMN_USAGE
					SQL_ISV_VIEW_TABLE_USAGE
					SQL_ISV_VIEWS;*/
			break;
		case SQL_KEYSET_CURSOR_ATTRIBUTES1:
			debugPrintf("  infotype: "
					"SQL_KEYSET_CURSOR_ATTRIBUTES1\n");
			// for now...
			*(SQLUINTEGER *)infovalue=SQL_CA1_NEXT|
						SQL_CA1_POS_POSITION;
			break;
		case SQL_KEYSET_CURSOR_ATTRIBUTES2:
			debugPrintf("  infotype: "
					"SQL_KEYSET_CURSOR_ATTRIBUTES2\n");
			*(SQLUINTEGER *)infovalue=SQL_CA2_READ_ONLY_CONCURRENCY;
			break;
		case SQL_MAX_ASYNC_CONCURRENT_STATEMENTS:
			debugPrintf("  unsupported infotype: %d\n",infotype);
			// 0 means no max or unknown
			*(SQLUINTEGER *)infovalue=0;
			break;
		case SQL_ODBC_INTERFACE_CONFORMANCE:
			debugPrintf("  infotype: "
					"SQL_ODBC_INTERFACE_CONFORMANCE\n");
			*(SQLUINTEGER *)infovalue=SQL_OIC_CORE;
			break;
		case SQL_PARAM_ARRAY_ROW_COUNTS:
			debugPrintf("  infotype: "
					"SQL_PARAM_ARRAY_ROW_COUNTS\n");
			// batch sql is not supported
			*(SQLUINTEGER *)infovalue=0;
			break;
		case SQL_PARAM_ARRAY_SELECTS:
			debugPrintf("  infotype: "
					"SQL_PARAM_ARRAY_SELECTS\n");
			// for now...
			*(SQLUINTEGER *)infovalue=SQL_PAS_NO_SELECT;
			break;
		case SQL_SQL92_DATETIME_FUNCTIONS:
			debugPrintf("  infotype: "
					"SQL_SQL92_DATETIME_FUNCTIONS\n");
			// FIXME: this isn't true for all db's
			*(SQLUINTEGER *)infovalue=
					SQL_SDF_CURRENT_DATE|
					SQL_SDF_CURRENT_TIME|
					SQL_SDF_CURRENT_TIMESTAMP;
			break;
		case SQL_SQL92_FOREIGN_KEY_DELETE_RULE:
			debugPrintf("  infotype: "
					"SQL_SQL92_FOREIGN_KEY_DELETE_RULE\n");
			// FIXME: this isn't true for all db's
			*(SQLUINTEGER *)infovalue=SQL_SFKD_CASCADE;
			break;
		case SQL_SQL92_FOREIGN_KEY_UPDATE_RULE:
			debugPrintf("  infotype: "
					"SQL_SQL92_FOREIGN_KEY_UPDATE_RULE\n");
			// FIXME: this isn't true for all db's
			*(SQLUINTEGER *)infovalue=SQL_SFKU_CASCADE;
			break;
		case SQL_SQL92_GRANT:
			debugPrintf("  infotype: "
					"SQL_SQL92_GRANT\n");
			// FIXME: this isn't true for all db's
			*(SQLUINTEGER *)infovalue=
					SQL_SG_DELETE_TABLE|
					SQL_SG_INSERT_COLUMN|
					SQL_SG_INSERT_TABLE|
					SQL_SG_REFERENCES_TABLE|
					SQL_SG_REFERENCES_COLUMN|
					SQL_SG_SELECT_TABLE|
					SQL_SG_UPDATE_COLUMN|
					SQL_SG_UPDATE_TABLE|
					SQL_SG_USAGE_ON_DOMAIN|
					SQL_SG_USAGE_ON_CHARACTER_SET|
					SQL_SG_USAGE_ON_COLLATION|
					SQL_SG_USAGE_ON_TRANSLATION|
					SQL_SG_WITH_GRANT_OPTION;
			break;
		case SQL_SQL92_NUMERIC_VALUE_FUNCTIONS:
			debugPrintf("  infotype: "
					"SQL_SQL92_NUMERIC_VALUE_FUNCTIONS\n");
			// FIXME: this isn't true for all db's
			*(SQLUINTEGER *)infovalue=
					SQL_SNVF_BIT_LENGTH|
					SQL_SNVF_CHAR_LENGTH|
					SQL_SNVF_CHARACTER_LENGTH|
					SQL_SNVF_EXTRACT|
					SQL_SNVF_OCTET_LENGTH|
					SQL_SNVF_POSITION;
			break;
		case SQL_SQL92_PREDICATES:
			debugPrintf("  infotype: "
					"SQL_SQL92_PREDICATES\n");
			// FIXME: this isn't true for all db's
			*(SQLUINTEGER *)infovalue=
					SQL_SP_BETWEEN|
					SQL_SP_COMPARISON|
					SQL_SP_EXISTS|
					SQL_SP_IN|
					SQL_SP_ISNOTNULL|
					SQL_SP_ISNULL|
					SQL_SP_LIKE|
					SQL_SP_MATCH_FULL|
					SQL_SP_MATCH_PARTIAL|
					SQL_SP_MATCH_UNIQUE_FULL|
					SQL_SP_MATCH_UNIQUE_PARTIAL|
					SQL_SP_OVERLAPS|
					SQL_SP_QUANTIFIED_COMPARISON|
					SQL_SP_UNIQUE;
			break;
		case SQL_SQL92_RELATIONAL_JOIN_OPERATORS:
			debugPrintf("  infotype: "
				"SQL_SQL92_RELATIONAL_JOIN_OPERATORS\n");
			// FIXME: this isn't true for all db's
			*(SQLUINTEGER *)infovalue=
					SQL_SRJO_CORRESPONDING_CLAUSE|
					SQL_SRJO_CROSS_JOIN|
					SQL_SRJO_EXCEPT_JOIN|
					SQL_SRJO_FULL_OUTER_JOIN|
					SQL_SRJO_INNER_JOIN|
					SQL_SRJO_INTERSECT_JOIN|
					SQL_SRJO_LEFT_OUTER_JOIN|
					SQL_SRJO_NATURAL_JOIN|
					SQL_SRJO_RIGHT_OUTER_JOIN|
					SQL_SRJO_UNION_JOIN;
			break;
		case SQL_SQL92_REVOKE:
			debugPrintf("  infotype: "
					"SQL_SQL92_REVOKE\n");
			// FIXME: this isn't true for all db's
			*(SQLUINTEGER *)infovalue=
					SQL_SR_CASCADE|
					SQL_SR_DELETE_TABLE|
					SQL_SR_GRANT_OPTION_FOR|
					SQL_SR_INSERT_COLUMN|
					SQL_SR_INSERT_TABLE|
					SQL_SR_REFERENCES_COLUMN|
					SQL_SR_REFERENCES_TABLE|
					SQL_SR_RESTRICT|
					SQL_SR_SELECT_TABLE|
					SQL_SR_UPDATE_COLUMN|
					SQL_SR_UPDATE_TABLE|
					SQL_SR_USAGE_ON_DOMAIN|
					SQL_SR_USAGE_ON_CHARACTER_SET|
					SQL_SR_USAGE_ON_COLLATION|
					SQL_SR_USAGE_ON_TRANSLATION;
			break;
		case SQL_SQL92_ROW_VALUE_CONSTRUCTOR:
			debugPrintf("  infotype: "
					"SQL_SQL92_ROW_VALUE_CONSTRUCTOR\n");
			// FIXME: this isn't true for all db's
			*(SQLUINTEGER *)infovalue=
					SQL_SRVC_VALUE_EXPRESSION|
					SQL_SRVC_NULL|
					SQL_SRVC_DEFAULT|
					SQL_SRVC_ROW_SUBQUERY;
			break;
		case SQL_SQL92_STRING_FUNCTIONS:
			debugPrintf("  infotype: "
					"SQL_SQL92_STRING_FUNCTIONS\n");
			// FIXME: this isn't true for all db's
			*(SQLUINTEGER *)infovalue=
					SQL_SSF_CONVERT|
					SQL_SSF_LOWER|
					SQL_SSF_UPPER|
					SQL_SSF_SUBSTRING|
					SQL_SSF_TRANSLATE|
					SQL_SSF_TRIM_BOTH|
					SQL_SSF_TRIM_LEADING|
					SQL_SSF_TRIM_TRAILING;
			break;
		case SQL_SQL92_VALUE_EXPRESSIONS:
			debugPrintf("  infotype: "
					"SQL_SQL92_VALUE_EXPRESSIONS\n");
			// FIXME: this isn't true for all db's
			*(SQLUINTEGER *)infovalue=
					SQL_SVE_CASE|
					SQL_SVE_CAST|
					SQL_SVE_COALESCE|
					SQL_SVE_NULLIF;
			break;
		case SQL_STANDARD_CLI_CONFORMANCE:
			debugPrintf("  infotype: "
					"SQL_STANDARD_CLI_CONFORMANCE\n");
			// FIXME: no idea, conservative guess...
			*(SQLUINTEGER *)infovalue=SQL_SCC_XOPEN_CLI_VERSION1;
			//*(SQLUINTEGER *)infovalue=SQL_SCC_ISO92_CLI;
			break;
		case SQL_STATIC_CURSOR_ATTRIBUTES1:
			debugPrintf("  infotype: "
					"SQL_STATIC_CURSOR_ATTRIBUTES1\n");
			// for now...
			*(SQLUINTEGER *)infovalue=SQL_CA1_NEXT|
						SQL_CA1_POS_POSITION;
			break;
		case SQL_STATIC_CURSOR_ATTRIBUTES2:
			debugPrintf("  infotype: "
					"SQL_STATIC_CURSOR_ATTRIBUTES2\n");
			*(SQLUINTEGER *)infovalue=SQL_CA2_READ_ONLY_CONCURRENCY;
			break;
		case SQL_AGGREGATE_FUNCTIONS:
			debugPrintf("  infotype: "
					"SQL_AGGREGATE_FUNCTIONS\n");
			// FIXME: is this true for all db's?
			*(SQLUINTEGER *)infovalue=
						SQL_AF_ALL|
						SQL_AF_AVG|
						SQL_AF_COUNT|
						SQL_AF_DISTINCT|
						SQL_AF_MAX|
						SQL_AF_MIN|
						SQL_AF_SUM;
			break;
		case SQL_DDL_INDEX:
			debugPrintf("  infotype: "
					"SQL_DDL_INDEX\n");
			*(SQLUINTEGER *)infovalue=
					SQL_DI_CREATE_INDEX|
					SQL_DI_DROP_INDEX;
			break;
		case SQL_DM_VER:
			debugPrintf("  infotype: "
					"SQL_DM_VER\n");
			// FIXME: Should this be implemented or should only
			// the Driver Manager implement it?
			// This is the version number for the windows 7
			// Driver Manager...
			strval="03.80";
			break;
		case SQL_INSERT_STATEMENT:
			debugPrintf("  infotype: "
					"SQL_INSERT_STATEMENT\n");
			// FIXME: is this true for all db's?
			*(SQLUINTEGER *)infovalue=
					SQL_IS_INSERT_LITERALS|
					SQL_IS_INSERT_SEARCHED|
					SQL_IS_SELECT_INTO;
			break;
		#if (ODBCVER >= 0x0380)
		case SQL_ASYNC_DBC_FUNCTIONS:
			debugPrintf("  infotype: "
					"SQL_ASYNC_DBC_FUNCTIONS\n");
			// for now...
			*(SQLUINTEGER *)infovalue=SQL_ASYNC_DBC_NOT_CAPABLE;
			break;
		#endif
		#endif
		#ifdef SQL_DTC_TRANSITION_COST
		case SQL_DTC_TRANSITION_COST:
			debugPrintf("  unsupported infotype: "
					"SQL_DTC_TRANSITION_COST\n");
			break;
		#endif
		default:
			debugPrintf("  unsupported infotype: %d\n",infotype);
			break;
	}

	// copy out the string value
	if (strval) {
		charstring::safeCopy((char *)infovalue,bufferlength,strval);
		debugPrintf("  infovalue: %s\n",(const char *)infovalue);
		if (stringlength) {
			*stringlength=charstring::length(strval);
			debugPrintf("  stringlength: %d\n",(int)*stringlength);
		}
	}

	return SQL_SUCCESS;
}

static SQLRETURN SQLR_SQLGetStmtAttr(SQLHSTMT statementhandle,
					SQLINTEGER attribute,
					SQLPOINTER value,
					SQLINTEGER bufferlength,
					SQLINTEGER *stringlength) {
	debugFunction();

	STMT	*stmt=(STMT *)statementhandle;
	if (statementhandle==SQL_NULL_HSTMT || !stmt || !stmt->cur) {
		debugPrintf("  NULL stmt handle\n");
		return SQL_INVALID_HANDLE;
	}

	switch (attribute) {
		#if (ODBCVER >= 0x0300)
		case SQL_ATTR_APP_ROW_DESC:
			debugPrintf("  attribute: SQL_ATTR_APP_ROW_DESC\n");
			*(rowdesc **)value=stmt->approwdesc;
			break;
		case SQL_ATTR_APP_PARAM_DESC:
			debugPrintf("  attribute: SQL_ATTR_APP_PARAM_DESC\n");
			*(paramdesc **)value=stmt->appparamdesc;
			break;
		case SQL_ATTR_IMP_ROW_DESC:
			debugPrintf("  attribute: SQL_ATTR_IMP_ROW_DESC\n");
			*(rowdesc **)value=stmt->improwdesc;
			break;
		case SQL_ATTR_IMP_PARAM_DESC:
			debugPrintf("  attribute: SQL_ATTR_IMP_PARAM_DESC\n");
			*(paramdesc **)value=stmt->impparamdesc;
			break;
		case SQL_ATTR_CURSOR_SCROLLABLE:
			debugPrintf("  attribute: SQL_ATTR_CURSOR_SCROLLABLE\n");
			*(SQLULEN *)value=SQL_NONSCROLLABLE;
			break;
		case SQL_ATTR_CURSOR_SENSITIVITY:
			debugPrintf("  attribute: SQL_ATTR_CURSOR_SENSITIVITY\n");
			*(SQLULEN *)value=SQL_UNSPECIFIED;
			break;
		#endif
		//case SQL_ATTR_QUERY_TIMEOUT:
		case SQL_QUERY_TIMEOUT:
			debugPrintf("  attribute: "
					"SQL_ATTR_QUERY_TIMEOUT/"
					"SQL_QUERY_TIMEOUT\n");
			*(SQLULEN *)value=0;
			break;
		//case SQL_ATTR_MAX_ROWS:
		case SQL_MAX_ROWS:
			debugPrintf("  attribute: "
					"SQL_ATTR_MAX_ROWS/"
					"SQL_MAX_ROWS:\n");
			*(SQLULEN *)value=0;
			break;
		//case SQL_ATTR_NOSCAN:
		case SQL_NOSCAN:
			debugPrintf("  attribute: "
					"SQL_ATTR_NOSCAN/"
					"SQL_NOSCAN\n");
			// FIXME: is this true for all db's?
			*(SQLULEN *)value=SQL_NOSCAN_OFF;
			break;
		//case SQL_ATTR_MAX_LENGTH:
		case SQL_MAX_LENGTH:
			debugPrintf("  attribute: "
					"SQL_ATTR_MAX_LENGTH/"
					"SQL_MAX_LENGTH\n");
			*(SQLULEN *)value=0;
			break;
		//case SQL_ATTR_ASYNC_ENABLE:
		case SQL_ASYNC_ENABLE:
			debugPrintf("  attribute: "
					"SQL_ATTR_ASYNC_ENABLE/"
					"SQL_ASYNC_ENABLE\n");
			*(SQLULEN *)value=SQL_ASYNC_ENABLE_OFF;
			break;
		//case SQL_ATTR_ROW_BIND_TYPE:
		case SQL_BIND_TYPE:
			debugPrintf("  attribute: "
					"SQL_ATTR_BIND_TYPE/"
					"SQL_BIND_TYPE\n");
			*(SQLULEN *)value=stmt->rowbindtype;
			break;
		//case SQL_ATTR_CONCURRENCY:
		//case SQL_ATTR_CURSOR_TYPE:
		case SQL_CURSOR_TYPE:
			debugPrintf("  attribute: "
					"SQL_ATTR_CONCURRENCY/"
					"SQL_ATTR_CURSOR_TYPE/"
					"SQL_CURSOR_TYPE\n");
			*(SQLULEN *)value=SQL_CURSOR_FORWARD_ONLY;
			break;
		case SQL_CONCURRENCY:
			debugPrintf("  attribute: SQL_CONCURRENCY\n");
			*(SQLULEN *)value=SQL_CONCUR_READ_ONLY;
			break;
		//case SQL_ATTR_KEYSET_SIZE:
		case SQL_KEYSET_SIZE:
			debugPrintf("  attribute: "
					"SQL_ATTR_KEYSET_SIZE/"
					"SQL_KEYSET_SIZE\n");
			*(SQLULEN *)value=0;
			break;
		case SQL_ROWSET_SIZE:
			debugPrintf("  attribute: SQL_ROWSET_SIZE\n");
			*(SQLULEN *)value=stmt->cur->getResultSetBufferSize();
			break;
		//case SQL_ATTR_SIMULATE_CURSOR:
		case SQL_SIMULATE_CURSOR:
			debugPrintf("  attribute: "
					"SQL_ATTR_SIMULATE_CURSOR/"
					"SQL_SIMULATE_CURSOR\n");
			// FIXME: I'm not sure this is true...
			*(SQLULEN *)value=SQL_SC_UNIQUE;
			break;
		//case SQL_ATTR_RETRIEVE_DATA:
		case SQL_RETRIEVE_DATA:
			debugPrintf("  attribute: "
					"SQL_ATTR_RETRIEVE_DATA/"
					"SQL_RETRIEVE_DATA\n");
			*(SQLULEN *)value=SQL_RD_ON;
			break;
		//case SQL_ATTR_USE_BOOKMARKS:
		case SQL_USE_BOOKMARKS:
			debugPrintf("  attribute: "
					"SQL_ATTR_USE_BOOKMARKS/"
					"SQL_USE_BOOKMARKS\n");
			*(SQLULEN *)value=SQL_UB_OFF;
			break;
		case SQL_GET_BOOKMARK:
			debugPrintf("  attribute: SQL_GET_BOOKMARK\n");
			// FIXME: implement
			break;
		// case SQL_ATTR_ROW_NUMBER
		case SQL_ROW_NUMBER:
			debugPrintf("  attribute: "
					"SQL_ATTR_ROW_NUMBER/"
					"SQL_ROW_NUMBER\n");
			// FIXME: implement
			break;
		#if (ODBCVER >= 0x0300)
		case SQL_ATTR_ENABLE_AUTO_IPD:
			debugPrintf("  attribute: SQL_ATTR_ENABLE_AUTO_IPD\n");
			*(SQLULEN *)value=SQL_TRUE;
			break;
		case SQL_ATTR_FETCH_BOOKMARK_PTR:
			debugPrintf("  attribute: SQL_ATTR_FETCH_BOOKMARK_PTR\n");
			// FIXME: implement
			break;
		case SQL_ATTR_PARAM_BIND_OFFSET_PTR:
			debugPrintf("  attribute: "
					"SQL_ATTR_PARAM_BIND_OFFSET_PTR\n");
			// FIXME: implement
			break;
		case SQL_ATTR_PARAM_BIND_TYPE:
			debugPrintf("  attribute: SQL_ATTR_PARAM_BIND_TYPE\n");
			// FIXME: implement
			break;
		case SQL_ATTR_PARAM_OPERATION_PTR:
			debugPrintf("  attribute: "
					"SQL_ATTR_PARAM_OPERATION_PTR\n");
			// FIXME: implement
			break;
		case SQL_ATTR_PARAM_STATUS_PTR:
			debugPrintf("  attribute: SQL_ATTR_PARAM_STATUS_PTR\n");
			// FIXME: implement
			break;
		case SQL_ATTR_PARAMS_PROCESSED_PTR:
			debugPrintf("  attribute: "
					"SQL_ATTR_PARAMS_PROCESSED_PTR\n");
			// FIXME: implement
			break;
		case SQL_ATTR_PARAMSET_SIZE:
			debugPrintf("  attribute: SQL_ATTR_PARAMSET_SIZE\n");
			// FIXME: implement
			break;
		case SQL_ATTR_ROW_BIND_OFFSET_PTR:
			debugPrintf("  attribute: "
					"SQL_ATTR_ROW_BIND_OFFSET_PTR\n");
			// FIXME: implement
			break;
		case SQL_ATTR_ROW_OPERATION_PTR:
			debugPrintf("  attribute: SQL_ATTR_ROW_OPERATION_PTR\n");
			// FIXME: implement
			break;
		case SQL_ATTR_ROW_STATUS_PTR:
			debugPrintf("  attribute: SQL_ATTR_ROW_STATUS_PTR\n");
			*(SQLUSMALLINT **)value=stmt->rowstatusptr;
			break;
		case SQL_ATTR_ROWS_FETCHED_PTR:
			debugPrintf("  attribute: SQL_ATTR_ROWS_FETCHED_PTR\n");
			*(SQLROWSETSIZE **)value=stmt->rowsfetchedptr;
			break;
		case SQL_ATTR_ROW_ARRAY_SIZE:
			debugPrintf("  attribute: SQL_ATTR_ROW_ARRAY_SIZE\n");
			*(SQLULEN *)value=stmt->cur->getResultSetBufferSize();
			break;
		#endif
		#if (ODBCVER < 0x0300)
		case SQL_STMT_OPT_MAX:
			debugPrintf("  attribute: SQL_STMT_OPT_MAX\n");
			// FIXME: implement
			break;
		case SQL_STMT_OPT_MIN:
			debugPrintf("  attribute: SQL_STMT_OPT_MIN\n");
			// FIXME: implement
			break;
		#endif
		default:
			debugPrintf("  invalid attribute\n");
			return SQL_ERROR;
	}
	return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLGetStmtAttr(SQLHSTMT statementhandle,
					SQLINTEGER attribute,
					SQLPOINTER value,
					SQLINTEGER bufferlength,
					SQLINTEGER *stringlength) {
	debugFunction();
	return SQLR_SQLGetStmtAttr(statementhandle,attribute,
					value,bufferlength,stringlength);
}

SQLRETURN SQL_API SQLGetStmtOption(SQLHSTMT statementhandle,
					SQLUSMALLINT option,
					SQLPOINTER value) {
	debugFunction();
	return SQLR_SQLGetStmtAttr(statementhandle,option,value,-1,NULL);
}

SQLRETURN SQL_API SQLGetTypeInfo(SQLHSTMT statementhandle,
					SQLSMALLINT DataType) {
	debugFunction();
	// not supported, return success though, JDBC-ODBC bridge really
	// wants this function to work and it will fail gracefully when
	// attempts to fetch the result set result in no data
	return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLNumResultCols(SQLHSTMT statementhandle,
					SQLSMALLINT *columncount) {
	debugFunction();

	STMT	*stmt=(STMT *)statementhandle;
	if (statementhandle==SQL_NULL_HSTMT || !stmt || !stmt->cur) {
		debugPrintf("  NULL stmt handle\n");
		return SQL_INVALID_HANDLE;
	}

	SQLRETURN	result=SQL_SUCCESS;

	// Some db's apparently support this after the prepare phase, prior
	// to execution.  SQL Relay doesn't but we can fake that by executing
	// here and bypassing the next attempt to execute.
	if (!stmt->executed) {
		debugPrintf("  not executed yet...\n");
		stmt->executedbynumresultcolsresult=SQLR_SQLExecute(stmt);
		stmt->executedbynumresultcols=true;
		result=stmt->executedbynumresultcolsresult;
	}

	*columncount=(SQLSMALLINT)stmt->cur->colCount();
	debugPrintf("  columncount: %d\n",(int)*columncount);

	return result;
}

SQLRETURN SQL_API SQLParamData(SQLHSTMT statementhandle,
					SQLPOINTER *Value) {
	debugFunction();

	STMT	*stmt=(STMT *)statementhandle;
	if (statementhandle==SQL_NULL_HSTMT || !stmt || !stmt->cur) {
		debugPrintf("  NULL stmt handle\n");
		return SQL_INVALID_HANDLE;
	}

	// not supported
	SQLR_STMTSetError(stmt,
			"Driver does not support this function",0,"IM001");

	return SQL_ERROR;
}

SQLRETURN SQL_API SQLPrepare(SQLHSTMT statementhandle,
					SQLCHAR *statementtext,
					SQLINTEGER textlength) {
	debugFunction();

	STMT	*stmt=(STMT *)statementhandle;
	if (statementhandle==SQL_NULL_HSTMT || !stmt || !stmt->cur) {
		debugPrintf("  NULL stmt handle\n");
		return SQL_INVALID_HANDLE;
	}

	// trim query
	uint32_t	statementtextlength=SQLR_TrimQuery(
						statementtext,textlength);

	// prepare the query
	#ifdef DEBUG_MESSAGES
	stringbuffer	debugstr;
	debugstr.append(statementtext,statementtextlength);
	debugPrintf("  statement: \"%s\",%d)\n",
			debugstr.getString(),(int)statementtextlength);
	#endif
	stmt->cur->prepareQuery((const char *)statementtext,
						statementtextlength);

	// the statement has not been executed yet
	stmt->executed=false;
	stmt->executedbynumresultcols=false;

	return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLPutData(SQLHSTMT statementhandle,
					SQLPOINTER Data,
					SQLLEN StrLen_or_Ind) {
	debugFunction();

	STMT	*stmt=(STMT *)statementhandle;
	if (statementhandle==SQL_NULL_HSTMT || !stmt || !stmt->cur) {
		debugPrintf("  NULL stmt handle\n");
		return SQL_INVALID_HANDLE;
	}

	// not supported
	SQLR_STMTSetError(stmt,
			"Driver does not support this function",0,"IM001");

	return SQL_ERROR;
}

SQLRETURN SQL_API SQLRowCount(SQLHSTMT statementhandle,
					SQLLEN *rowcount) {
	debugFunction();

	STMT	*stmt=(STMT *)statementhandle;
	if (statementhandle==SQL_NULL_HSTMT || !stmt || !stmt->cur) {
		debugPrintf("  NULL stmt handle\n");
		return SQL_INVALID_HANDLE;
	}

	*rowcount=stmt->cur->affectedRows();

	return SQL_SUCCESS;
}

static SQLRETURN SQLR_SQLSetConnectAttr(SQLHDBC connectionhandle,
					SQLINTEGER attribute,
					SQLPOINTER value,
					SQLINTEGER stringlength) {
	debugFunction();

	CONN	*conn=(CONN *)connectionhandle;
	if (connectionhandle==SQL_NULL_HANDLE || !conn || !conn->con) {
		debugPrintf("  NULL conn handle\n");
		return SQL_INVALID_HANDLE;
	}

	switch (attribute) {
		#ifdef SQL_AUTOCOMMIT
		case SQL_AUTOCOMMIT:
		{
			debugPrintf("  attribute: SQL_AUTOCOMMIT\n");
			// use reinterpret_cast to avoid compiler warnings
			uint64_t	val=reinterpret_cast<uint64_t>(value);
			if (val==SQL_AUTOCOMMIT_ON) {
				if (conn->con->autoCommitOn()) {
					return SQL_SUCCESS;
				}
			} else if (val==SQL_AUTOCOMMIT_OFF) {
				if (conn->con->autoCommitOff()) {
					return SQL_SUCCESS;
				}
			}
		}
		#endif

		// FIXME: implement
 		/*case SQL_ACCESS_MODE:
		case SQL_LOGIN_TIMEOUT:
		case SQL_OPT_TRACE:
		case SQL_OPT_TRACEFILE:
		case SQL_TRANSLATE_DLL:
		case SQL_TRANSLATE_OPTION:
		case SQL_ODBC_CURSORS:
		case SQL_QUIET_MODE:
		case SQL_PACKET_SIZE:
	#if (ODBCVER >= 0x0300)
		case SQL_ATTR_CONNECTION_TIMEOUT:
		case SQL_ATTR_DISCONNECT_BEHAVIOR:
		case SQL_ATTR_ENLIST_IN_DTC:
		case SQL_ATTR_ENLIST_IN_XA:
		case SQL_ATTR_AUTO_IPD:
		case SQL_ATTR_METADATA_ID:
	#endif*/
		default:
			debugPrintf("  unsupported attribute: %d\n",attribute);
			return SQL_SUCCESS;
	}

	return SQL_ERROR;
}

SQLRETURN SQL_API SQLSetConnectAttr(SQLHDBC connectionhandle,
					SQLINTEGER attribute,
					SQLPOINTER value,
					SQLINTEGER stringlength) {
	debugFunction();
	return SQLR_SQLSetConnectAttr(connectionhandle,attribute,
						value,stringlength);
}

SQLRETURN SQL_API SQLSetConnectOption(SQLHDBC connectionhandle,
					SQLUSMALLINT option,
					SQLULEN value) {
	debugFunction();
	return SQLR_SQLSetConnectAttr(connectionhandle,option,
						(SQLPOINTER)value,0);
}

SQLRETURN SQL_API SQLSetCursorName(SQLHSTMT statementhandle,
					SQLCHAR *cursorname,
					SQLSMALLINT namelength) {
	debugFunction();

	STMT	*stmt=(STMT *)statementhandle;
	if (statementhandle==SQL_NULL_HSTMT || !stmt || !stmt->cur) {
		debugPrintf("  NULL stmt handle\n");
		return SQL_INVALID_HANDLE;
	}

	delete[] stmt->name;
	if (namelength==SQL_NTS) {
		stmt->name=charstring::duplicate((const char *)cursorname);
	} else {
		stmt->name=charstring::duplicate((const char *)cursorname,
								namelength);
	}

	return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLSetDescField(SQLHDESC DescriptorHandle,
					SQLSMALLINT RecNumber,
					SQLSMALLINT FieldIdentifier,
					SQLPOINTER Value,
					SQLINTEGER BufferLength) {
	debugFunction();
	// not supported
	return SQL_ERROR;
}

SQLRETURN SQL_API SQLSetDescRec(SQLHDESC DescriptorHandle,
					SQLSMALLINT RecNumber,
					SQLSMALLINT Type,
					SQLSMALLINT SubType,
					SQLLEN Length,
					SQLSMALLINT Precision,
					SQLSMALLINT Scale,
					SQLPOINTER Data,
					SQLLEN *StringLength,
					SQLLEN *Indicator) {
	debugFunction();
	// not supported
	return SQL_ERROR;
}

SQLRETURN SQL_API SQLSetEnvAttr(SQLHENV environmenthandle,
					SQLINTEGER attribute,
					SQLPOINTER value,
					SQLINTEGER stringlength) {
	debugFunction();

	ENV	*env=(ENV *)environmenthandle;
	if (environmenthandle==SQL_NULL_HENV || !env) {
		debugPrintf("  NULL env handle\n");
		return SQL_INVALID_HANDLE;
	}

	// use reinterpret_cast and assignment to smaller
	// sized value to avoid compiler warnings
	SQLUINTEGER	val=reinterpret_cast<uint64_t>(value);

	switch (attribute) {
		case SQL_ATTR_OUTPUT_NTS:
			debugPrintf("  attribute: SQL_ATTR_OUTPUT_NTS\n");
			// this can't be set to false
			return (val==SQL_TRUE)?SQL_SUCCESS:SQL_ERROR;
		case SQL_ATTR_ODBC_VERSION:
			debugPrintf("  attribute: SQL_ATTR_ODBC_VERSION\n");
			switch (val) {
				case SQL_OV_ODBC2:
					env->odbcversion=SQL_OV_ODBC2;
					break;
				#if (ODBCVER >= 0x0300)
				case SQL_OV_ODBC3:
					env->odbcversion=SQL_OV_ODBC3;
					break;
				#endif
			}
			debugPrintf("  odbcversion: %d\n",(int)env->odbcversion);
			return SQL_SUCCESS;
		case SQL_ATTR_CONNECTION_POOLING:
			debugPrintf("  attribute: SQL_ATTR_CONNECTION_POOLING\n");
			// this can't be set on
			return (val==SQL_CP_OFF)?SQL_SUCCESS:SQL_ERROR;
		case SQL_ATTR_CP_MATCH:
			debugPrintf("  attribute: SQL_ATTR_CP_MATCH\n");
			// this can't be set to anything but default
			return (val==SQL_CP_MATCH_DEFAULT)?
						SQL_SUCCESS:SQL_ERROR;
		default:
			debugPrintf("  unsupported attribute: %d\n",attribute);
			return SQL_SUCCESS;
	}
}

SQLRETURN SQL_API SQLSetParam(SQLHSTMT statementhandle,
					SQLUSMALLINT parameternumber,
					SQLSMALLINT valuetype,
					SQLSMALLINT parametertype,
					SQLULEN lengthprecision,
					SQLSMALLINT parameterscale,
					SQLPOINTER parametervalue,
					SQLLEN *strlen_or_ind) {
	debugFunction();
	return SQLR_SQLBindParameter(statementhandle,
					parameternumber,
					SQL_PARAM_INPUT,
					valuetype,
					parametertype,
					lengthprecision,
					parameterscale,
					parametervalue,
					0,
					strlen_or_ind);
}

static SQLRETURN SQLR_SQLSetStmtAttr(SQLHSTMT statementhandle,
					SQLINTEGER attribute,
					SQLPOINTER value,
					SQLINTEGER stringlength) {
	debugFunction();

	STMT	*stmt=(STMT *)statementhandle;
	if (statementhandle==SQL_NULL_HSTMT || !stmt || !stmt->cur) {
		debugPrintf("  NULL stmt handle\n");
		return SQL_INVALID_HANDLE;
	}

	switch (attribute) {
		#if (ODBCVER >= 0x0300)
		case SQL_ATTR_APP_ROW_DESC:
			debugPrintf("  attribute: SQL_ATTR_APP_ROW_DESC\n");
			stmt->approwdesc=(rowdesc *)value;
			if (stmt->approwdesc==SQL_NULL_DESC) {
				stmt->approwdesc=stmt->improwdesc;
			}
			return SQL_SUCCESS;
		case SQL_ATTR_APP_PARAM_DESC:
			debugPrintf("  attribute: SQL_ATTR_APP_PARAM_DESC\n");
			stmt->appparamdesc=(paramdesc *)value;
			if (stmt->appparamdesc==SQL_NULL_DESC) {
				stmt->appparamdesc=stmt->impparamdesc;
			}
			return SQL_SUCCESS;
		case SQL_ATTR_IMP_ROW_DESC:
			debugPrintf("  attribute: SQL_ATTR_IMP_ROW_DESC\n");
			// read-only
			return SQL_ERROR;
		case SQL_ATTR_IMP_PARAM_DESC:
			debugPrintf("  attribute: SQL_ATTR_IMP_PARAM_DESC\n");
			// read-only
			return SQL_ERROR;
		case SQL_ATTR_CURSOR_SCROLLABLE:
			debugPrintf("  attribute: SQL_ATTR_CURSOR_SCROLLABLE\n");
			// FIXME: implement
			return SQL_SUCCESS;
		case SQL_ATTR_CURSOR_SENSITIVITY:
			debugPrintf("  attribute: SQL_ATTR_CURSOR_SENSITIVITY\n");
			// FIXME: implement
			return SQL_SUCCESS;
		#endif
		//case SQL_ATTR_QUERY_TIMEOUT:
		case SQL_QUERY_TIMEOUT:
			debugPrintf("  attribute: "
					"SQL_ATTR_QUERY_TIMEOUT/"
					"SQL_QUERY_TIMEOUT\n");
			// FIXME: implement
			return SQL_SUCCESS;
		//case SQL_ATTR_MAX_ROWS:
		case SQL_MAX_ROWS:
			debugPrintf("  attribute: "
					"SQL_ATTR_MAX_ROWS/"
					"SQL_MAX_ROWS\n");
			// FIXME: implement
			return SQL_SUCCESS;
		//case SQL_ATTR_NOSCAN:
		case SQL_NOSCAN:
			debugPrintf("  attribute: "
					"SQL_ATTR_NOSCAN/"
					"SQL_NOSCAN\n");
			// FIXME: implement
			return SQL_SUCCESS;
		//case SQL_ATTR_MAX_LENGTH:
		case SQL_MAX_LENGTH:
			debugPrintf("  attribute: "
					"SQL_ATTR_MAX_LENGTH/"
					"SQL_MAX_LENGTH\n");
			// FIXME: implement
			return SQL_SUCCESS;
		case SQL_ASYNC_ENABLE:
			debugPrintf("  attribute: SQL_ASYNC_ENABLE\n");
			// FIXME: implement
			return SQL_SUCCESS;
		//case SQL_ATTR_ROW_BIND_TYPE:
		case SQL_BIND_TYPE:
			debugPrintf("  attribute: "
					"SQL_ATTR_ROW_BIND_TYPE/"
					"SQL_BIND_TYPE: "
					"%lld\n",(uint64_t)(value));
			stmt->rowbindtype=(SQLULEN)value;
			return SQL_SUCCESS;
		//case SQL_ATTR_CONCURRENCY:
		//case SQL_ATTR_CURSOR_TYPE:
		case SQL_CURSOR_TYPE:
			debugPrintf("  attribute: "
					"SQL_ATTR_CONCURRENCY/"
					"SQL_ATTR_CURSOR_TYPE/"
					"SQL_CURSOR_TYPE\n");
			// FIXME: implement
			return SQL_SUCCESS;
		case SQL_CONCURRENCY:
			debugPrintf("  attribute: SQL_CONCURRENCY\n");
			// FIXME: implement
			return SQL_SUCCESS;
		//case SQL_ATTR_KEYSET_SIZE:
		case SQL_KEYSET_SIZE:
			debugPrintf("  attribute: "
					"SQL_ATTR_KEYSET_SIZE/"
					"SQL_KEYSET_SIZE\n");
			// FIXME: implement
			return SQL_SUCCESS;
		case SQL_ROWSET_SIZE:
			debugPrintf("  attribute: SQL_ROWSET_SIZE\n");
			// FIXME: implement
			return SQL_SUCCESS;
		//case SQL_ATTR_SIMULATE_CURSOR:
		case SQL_SIMULATE_CURSOR:
			debugPrintf("  attribute: "
					"SQL_ATTR_SIMULATE_CURSOR/"
					"SQL_SIMULATE_CURSOR\n");
			// FIXME: implement
			return SQL_SUCCESS;
		//case SQL_ATTR_RETRIEVE_DATA:
		case SQL_RETRIEVE_DATA:
			debugPrintf("  attribute: "
					"SQL_ATTR_RETRIEVE_DATA/"
					"SQL_RETRIEVE_DATA\n");
			// FIXME: implement
			return SQL_SUCCESS;
		//case SQL_ATTR_USE_BOOKMARKS:
		case SQL_USE_BOOKMARKS:
			debugPrintf("  attribute: "
					"SQL_ATTR_USE_BOOKMARKS/"
					"SQL_USE_BOOKMARKS\n");
			// FIXME: implement
			return SQL_SUCCESS;
		case SQL_GET_BOOKMARK:
			debugPrintf("  attribute: SQL_GET_BOOKMARK\n");
			// FIXME: implement
			return SQL_SUCCESS;
		//case SQL_ATTR_ROW_NUMBER:
		case SQL_ROW_NUMBER:
			debugPrintf("  attribute: "
					"SQL_ATTR_ROW_NUMBER/"
					"SQL_ROW_NUMBER\n");
			// read-only
			return SQL_ERROR;
		#if (ODBCVER >= 0x0300)
		case SQL_ATTR_ENABLE_AUTO_IPD:
			debugPrintf("  attribute: SQL_ATTR_ENABLE_AUTO_IPD\n");
			// FIXME: implement
			return SQL_SUCCESS;
		case SQL_ATTR_FETCH_BOOKMARK_PTR:
			debugPrintf("  attribute: SQL_ATTR_FETCH_BOOKMARK_PTR\n");
			// FIXME: implement
			return SQL_SUCCESS;
		case SQL_ATTR_PARAM_BIND_OFFSET_PTR:
			debugPrintf("  attribute: "
					"SQL_ATTR_PARAM_BIND_OFFSET_PTR\n");
			// FIXME: implement
			return SQL_SUCCESS;
		case SQL_ATTR_PARAM_BIND_TYPE:
			debugPrintf("  attribute: SQL_ATTR_PARAM_BIND_TYPE\n");
			// FIXME: implement
			return SQL_SUCCESS;
		case SQL_ATTR_PARAM_OPERATION_PTR:
			debugPrintf("  attribute: "
					"SQL_ATTR_PARAM_OPERATION_PTR\n");
			// FIXME: implement
			return SQL_SUCCESS;
		case SQL_ATTR_PARAM_STATUS_PTR:
			debugPrintf("  attribute: SQL_ATTR_PARAM_STATUS_PTR\n");
			// FIXME: implement
			return SQL_SUCCESS;
		case SQL_ATTR_PARAMS_PROCESSED_PTR:
			debugPrintf("  attribute: "
					"SQL_ATTR_PARAMS_PROCESSED_PTR\n");
			// FIXME: implement
			return SQL_SUCCESS;
		case SQL_ATTR_PARAMSET_SIZE:
			debugPrintf("  attribute: SQL_ATTR_PARAMSET_SIZE\n");
			// FIXME: implement
			return SQL_SUCCESS;
		case SQL_ATTR_ROW_BIND_OFFSET_PTR:
			debugPrintf("  attribute: "	
					"SQL_ATTR_ROW_BIND_OFFSET_PTR\n");
			// FIXME: implement
			return SQL_SUCCESS;
		case SQL_ATTR_ROW_OPERATION_PTR:
			debugPrintf("  attribute: SQL_ATTR_ROW_OPERATION_PTR\n");
			// FIXME: implement
			return SQL_SUCCESS;
		case SQL_ATTR_ROW_STATUS_PTR:
			debugPrintf("  attribute: SQL_ATTR_ROW_STATUS_PTR\n");
			stmt->rowstatusptr=(SQLUSMALLINT *)value;
			return SQL_SUCCESS;
		case SQL_ATTR_ROWS_FETCHED_PTR:
			debugPrintf("  attribute: SQL_ATTR_ROWS_FETCHED_PTR\n");
			stmt->rowsfetchedptr=(SQLROWSETSIZE *)value;
			return SQL_SUCCESS;
		case SQL_ATTR_ROW_ARRAY_SIZE:
			debugPrintf("  attribute: SQL_ATTR_ROW_ARRAY_SIZE: "
						"%lld\n",(uint64_t)(value));
			stmt->cur->setResultSetBufferSize((uint64_t)value);
			return SQL_SUCCESS;
		#endif
		#if (ODBCVER < 0x0300)
		case SQL_STMT_OPT_MAX:
			debugPrintf("  attribute: SQL_STMT_OPT_MAX\n");
			// FIXME: implement
			return SQL_SUCCESS;
		case SQL_STMT_OPT_MIN:
			debugPrintf("  attribute: SQL_STMT_OPT_MIN\n");
			// FIXME: implement
			return SQL_SUCCESS;
		#endif
		default:
			return SQL_ERROR;
	}
}

SQLRETURN SQL_API SQLSetStmtAttr(SQLHSTMT statementhandle,
					SQLINTEGER attribute,
					SQLPOINTER value,
					SQLINTEGER stringlength) {
	debugFunction();
	return SQLR_SQLSetStmtAttr(statementhandle,attribute,
						value,stringlength);
}

SQLRETURN SQL_API SQLSetStmtOption(SQLHSTMT statementhandle,
					SQLUSMALLINT option,
					SQLULEN value) {
	debugFunction();
	return SQLR_SQLSetStmtAttr(statementhandle,option,
						(SQLPOINTER)value,0);
}

SQLRETURN SQL_API SQLSpecialColumns(SQLHSTMT statementhandle,
					SQLUSMALLINT IdentifierType,
					SQLCHAR *CatalogName,
					SQLSMALLINT NameLength1,
					SQLCHAR *SchemaName,
					SQLSMALLINT NameLength2,
					SQLCHAR *TableName,
					SQLSMALLINT NameLength3,
					SQLUSMALLINT Scope,
					SQLUSMALLINT Nullable) {
	debugFunction();

	STMT	*stmt=(STMT *)statementhandle;
	if (statementhandle==SQL_NULL_HSTMT || !stmt || !stmt->cur) {
		debugPrintf("  NULL stmt handle\n");
		return SQL_INVALID_HANDLE;
	}

	// not supported
	SQLR_STMTSetError(stmt,
			"Driver does not support this function",0,"IM001");

	return SQL_ERROR;
}

SQLRETURN SQL_API SQLStatistics(SQLHSTMT statementhandle,
					SQLCHAR *CatalogName,
					SQLSMALLINT NameLength1,
					SQLCHAR *SchemaName,
					SQLSMALLINT NameLength2,
					SQLCHAR *TableName,
					SQLSMALLINT NameLength3,
					SQLUSMALLINT Unique,
					SQLUSMALLINT Reserved) {
	debugFunction();

	STMT	*stmt=(STMT *)statementhandle;
	if (statementhandle==SQL_NULL_HSTMT || !stmt || !stmt->cur) {
		debugPrintf("  NULL stmt handle\n");
		return SQL_INVALID_HANDLE;
	}

	// not supported
	SQLR_STMTSetError(stmt,
			"Driver does not support this function",0,"IM001");

	return SQL_ERROR;
}

SQLRETURN SQL_API SQLTables(SQLHSTMT statementhandle,
					SQLCHAR *catalogname,
					SQLSMALLINT namelength1,
					SQLCHAR *schemaname,
					SQLSMALLINT namelength2,
					SQLCHAR *tablename,
					SQLSMALLINT namelength3,
					SQLCHAR *tabletype,
					SQLSMALLINT namelength4) {
	debugFunction();

	STMT	*stmt=(STMT *)statementhandle;
	if (statementhandle==SQL_NULL_HSTMT || !stmt) {
		debugPrintf("  NULL stmt handle\n");
		return SQL_INVALID_HANDLE;
	}

	char	*wild=NULL;
	if (namelength3==SQL_NTS) {
		wild=charstring::duplicate((const char *)tablename);
	} else {
		wild=charstring::duplicate((const char *)tablename,
							namelength3);
	}
	debugPrintf("  wild: %s\n",(wild)?wild:"");

	SQLRETURN	retval=
		(stmt->cur->getTableList(wild,SQLRCLIENTLISTFORMAT_ODBC))?
							SQL_SUCCESS:SQL_ERROR;
	delete[] wild;
	return retval;
}

static SQLRETURN SQLR_SQLEndTran(SQLSMALLINT handletype,
					SQLHANDLE handle,
					SQLSMALLINT completiontype);

SQLRETURN SQL_API SQLTransact(SQLHENV environmenthandle,
					SQLHDBC connectionhandle,
					SQLUSMALLINT completiontype) {
	debugFunction();
	if (connectionhandle) {
		return SQLR_SQLEndTran(SQL_HANDLE_DBC,
					connectionhandle,
					completiontype);
	} else if (environmenthandle) {
		return SQLR_SQLEndTran(SQL_HANDLE_ENV,
					environmenthandle,
					completiontype);
	} else {
		debugPrintf("  no valid handle\n");
		return SQL_INVALID_HANDLE;
	}
}

SQLRETURN SQL_API SQLDriverConnect(SQLHDBC hdbc,
					SQLHWND hwnd,
					SQLCHAR *szconnstrin,
					SQLSMALLINT cbconnstrin,
					SQLCHAR *szconnstrout,
					SQLSMALLINT cbconnstroutmax,
					SQLSMALLINT *pcbconnstrout,
					SQLUSMALLINT fdrivercompletion) {
	debugFunction();

	CONN	*conn=(CONN *)hdbc;
	if (hdbc==SQL_NULL_HANDLE || !conn) {
		debugPrintf("  NULL conn handle\n");
		return SQL_INVALID_HANDLE;
	}

	// the connect string may not be null terminated, so make a copy that is
	char	*nulltermconnstr;
	if (cbconnstrin==SQL_NTS) {
		nulltermconnstr=charstring::duplicate(
					(const char *)szconnstrin);
	} else {
		nulltermconnstr=charstring::duplicate(
					(const char *)szconnstrin,
					cbconnstrin);
	}
	debugPrintf("  connectstring: %s\n",nulltermconnstr);

	// parse out DSN, UID and PWD from the connect string
	parameterstring	pstr;
	pstr.parse(nulltermconnstr);
	const char	*dsn=pstr.getValue("DSN");
	if (!charstring::length(dsn)) {
		dsn=pstr.getValue("dsn");
	}
	const char	*username=pstr.getValue("UID");
	if (!charstring::length(username)) {
		username=pstr.getValue("uid");
	}
	const char	*authentication=pstr.getValue("PWD");
	if (!charstring::length(authentication)) {
		authentication=pstr.getValue("pwd");
	}

	debugPrintf("  dsn: %s\n",dsn);
	debugPrintf("  username: %s\n",username);
	debugPrintf("  authentication: %s\n",authentication);

	// for now, don't do any prompting...
	switch (fdrivercompletion) {
		case SQL_DRIVER_PROMPT:
			debugPrintf("  fbdrivercompletion: "
					"SQL_DRIVER_PROMPT\n");
			break;
		case SQL_DRIVER_COMPLETE:
			debugPrintf("  fbdrivercompletion: "
					"SQL_DRIVER_COMPLETE\n");
			break;
		case SQL_DRIVER_COMPLETE_REQUIRED:
			debugPrintf("  fbdrivercompletion: "
					"SQL_DRIVER_COMPLETE_REQUIRED\n");
			break;
		case SQL_DRIVER_NOPROMPT:
			debugPrintf("  fbdrivercompletion: "
					"SQL_DRIVER_NOPROMPT\n");
			break;
	}

	// the dsn must be valid
	if (!charstring::length(dsn)) {
		return SQL_ERROR;
	}

	// since we don't support prompting and updating the connect string...
	if (cbconnstrin==SQL_NTS) {
		*pcbconnstrout=charstring::length(szconnstrin);
	} else {
		*pcbconnstrout=cbconnstrin;
	}
	*pcbconnstrout=cbconnstrin;
	charstring::safeCopy((char *)szconnstrout,
				*pcbconnstrout,nulltermconnstr);

	// connect
	SQLRETURN	retval=SQLR_SQLConnect(hdbc,
					(SQLCHAR *)dsn,
					charstring::length(dsn),
					(SQLCHAR *)username,
					charstring::length(username),
					(SQLCHAR *)authentication,
					charstring::length(authentication));

	// clean up
	delete[] nulltermconnstr;

	return retval;
}

SQLRETURN SQL_API SQLBulkOperations(SQLHSTMT statementhandle,
					SQLSMALLINT Operation) {
	debugFunction();

	STMT	*stmt=(STMT *)statementhandle;
	if (statementhandle==SQL_NULL_HSTMT || !stmt || !stmt->cur) {
		debugPrintf("  NULL stmt handle\n");
		return SQL_INVALID_HANDLE;
	}

	// not supported
	SQLR_STMTSetError(stmt,
			"Driver does not support this function",0,"IM001");

	return SQL_ERROR;
}

SQLRETURN SQL_API SQLColAttributes(SQLHSTMT statementhandle,
					SQLUSMALLINT icol,
					SQLUSMALLINT fdesctype,
					SQLPOINTER rgbdesc,
					SQLSMALLINT cbdescmax,
					SQLSMALLINT *pcbdesc,
					SQLLEN *pfdesc) {
	debugFunction();
	return SQLR_SQLColAttribute(statementhandle,
					icol,
					fdesctype,
					rgbdesc,
					cbdescmax,
					pcbdesc,
					(NUMERICATTRIBUTETYPE)pfdesc);
}

SQLRETURN SQL_API SQLColumnPrivileges(SQLHSTMT statementhandle,
					SQLCHAR *szCatalogName,
					SQLSMALLINT cbCatalogName,
					SQLCHAR *szSchemaName,
					SQLSMALLINT cbSchemaName,
					SQLCHAR *szTableName,
					SQLSMALLINT cbTableName,
					SQLCHAR *szColumnName,
					SQLSMALLINT cbColumnName) {
	debugFunction();

	STMT	*stmt=(STMT *)statementhandle;
	if (statementhandle==SQL_NULL_HSTMT || !stmt || !stmt->cur) {
		debugPrintf("  NULL stmt handle\n");
		return SQL_INVALID_HANDLE;
	}

	// not supported
	SQLR_STMTSetError(stmt,
			"Driver does not support this function",0,"IM001");

	return SQL_ERROR;
}

SQLRETURN SQL_API SQLDescribeParam(SQLHSTMT statementhandle,
					SQLUSMALLINT ipar,
					SQLSMALLINT *pfSqlType,
					SQLULEN *pcbParamDef,
					SQLSMALLINT *pibScale,
					SQLSMALLINT *pfNullable) {
	debugFunction();

	STMT	*stmt=(STMT *)statementhandle;
	if (statementhandle==SQL_NULL_HSTMT || !stmt || !stmt->cur) {
		debugPrintf("  NULL stmt handle\n");
		return SQL_INVALID_HANDLE;
	}

	// not supported
	SQLR_STMTSetError(stmt,
			"Driver does not support this function",0,"IM001");

	return SQL_ERROR;
}

#ifdef HAVE_SQLEXTENDEDFETCH_LEN
SQLRETURN SQL_API SQLExtendedFetch(SQLHSTMT statementhandle,
					SQLUSMALLINT fetchorientation,
					SQLLEN fetchoffset,
					SQLULEN *pcrow,
					SQLUSMALLINT *rgfrowstatus) {
	debugFunction();
	return SQLR_Fetch(statementhandle,pcrow,rgfrowstatus);
}
#else
SQLRETURN SQL_API SQLExtendedFetch(SQLHSTMT statementhandle,
					SQLUSMALLINT fetchorientation,
					SQLROWOFFSET fetchoffset,
					SQLROWSETSIZE *pcrow,
					SQLUSMALLINT *rgfrowstatus) {
	debugFunction();
	return SQLR_Fetch(statementhandle,(SQLULEN *)pcrow,rgfrowstatus);
}
#endif

SQLRETURN SQL_API SQLForeignKeys(SQLHSTMT statementhandle,
					SQLCHAR *szPkCatalogName,
					SQLSMALLINT cbPkCatalogName,
					SQLCHAR *szPkSchemaName,
					SQLSMALLINT cbPkSchemaName,
					SQLCHAR *szPkTableName,
					SQLSMALLINT cbPkTableName,
					SQLCHAR *szFkCatalogName,
					SQLSMALLINT cbFkCatalogName,
					SQLCHAR *szFkSchemaName,
					SQLSMALLINT cbFkSchemaName,
					SQLCHAR *szFkTableName,
					SQLSMALLINT cbFkTableName) {
	debugFunction();

	STMT	*stmt=(STMT *)statementhandle;
	if (statementhandle==SQL_NULL_HSTMT || !stmt || !stmt->cur) {
		debugPrintf("  NULL stmt handle\n");
		return SQL_INVALID_HANDLE;
	}

	// not supported
	SQLR_STMTSetError(stmt,
			"Driver does not support this function",0,"IM001");

	return SQL_ERROR;
}

SQLRETURN SQL_API SQLMoreResults(SQLHSTMT statementhandle) {
	debugFunction();
	// only supports fetching the first result set of a query
	return SQL_NO_DATA_FOUND;
}

SQLRETURN SQL_API SQLNativeSql(SQLHDBC hdbc,
					SQLCHAR *szSqlStrIn,
					SQLINTEGER cbSqlStrIn,
					SQLCHAR *szSqlStr,
					SQLINTEGER cbSqlStrMax,
					SQLINTEGER *pcbSqlStr) {
	debugFunction();
	// not supported
	return SQL_ERROR;
}

SQLRETURN SQL_API SQLNumParams(SQLHSTMT statementhandle,
					SQLSMALLINT *pcpar) {
	debugFunction();

	STMT	*stmt=(STMT *)statementhandle;
	if (statementhandle==SQL_NULL_HSTMT || !stmt || !stmt->cur) {
		debugPrintf("  NULL stmt handle\n");
		return SQL_INVALID_HANDLE;
	}

	*pcpar=stmt->cur->countBindVariables();

	return SQL_SUCCESS;
}

static SQLRETURN SQLR_SQLSetStmtAttr(SQLHSTMT statementhandle,
					SQLINTEGER attribute,
					SQLPOINTER value,
					SQLINTEGER stringlength);

SQLRETURN SQL_API SQLParamOptions(SQLHSTMT statementhandle,
					SQLULEN crow,
					SQLULEN *pirow) {
	debugFunction();
	return (SQLR_SQLSetStmtAttr(statementhandle,
				SQL_ATTR_PARAMSET_SIZE,
				(SQLPOINTER)crow,0)==SQL_SUCCESS &&
		SQLR_SQLSetStmtAttr(statementhandle,
				SQL_ATTR_PARAMS_PROCESSED_PTR,
				(SQLPOINTER)pirow,0)==SQL_SUCCESS)?
				SQL_SUCCESS:SQL_ERROR;
}

SQLRETURN SQL_API SQLPrimaryKeys(SQLHSTMT statementhandle,
					SQLCHAR *szCatalogName,
					SQLSMALLINT cbCatalogName,
					SQLCHAR *szSchemaName,
					SQLSMALLINT cbSchemaName,
					SQLCHAR *szTableName,
					SQLSMALLINT cbTableName) {
	debugFunction();

	STMT	*stmt=(STMT *)statementhandle;
	if (statementhandle==SQL_NULL_HSTMT || !stmt || !stmt->cur) {
		debugPrintf("  NULL stmt handle\n");
		return SQL_INVALID_HANDLE;
	}

	// not supported
	SQLR_STMTSetError(stmt,
			"Driver does not support this function",0,"IM001");

	return SQL_ERROR;
}

SQLRETURN SQL_API SQLProcedureColumns(SQLHSTMT statementhandle,
					SQLCHAR *szCatalogName,
					SQLSMALLINT cbCatalogName,
					SQLCHAR *szSchemaName,
					SQLSMALLINT cbSchemaName,
					SQLCHAR *szProcName,
					SQLSMALLINT cbProcName,
					SQLCHAR *szColumnName,
					SQLSMALLINT cbColumnName) {
	debugFunction();

	STMT	*stmt=(STMT *)statementhandle;
	if (statementhandle==SQL_NULL_HSTMT || !stmt || !stmt->cur) {
		debugPrintf("  NULL stmt handle\n");
		return SQL_INVALID_HANDLE;
	}

	// not supported
	SQLR_STMTSetError(stmt,
			"Driver does not support this function",0,"IM001");

	return SQL_ERROR;
}

SQLRETURN SQL_API SQLProcedures(SQLHSTMT statementhandle,
					SQLCHAR *szCatalogName,
					SQLSMALLINT cbCatalogName,
					SQLCHAR *szSchemaName,
					SQLSMALLINT cbSchemaName,
					SQLCHAR *szProcName,
					SQLSMALLINT cbProcName) {
	debugFunction();

	STMT	*stmt=(STMT *)statementhandle;
	if (statementhandle==SQL_NULL_HSTMT || !stmt || !stmt->cur) {
		debugPrintf("  NULL stmt handle\n");
		return SQL_INVALID_HANDLE;
	}

	// not supported
	SQLR_STMTSetError(stmt,
			"Driver does not support this function",0,"IM001");

	return SQL_ERROR;
}

SQLRETURN SQL_API SQLSetPos(SQLHSTMT statementhandle,
					SQLSETPOSIROW irow,
					SQLUSMALLINT foption,
					SQLUSMALLINT flock) {
	debugFunction();

	STMT	*stmt=(STMT *)statementhandle;
	if (statementhandle==SQL_NULL_HSTMT || !stmt || !stmt->cur) {
		debugPrintf("  NULL stmt handle\n");
		return SQL_INVALID_HANDLE;
	}

	if (foption==SQL_POSITION) {
		if (!irow) {
			irow=1;
		}
		stmt->currentgetdatarow=stmt->currentstartrow+irow-1;
		debugPrintf("  currentgetdatarow=%lld\n",
				stmt->currentgetdatarow);
		return SQL_SUCCESS;
	}

	SQLR_STMTSetError(stmt,
			"Driver does not support this function",0,"IM001");
	return SQL_ERROR;
}

SQLRETURN SQL_API SQLTablePrivileges(SQLHSTMT statementhandle,
					SQLCHAR *szCatalogName,
					SQLSMALLINT cbCatalogName,
					SQLCHAR *szSchemaName,
					SQLSMALLINT cbSchemaName,
					SQLCHAR *szTableName,
					SQLSMALLINT cbTableName) {
	debugFunction();

	STMT	*stmt=(STMT *)statementhandle;
	if (statementhandle==SQL_NULL_HSTMT || !stmt || !stmt->cur) {
		debugPrintf("  NULL stmt handle\n");
		return SQL_INVALID_HANDLE;
	}

	// not supported
	SQLR_STMTSetError(stmt,
			"Driver does not support this function",0,"IM001");

	return SQL_ERROR;
}

#if (ODBCVER < 0x0300)
SQLRETURN SQL_API SQLDrivers(SQLHENV environmenthandle,
					SQLUSMALLINT fDirection,
					SQLCHAR *szDriverDesc,
					SQLSMALLINT cbDriverDescMax,
					SQLSMALLINT *pcbDriverDesc,
					SQLCHAR *szDriverAttributes,
					SQLSMALLINT cbDrvrAttrMax,
					SQLSMALLINT *pcbDrvrAttr) {
	debugFunction();

	// FIXME: this is allegedly mapped in ODBC3 but I can't tell what to

	STMT	*stmt=(STMT *)statementhandle;
	if (statementhandle==SQL_NULL_HSTMT || !stmt || !stmt->cur) {
		debugPrintf("  NULL stmt handle\n");
		return SQL_INVALID_HANDLE;
	}

	// not supported
	SQLR_STMTSetError(stmt,
			"Driver does not support this function",0,"IM001");

	return SQL_ERROR;
}
#endif

static const char *SQLR_BuildNumeric(STMT *stmt,
					int32_t parameternumber,
					SQL_NUMERIC_STRUCT *ns) {
	debugFunction();

	// Get the numeric array as a base-10 number. It should be OK to
	// convert it to a 64-bit integer as SQL_MAX_NUMERIC_LEN should be 16
	// or less.
	//
	//  A number is stored in the val field of the SQL_NUMERIC_STRUCT
	//  structure as a scaled integer, in little endian mode (the leftmost
	//  byte being the least-significant byte). For example, the number
	//  10.001 base 10, with a scale of 4, is scaled to an integer of
	//  100010. Because this is 186AA in hexadecimal format, the value in
	//  SQL_NUMERIC_STRUCT would be "AA 86 01 00 00 ... 00", with the number
	//  of bytes defined by the SQL_MAX_NUMERIC_LEN #define...
	uint64_t	num=0;
	for (int8_t i=SQL_MAX_NUMERIC_LEN; i>=0; i--) {
		num=num*16+ns->val[i];
	}

	// build up a string from that number
	uint8_t	size=ns->precision+ns->sign+((ns->scale>0)?1:0);
	char	*string=new char[size+1];
	string[size]='\0';
	uint8_t	index=size-1;
	for (uint8_t i=0; i<ns->scale; i++) {
		string[index]=num%10+'0';
		num=num/10;
		index--;
	}
	if (ns->scale) {
		string[index]='.';
	}
	uint8_t	tens=ns->precision-ns->scale;
	for (uint8_t i=0; i<tens; i++) {
		string[index]=num%10+'0';
		num=num/10;
		index--;
	}
	if (ns->sign) {
		string[index]='-';
	}

	// hang on to that string
	char	*data=NULL;
	if (stmt->inputbindstrings.getValue(parameternumber,&data)) {
		stmt->inputbindstrings.remove(parameternumber);
		delete[] data;
	}
	stmt->inputbindstrings.setValue(parameternumber,string);

	// return the string
	return string;
}

static const char *SQLR_BuildInterval(STMT *stmt,
				int32_t parameternumber,
				SQL_INTERVAL_STRUCT *is) {
	debugFunction();

	// create a string to store the built-up interval
	char	*string=new char[1];

	// FIXME: implement
	string[0]='\0';

	//typedef struct tagSQL_INTERVAL_STRUCT
	//   {
	//   SQLINTERVAL interval_type;
	//   SQLSMALLINT   interval_sign;
	//   union
	//      {
	//      SQL_YEAR_MONTH_STRUCT year_month;
	//      SQL_DAY_SECOND_STRUCT day_second;
	//      } intval;
	//   }SQLINTERVAL_STRUCT;
	//
	//typedef enum
	//   {
	//   SQL_IS_YEAR=1,
	//   SQL_IS_MONTH=2,
	//   SQL_IS_DAY=3,
	//   SQL_IS_HOUR=4,
	//   SQL_IS_MINUTE=5,
	//   SQL_IS_SECOND=6,
	//   SQL_IS_YEAR_TO_MONTH=7,
	//   SQL_IS_DAY_TO_HOUR=8,
	//   SQL_IS_DAY_TO_MINUTE=9,
	//   SQL_IS_DAY_TO_SECOND=10,
	//   SQL_IS_HOUR_TO_MINUTE=11,
	//   SQL_IS_HOUR_TO_SECOND=12,
	//   SQL_IS_MINUTE_TO_SECOND=13,
	//   }SQLINTERVAL;
	//
	//typedef struct tagSQL_YEAR_MONTH
	//   {
	//   SQLUINTEGER year;
	//   SQLUINTEGER month;
	//   }SQL_YEAR_MOHTH_STRUCT;
	//
	//typedef struct tagSQL_DAY_SECOND
	//   {
	//   SQLUINTEGER day;
	//   SQLUNINTEGER hour;
	//   SQLUINTEGER minute;
	//   SQLUINTEGER second;
	//   SQLUINTEGER fraction;
	//   }SQL_DAY_SECOND_STRUCT;

	// hang on to that string
	char	*data=NULL;
	if (stmt->inputbindstrings.getValue(parameternumber,&data)) {
		stmt->inputbindstrings.remove(parameternumber);
		delete[] data;
	}
	stmt->inputbindstrings.setValue(parameternumber,string);

	// return the string
	return string;
}

static char SQLR_HexToChar(const char input) {
	debugFunction();
	char	ch=input;
	if (ch>=0 && ch<=9) {
		ch=ch+'0';
	} else if (ch>=10 && ch<=16) {
		ch=ch-10+'A';
	} else {
		ch='0';
	}
	return ch;
}

static const char *SQLR_BuildGuid(STMT *stmt,
				int32_t parameternumber, SQLGUID *guid) {
	debugFunction();

	// create a string to store the built-up guid
	char	*string=new char[37];

	// decode the guid struct
	uint32_t	data1=guid->Data1;
	for (int16_t index=7; index>=0; index--) {
		string[index]=SQLR_HexToChar(data1%16);
		data1=data1/16;
	}
	string[8]='-';

	uint16_t	data2=guid->Data2;
	for (int16_t index=12; index>=9; index--) {
		string[index]=SQLR_HexToChar(data2%16);
		data2=data2/16;
	}
	string[13]='-';

	uint16_t	data3=guid->Data3;
	for (int16_t index=17; index>=14; index--) {
		string[index]=SQLR_HexToChar(data3%16);
		data3=data3/16;
	}
	string[18]='-';

	uint16_t	byte=0;
	for (uint16_t index=20; index<36; index=index+2) {
		unsigned char	data=guid->Data4[byte];
		string[index+1]=SQLR_HexToChar(data%16);
		data=data/16;
		string[index]=SQLR_HexToChar(data%16);
		byte++;
	}

	// hang on to that string
	char	*data=NULL;
	if (stmt->inputbindstrings.getValue(parameternumber,&data)) {
		stmt->inputbindstrings.remove(parameternumber);
		delete[] data;
	}
	stmt->inputbindstrings.setValue(parameternumber,string);

	// return the string
	return string;
}

static SQLRETURN SQLR_InputBindParameter(SQLHSTMT statementhandle,
					SQLUSMALLINT parameternumber,
					SQLSMALLINT valuetype,
					SQLULEN lengthprecision,
					SQLSMALLINT parameterscale,
					SQLPOINTER parametervalue,
					SQLLEN *strlen_or_ind) {
	debugFunction();

	STMT	*stmt=(STMT *)statementhandle;
	if (statementhandle==SQL_NULL_HSTMT || !stmt || !stmt->cur) {
		debugPrintf("  NULL stmt handle\n");
		return SQL_INVALID_HANDLE;
	}

	SQLRETURN	retval=SQL_SUCCESS;

	// convert parameternumber to a string
	char	*parametername=charstring::parseNumber(parameternumber);

	switch (valuetype) {
		case SQL_C_CHAR:
			debugPrintf("  valuetype: SQL_C_CHAR\n");
			stmt->cur->inputBind(parametername,
					(const char *)parametervalue);
			break;
		case SQL_C_LONG:
			debugPrintf("  valuetype: SQL_C_LONG\n");
			stmt->cur->inputBind(parametername,
					(int64_t)(*((long *)parametervalue)));
			break;
		case SQL_C_SHORT:
			debugPrintf("  valuetype: SQL_C_SHORT\n");
			stmt->cur->inputBind(parametername,
					(int64_t)(*((short *)parametervalue)));
			break;
		case SQL_C_FLOAT:
			debugPrintf("  valuetype: SQL_C_FLOAT\n");
			stmt->cur->inputBind(parametername,
					(float)(*((double *)parametervalue)),
					lengthprecision,
					parameterscale);
			break;
		case SQL_C_DOUBLE:
			debugPrintf("  valuetype: SQL_C_DOUBLE\n");
			stmt->cur->inputBind(parametername,
					*((double *)parametervalue),
					lengthprecision,
					parameterscale);
			break;
		case SQL_C_NUMERIC:
			debugPrintf("  valuetype: SQL_C_NUMERIC\n");
			stmt->cur->inputBind(parametername,
				SQLR_BuildNumeric(stmt,parameternumber,
					(SQL_NUMERIC_STRUCT *)parametervalue));
			break;
		case SQL_C_DATE:
		case SQL_C_TYPE_DATE:
			{
			debugPrintf("  valuetype: SQL_C_DATE/SQL_C_TYPE_DATE\n");
			DATE_STRUCT	*ds=(DATE_STRUCT *)parametervalue;
			stmt->cur->inputBind(parametername,
						ds->year,ds->month,ds->day,
						0,0,0,0,NULL);
			}
			break;
		case SQL_C_TIME:
		case SQL_C_TYPE_TIME:
			{
			debugPrintf("  valuetype: SQL_C_TIME/SQL_C_TYPE_TIME\n");
			TIME_STRUCT	*ts=(TIME_STRUCT *)parametervalue;
			stmt->cur->inputBind(parametername,
						0,0,0,
						ts->hour,ts->minute,ts->second,
						0,NULL);
			break;
			}
		case SQL_C_TIMESTAMP:
		case SQL_C_TYPE_TIMESTAMP:
			{
			debugPrintf("  valuetype: "
				"SQL_C_TIMESTAMP/SQL_C_TYPE_TIMESTAMP\n");
			TIMESTAMP_STRUCT	*tss=
					(TIMESTAMP_STRUCT *)parametervalue;
			stmt->cur->inputBind(parametername,
					tss->year,tss->month,tss->day,
					tss->hour,tss->minute,tss->second,
					tss->fraction/10,NULL);
			break;
			}
		case SQL_C_INTERVAL_YEAR:
		case SQL_C_INTERVAL_MONTH:
		case SQL_C_INTERVAL_DAY:
		case SQL_C_INTERVAL_HOUR:
		case SQL_C_INTERVAL_MINUTE:
		case SQL_C_INTERVAL_SECOND:
		case SQL_C_INTERVAL_YEAR_TO_MONTH:
		case SQL_C_INTERVAL_DAY_TO_HOUR:
		case SQL_C_INTERVAL_DAY_TO_MINUTE:
		case SQL_C_INTERVAL_DAY_TO_SECOND:
		case SQL_C_INTERVAL_HOUR_TO_MINUTE:
		case SQL_C_INTERVAL_HOUR_TO_SECOND:
		case SQL_C_INTERVAL_MINUTE_TO_SECOND:
			debugPrintf("  valuetype: SQL_C_INTERVAL_XXX\n");
			stmt->cur->inputBind(parametername,
				SQLR_BuildInterval(stmt,parameternumber,
					(SQL_INTERVAL_STRUCT *)parametervalue));
			break;
		//case SQL_C_VARBOOKMARK:
		//	(dup of SQL_C_BINARY)
		case SQL_C_BINARY:
			debugPrintf("  valuetype: "
				"SQL_C_BINARY/SQL_C_VARBOOKMARK\n");
			stmt->cur->inputBindBlob(parametername,
					(const char *)parametervalue,
					lengthprecision);
			break;
		case SQL_C_BIT:
			debugPrintf("  valuetype: SQL_C_BIT\n");
			stmt->cur->inputBind(parametername,
				(charstring::contains("YyTt",
					(const char *)parametervalue) ||
				charstring::toInteger(
					(const char *)parametervalue))?"1":"0");
			break;
		case SQL_C_SBIGINT:
			debugPrintf("  valuetype: SQL_C_BIGINT\n");
			stmt->cur->inputBind(parametername,
				(int64_t)(*((int64_t *)parametervalue)));
			break;
		case SQL_C_UBIGINT:
			debugPrintf("  valuetype: SQL_C_UBIGINT\n");
			stmt->cur->inputBind(parametername,
				(int64_t)(*((uint64_t *)parametervalue)));
			break;
		case SQL_C_SLONG:
			debugPrintf("  valuetype: SQL_C_SLONG\n");
			stmt->cur->inputBind(parametername,
					(int64_t)(*((long *)parametervalue)));
			break;
		case SQL_C_SSHORT:
			debugPrintf("  valuetype: SQL_C_SSHORT\n");
			stmt->cur->inputBind(parametername,
					(int64_t)(*((short *)parametervalue)));
			break;
		case SQL_C_TINYINT:
		case SQL_C_STINYINT:
			debugPrintf("  valuetype: "
				"SQL_C_TINYINT/SQL_C_STINYINT\n");
			stmt->cur->inputBind(parametername,
					(int64_t)(*((char *)parametervalue)));
			break;
		//case SQL_C_BOOKMARK:
		//	(dup of SQL_C_ULONG)
		case SQL_C_ULONG:
			debugPrintf("  valuetype: SQL_C_ULONG/SQL_C_BOOKMARK\n");
			stmt->cur->inputBind(parametername,
				(int64_t)(*((unsigned long *)parametervalue)));
			break;
		case SQL_C_USHORT:
			debugPrintf("  valuetype: SQL_C_USHORT\n");
			stmt->cur->inputBind(parametername,
				(int64_t)(*((unsigned short *)parametervalue)));
			break;
		case SQL_C_UTINYINT:
			debugPrintf("  valuetype: SQL_C_UTINYINT\n");
			stmt->cur->inputBind(parametername,
				(int64_t)(*((unsigned char *)parametervalue)));
			break;
		case SQL_C_GUID:
			{
			debugPrintf("  valuetype: SQL_C_GUID\n");
			stmt->cur->inputBind(parametername,
				SQLR_BuildGuid(stmt,parameternumber,
						(SQLGUID *)parametervalue));
			}
			break;
		default:
			debugPrintf("  invalid valuetype\n");
			retval=SQL_ERROR;
			break;
	}

	delete[] parametername;

	return retval;
}

static SQLRETURN SQLR_OutputBindParameter(SQLHSTMT statementhandle,
					SQLUSMALLINT parameternumber,
					SQLSMALLINT valuetype,
					SQLULEN lengthprecision,
					SQLSMALLINT parameterscale,
					SQLPOINTER parametervalue,
					SQLLEN bufferlength,
					SQLLEN *strlen_or_ind) {
	debugFunction();

	STMT	*stmt=(STMT *)statementhandle;
	if (statementhandle==SQL_NULL_HSTMT || !stmt || !stmt->cur) {
		debugPrintf("  NULL stmt handle\n");
		return SQL_INVALID_HANDLE;
	}

	SQLRETURN	retval=SQL_SUCCESS;

	// convert parameternumber to a string
	char	*parametername=charstring::parseNumber(parameternumber);

	// store the output bind for later
	outputbind	*ob=new outputbind;
	ob->parameternumber=parameternumber;
	ob->valuetype=valuetype;
	ob->lengthprecision=lengthprecision;
	ob->parameterscale=parameterscale;
	ob->parametervalue=parametervalue;
	ob->bufferlength=bufferlength;
	ob->strlen_or_ind=strlen_or_ind;
	stmt->outputbinds.setValue(parameternumber,ob);

	switch (valuetype) {
		case SQL_C_CHAR:
		case SQL_C_BIT:
			debugPrintf("  valuetype: SQL_C_CHAR/SQL_C_BIT\n");
			stmt->cur->defineOutputBindString(parametername,
								bufferlength);
			break;
		case SQL_C_LONG:
		case SQL_C_SBIGINT:
		case SQL_C_UBIGINT:
		case SQL_C_SHORT:
		case SQL_C_TINYINT:
		case SQL_C_SLONG:
		case SQL_C_SSHORT:
		case SQL_C_STINYINT:
		//case SQL_C_BOOKMARK:
		//	(dup of SQL_C_ULONG)
		case SQL_C_ULONG:
		case SQL_C_USHORT:
		case SQL_C_UTINYINT:
			debugPrintf("  valuetype: SQL_C_(INT of some kind)\n");
			stmt->cur->defineOutputBindInteger(parametername);
			break;
		case SQL_C_FLOAT:
		case SQL_C_DOUBLE:
			debugPrintf("  valuetype: SQL_C_FLOAT/SQL_C_DOUBLE\n");
			stmt->cur->defineOutputBindDouble(parametername);
			break;
		case SQL_C_NUMERIC:
			debugPrintf("  valuetype: SQL_C_NUMERIC\n");
			// bind as a string, the result will be parsed
			stmt->cur->defineOutputBindString(parametername,128);
			break;
		case SQL_C_DATE:
		case SQL_C_TYPE_DATE:
			debugPrintf("  valuetype: SQL_C_DATE/SQL_C_TYPE_DATE\n");
			stmt->cur->defineOutputBindDate(parametername);
			break;
		case SQL_C_TIME:
		case SQL_C_TYPE_TIME:
			debugPrintf("  valuetype: SQL_C_TIME/SQL_C_TYPE_TIME\n");
			stmt->cur->defineOutputBindDate(parametername);
			break;
		case SQL_C_TIMESTAMP:
		case SQL_C_TYPE_TIMESTAMP:
			debugPrintf("  valuetype: "
				"SQL_C_TIMESTAMP/SQL_C_TYPE_TIMESTAMP\n");
			stmt->cur->defineOutputBindDate(parametername);
			break;
		case SQL_C_INTERVAL_YEAR:
		case SQL_C_INTERVAL_MONTH:
		case SQL_C_INTERVAL_DAY:
		case SQL_C_INTERVAL_HOUR:
		case SQL_C_INTERVAL_MINUTE:
		case SQL_C_INTERVAL_SECOND:
		case SQL_C_INTERVAL_YEAR_TO_MONTH:
		case SQL_C_INTERVAL_DAY_TO_HOUR:
		case SQL_C_INTERVAL_DAY_TO_MINUTE:
		case SQL_C_INTERVAL_DAY_TO_SECOND:
		case SQL_C_INTERVAL_HOUR_TO_MINUTE:
		case SQL_C_INTERVAL_HOUR_TO_SECOND:
		case SQL_C_INTERVAL_MINUTE_TO_SECOND:
			debugPrintf("  valuetype: SQL_C_INTERVAL_XXX\n");
			// bind as a string, the result will be parsed
			stmt->cur->defineOutputBindString(parametername,128);
			break;
		//case SQL_C_VARBOOKMARK:
		//	(dup of SQL_C_BINARY)
		case SQL_C_BINARY:
			debugPrintf("  valuetype: "
				"SQL_C_BINARY/SQL_C_VARBOOKMARK\n");
			stmt->cur->defineOutputBindBlob(parametername);
			break;
		case SQL_C_GUID:
			debugPrintf("  valuetype: SQL_C_GUID\n");
			// bind as a string, the result will be parsed
			stmt->cur->defineOutputBindString(parametername,128);
			break;
		default:
			debugPrintf("  invalid valuetype\n");
			retval=SQL_ERROR;
			break;
	}

	delete[] parametername;

	return retval;
}

static SQLRETURN SQLR_SQLBindParameter(SQLHSTMT statementhandle,
					SQLUSMALLINT parameternumber,
					SQLSMALLINT inputoutputtype,
					SQLSMALLINT valuetype,
					SQLSMALLINT parametertype,
					SQLULEN lengthprecision,
					SQLSMALLINT parameterscale,
					SQLPOINTER parametervalue,
					SQLLEN bufferlength,
					SQLLEN *strlen_or_ind) {
	debugFunction();

	STMT	*stmt=(STMT *)statementhandle;
	if (statementhandle==SQL_NULL_HSTMT || !stmt || !stmt->cur) {
		debugPrintf("  NULL stmt handle\n");
		return SQL_INVALID_HANDLE;
	}

	switch (inputoutputtype) {
		case SQL_PARAM_INPUT:
			debugPrintf("  parametertype: SQL_PARAM_INPUT\n");
			return SQLR_InputBindParameter(statementhandle,
							parameternumber,
							valuetype,
							lengthprecision,
							parameterscale,
							parametervalue,
							strlen_or_ind);
		case SQL_PARAM_INPUT_OUTPUT:
			debugPrintf("  parametertype: SQL_PARAM_INPUT_OUTPUT\n");
			// SQL Relay doesn't currently support in/out params
			return SQL_ERROR;
		case SQL_PARAM_OUTPUT:
			debugPrintf("  parametertype: SQL_PARAM_OUTPUT\n");
			return SQLR_OutputBindParameter(statementhandle,
							parameternumber,
							valuetype,
							lengthprecision,
							parameterscale,
							parametervalue,
							bufferlength,
							strlen_or_ind);
		default:
			debugPrintf("  invalid parametertype\n");
			return SQL_ERROR;
	}
}

SQLRETURN SQL_API SQLBindParameter(SQLHSTMT statementhandle,
					SQLUSMALLINT parameternumber,
					SQLSMALLINT inputoutputtype,
					SQLSMALLINT valuetype,
					SQLSMALLINT parametertype,
					SQLULEN lengthprecision,
					SQLSMALLINT parameterscale,
					SQLPOINTER parametervalue,
					SQLLEN bufferlength,
					SQLLEN *strlen_or_ind) {
	debugFunction();
	return SQLR_SQLBindParameter(statementhandle,
					parameternumber,
					inputoutputtype,
					valuetype,
					parametertype,
					lengthprecision,
					parameterscale,
					parametervalue,
					bufferlength,
					strlen_or_ind);
}

#ifdef _WIN32

#define SQLR_BOX	101
#define SQLR_LABEL	102
#define SQLR_EDIT	103
#define SQLR_OK		104
#define SQLR_CANCEL	105

static HINSTANCE	hinst;
static HWND		mainwindow;
static HWND		dsnedit;
static HWND		serveredit;
static HWND		portedit;
static HWND		socketedit;
static HWND		useredit;
static HWND		passwordedit;
static HWND		retrytimeedit;
static HWND		triesedit;
static HWND		debugedit;

static const char	sqlrwindowclass[]="SQLRWindowClass";
static const int	labelwidth=55;
static const int	labelheight=18;
static const int	labeloffset=2;
static const int	labelcount=9;
static const int	xoffset=8;
static const int	yoffset=8;
static const int	mainwindowwidth=300;
static const int	labelboxwidth=mainwindowwidth-yoffset*2;
static const int	editwidth=labelboxwidth-xoffset-labelwidth-xoffset*2;
static const int	labelboxheight=yoffset+
					labelcount*labelheight+
					labelcount*labeloffset+yoffset;
static const int	buttonheight=24;
static const int	buttonwidth=74;
static const int	mainwindowheight=yoffset+
					labelboxheight+yoffset+
					buttonheight+yoffset;

static WORD				dsnrequest;
static dictionary< char *, char * >	dsndict;

BOOL DllMain(HANDLE hinstdll, DWORD fdwreason, LPVOID lpvreserved) {
	if (fdwreason==DLL_PROCESS_ATTACH) {
		hinst=(HINSTANCE)hinstdll;
	}
	return TRUE;
}

static void createLabel(HWND parent, const char *label,
			int x, int y, int width, int height) {
	HWND	labelwin=CreateWindow("STATIC",label,
					WS_CHILD|WS_VISIBLE|SS_RIGHT,
					x,y,width,height,
					parent,(HMENU)SQLR_LABEL,hinst,NULL);
	SendMessage(labelwin,
			WM_SETFONT,
			(WPARAM)GetStockObject(DEFAULT_GUI_FONT),
			MAKELPARAM(FALSE,0));
}

static HWND createEdit(HWND parent, const char *defaultvalue,
			int x, int y, int width, int height,
			int charlimit, bool numeric, bool first) {
	DWORD	style=WS_CHILD|WS_VISIBLE|WS_BORDER|WS_TABSTOP|ES_LEFT;
	if (numeric) {
		style|=ES_NUMBER;
	}
	if (first) {
		style|=WS_GROUP;
	}
	HWND	editwin=CreateWindow("EDIT",(defaultvalue)?defaultvalue:"",
					style,x,y,width,height,
					parent,(HMENU)SQLR_EDIT,hinst,NULL);
	SendMessage(editwin,
			WM_SETFONT,
			(WPARAM)GetStockObject(DEFAULT_GUI_FONT),
			MAKELPARAM(FALSE,0));
	SendMessage(editwin,
			EM_SETLIMITTEXT,
			MAKEWPARAM(charlimit,0),
			MAKELPARAM(FALSE,0));
	return editwin;
}

static void createButton(HWND parent, const char *label,
					int x, int y, HMENU id,
					bool first) {
	DWORD	style=WS_CHILD|WS_VISIBLE|WS_TABSTOP;
	if (first) {
		style|=WS_GROUP;
	}
	HWND	buttonwin=CreateWindow("BUTTON",label,style,
					x,y,buttonwidth,buttonheight,
					parent,id,hinst,NULL);
	SendMessage(buttonwin,
			WM_SETFONT,
			(WPARAM)GetStockObject(DEFAULT_GUI_FONT),
			MAKELPARAM(FALSE,0));
}

static void createControls(HWND hwnd) {

	// create a box to surround the labels and edits
	HWND	box=CreateWindowEx(WS_EX_CONTROLPARENT,
				"STATIC","",
				WS_CHILD|WS_VISIBLE|SS_GRAYFRAME,
				xoffset,yoffset,
				labelboxwidth,
				labelboxheight,
				hwnd,(HMENU)SQLR_BOX,hinst,NULL);

	// create labels...
	int	x=xoffset;
	int	y=yoffset;
	createLabel(box,"DSN Name",x,y,labelwidth,labelheight);
	createLabel(box,"Server",x,y+=(labelheight+labeloffset),
					labelwidth,labelheight);
	createLabel(box,"Port",x,y+=(labelheight+labeloffset),
					labelwidth,labelheight);
	createLabel(box,"Socket",x,y+=(labelheight+labeloffset),
					labelwidth,labelheight);
	createLabel(box,"User",x,y+=(labelheight+labeloffset),
					labelwidth,labelheight);
	createLabel(box,"Password",x,y+=(labelheight+labeloffset),
					labelwidth,labelheight);
	createLabel(box,"Retry Time",x,y+=(labelheight+labeloffset),
					labelwidth,labelheight);
	createLabel(box,"Tries",x,y+=(labelheight+labeloffset),
					labelwidth,labelheight);
	createLabel(box,"Debug",x,y+=(labelheight+labeloffset),
					labelwidth,labelheight);

	// create edits...
	x=xoffset+labelwidth+xoffset;
	y=yoffset;
	dsnedit=createEdit(box,dsndict.getValue("DSN"),
			x,y,editwidth,labelheight,
			1024,false,true);
	serveredit=createEdit(box,dsndict.getValue("Server"),
			x,y+=(labelheight+labeloffset),editwidth,labelheight,
			1024,false,false);
	portedit=createEdit(box,dsndict.getValue("Port"),
			x,y+=(labelheight+labeloffset),editwidth,labelheight,
			6,true,false);
	socketedit=createEdit(box,dsndict.getValue("Socket"),
			x,y+=(labelheight+labeloffset),editwidth,labelheight,
			1024,false,false);
	useredit=createEdit(box,dsndict.getValue("User"),
			x,y+=(labelheight+labeloffset),editwidth,labelheight,
			1024,false,false);
	passwordedit=createEdit(box,dsndict.getValue("Password"),
			x,y+=(labelheight+labeloffset),editwidth,labelheight,
			1024,false,false);
	retrytimeedit=createEdit(box,dsndict.getValue("RetryTime"),
			x,y+=(labelheight+labeloffset),editwidth,labelheight,
			11,true,false);
	triesedit=createEdit(box,dsndict.getValue("Tries"),
			x,y+=(labelheight+labeloffset),editwidth,labelheight,
			11,true,false);
	debugedit=createEdit(box,dsndict.getValue("Debug"),
			x,y+=(labelheight+labeloffset),editwidth,labelheight,
			1,true,false);

	// create buttons...
	x=mainwindowwidth-xoffset-buttonwidth-xoffset-buttonwidth;
	y=yoffset+labelboxheight+yoffset;
	createButton(hwnd,"OK",x,y,(HMENU)SQLR_OK,true);
	createButton(hwnd,"Cancel",x+=buttonwidth+xoffset,y,
					(HMENU)SQLR_CANCEL,false);

	// set focus
	SetFocus(dsnedit);
}

static void parseDsn(const char *dsn) {

	// dsn is formatted like:
	// DSN=xxx\0Server=xxx\0Port=xxx\0\0
	for (const char *c=dsn; *c; c=c+charstring::length(c)+1) {
		char		**parts;
		uint64_t	partcount;
		charstring::split(c,"=",true,&parts,&partcount);
		dsndict.setValue(parts[0],parts[1]);
	}

	// But, actually, it usually just contains the DSN name itself and
	// the rest of the bits of data have to be fetched...

	// get the name of the dsn that we were given, bail if it's empty
	const char	*dsnval=dsndict.getValue("DSN");
	if (!charstring::length(dsn)) {

		// provide some defaults...
		dsndict.setValue("Port",charstring::duplicate("9000"));
		dsndict.setValue("RetryTime",charstring::duplicate("0"));
		dsndict.setValue("Tries",charstring::duplicate("1"));
		dsndict.setValue("Debug",charstring::duplicate("0"));

		return;
	}

	// get the rest of the data...
	if (!dsndict.getValue("Server")) {
		char	*server=new char[1024];
		SQLGetPrivateProfileString(dsnval,"Server","",
						server,1024,ODBC_INI);
		dsndict.setValue("Server",server);
	}
	if (!dsndict.getValue("Port")) {
		char	*port=new char[6];
		SQLGetPrivateProfileString(dsnval,"Port","9000",
						port,6,ODBC_INI);
		dsndict.setValue("Port",port);
	}
	if (!dsndict.getValue("Socket")) {
		char	*socket=new char[1024];
		SQLGetPrivateProfileString(dsnval,"Socket","",
						socket,1024,ODBC_INI);
		dsndict.setValue("Socket",socket);
	}
	if (!dsndict.getValue("User")) {
		char	*user=new char[1024];
		SQLGetPrivateProfileString(dsnval,"User","",
						user,1024,ODBC_INI);
		dsndict.setValue("User",user);
	}
	if (!dsndict.getValue("Password")) {
		char	*password=new char[1024];
		SQLGetPrivateProfileString(dsnval,"Password","",
						password,1024,ODBC_INI);
		dsndict.setValue("Password",password);
	}
	if (!dsndict.getValue("RetryTime")) {
		char	*retrytime=new char[11];
		SQLGetPrivateProfileString(dsnval,"RetryTime","0",
						retrytime,11,ODBC_INI);
		dsndict.setValue("RetryTime",retrytime);
	}
	if (!dsndict.getValue("Tries")) {
		char	*tries=new char[11];
		SQLGetPrivateProfileString(dsnval,"Tries","1",
						tries,11,ODBC_INI);
		dsndict.setValue("Tries",tries);
	}
	if (!dsndict.getValue("Debug")) {
		char	*debug=new char[2];
		SQLGetPrivateProfileString(dsnval,"Debug","0",
						debug,2,ODBC_INI);
		dsndict.setValue("Debug",debug);
	}
	dsndict.print();
}

static void dsnError() {

	DWORD	pferrorcode;
	char	errormsg[SQL_MAX_MESSAGE_LENGTH+1];
	
	for (WORD ierror=1; ierror<=16; ierror++) {
		if (SQLInstallerError(ierror,&pferrorcode,
					errormsg,sizeof(errormsg),
					NULL)==SQL_NO_DATA) {
			return;
		}

		MessageBox(NULL,errormsg,"Error",MB_OK|MB_ICONERROR);
	}
}

static bool validDsn() {

	// FIXME: SQLValidDSN always seems to return false
	return true;

	if (SQLValidDSN(dsndict.getValue("DSN"))==FALSE) {
		dsnError();
		return false;
	}
	return true;
}

static bool removeDsn() {
	if (SQLRemoveDSNFromIni(dsndict.getValue("DSN"))==FALSE) {
		dsnError();
		return false;
	}
	return true;
}

static void getDsnFromUi() {

	// populate dsndict from values in edit windows...

	// DSN...
	int	len=GetWindowTextLength(dsnedit);
	char	*data=new char[len+1];
	GetWindowText(dsnedit,data,len+1);
	delete[] dsndict.getValue("DSN");
	dsndict.setValue("DSN",data);

	// Server...
	len=GetWindowTextLength(serveredit);
	data=new char[len+1];
	GetWindowText(serveredit,data,len+1);
	delete[] dsndict.getValue("Server");
	dsndict.setValue("Server",data);

	// Port...
	len=GetWindowTextLength(portedit);
	data=new char[len+1];
	GetWindowText(portedit,data,len+1);
	delete[] dsndict.getValue("Port");
	dsndict.setValue("Port",data);

	// Socket...
	len=GetWindowTextLength(socketedit);
	data=new char[len+1];
	GetWindowText(socketedit,data,len+1);
	delete[] dsndict.getValue("Socket");
	dsndict.setValue("Socket",data);

	// User...
	len=GetWindowTextLength(useredit);
	data=new char[len+1];
	GetWindowText(useredit,data,len+1);
	delete[] dsndict.getValue("User");
	dsndict.setValue("User",data);

	// Password...
	len=GetWindowTextLength(passwordedit);
	data=new char[len+1];
	GetWindowText(passwordedit,data,len+1);
	delete[] dsndict.getValue("Password");
	dsndict.setValue("Password",data);

	// Retry Time...
	len=GetWindowTextLength(retrytimeedit);
	data=new char[len+1];
	GetWindowText(retrytimeedit,data,len+1);
	delete[] dsndict.getValue("RetryTime");
	dsndict.setValue("RetryTime",data);

	// Tries...
	len=GetWindowTextLength(triesedit);
	data=new char[len+1];
	GetWindowText(triesedit,data,len+1);
	delete[] dsndict.getValue("Tries");
	dsndict.setValue("Tries",data);

	// Debug...
	len=GetWindowTextLength(debugedit);
	data=new char[len+1];
	GetWindowText(debugedit,data,len+1);
	delete[] dsndict.getValue("Debug");
	dsndict.setValue("Debug",data);
}

static bool writeDsn() {
	const char	*dsnname=dsndict.getValue("DSN");
	if (SQLWriteDSNToIni(dsnname,"SQL Relay")==FALSE) {
		dsnError();
		return false;
	}
	for (linkedlistnode< char * > *key=dsndict.getKeys()->getFirst();
						key; key=key->getNext()) {
		if (!charstring::compare(key->getValue(),"DSN")) {
			continue;
		}
		if (SQLWritePrivateProfileString(
					dsnname,
					key->getValue(),
					dsndict.getValue(key->getValue()),
					ODBC_INI)==FALSE) {
			return false;
		}
	}
	return true;
}


static bool saveDsn() {

	// validate dsn
	if (!validDsn()) {
		return false;
	}

	// add/config...
	bool	success=false;
	switch (dsnrequest) {
		case ODBC_ADD_DSN:
			getDsnFromUi();
			success=writeDsn();
			break;
		case ODBC_CONFIG_DSN:
			if (removeDsn()) {
				getDsnFromUi();
				success=writeDsn();
			}
			break;
	}

	return success;
}

static LRESULT CALLBACK windowProc(HWND hwnd, UINT umsg,
				WPARAM wparam, LPARAM lparam) {
	switch (umsg) {
		case WM_CREATE:
			createControls(hwnd);
			break;
		case WM_CLOSE:
			DestroyWindow(hwnd);
			break;
		case WM_DESTROY:
			PostQuitMessage(0);
			break;
		case WM_COMMAND:
			switch (GetDlgCtrlID((HWND)lparam)) {
				case SQLR_OK:
					if (saveDsn()) {
						DestroyWindow(mainwindow);
					}
					break;
				case SQLR_CANCEL:
					DestroyWindow(mainwindow);
					break;
			}
		default:
			return DefWindowProc(hwnd,umsg,wparam,lparam);
	}
	return 0;
}

BOOL INSTAPI ConfigDSN(HWND hwndparent, WORD frequest,
			LPCSTR lpszdriver, LPCSTR lpszattributes) {
	debugFunction();

	// sanity check
	if (!hwndparent) {
		// FIXME: actually, if this is null, just use the
		// data provided in lpszattributes non-interactively
		return FALSE;
	}

	// parse the dsn
	parseDsn(lpszattributes);

	// handle remove directly...
	if (frequest==ODBC_REMOVE_DSN) {
		bool	success=(validDsn() && removeDsn());
		return (success)?TRUE:FALSE;
	}

	// save request type
	dsnrequest=frequest;

	// display a dialog box displaying values supplied in lpszattributes
	// and prompting the user for data not supplied

	// create a window class...
	WNDCLASS	wcx;
	wcx.style=0;
	wcx.lpfnWndProc=windowProc;
	wcx.cbClsExtra=0;
	wcx.cbWndExtra=0;
	wcx.hInstance=hinst;
	wcx.hIcon=LoadIcon(NULL,IDI_APPLICATION);
	wcx.hCursor=LoadCursor(NULL,IDC_ARROW);
	wcx.hbrBackground=(HBRUSH)COLOR_WINDOW;
	wcx.lpszMenuName=NULL;
	wcx.lpszClassName=sqlrwindowclass;
	if (RegisterClass(&wcx)==FALSE) {
		return FALSE;
	}

	// figure out how big the outside of the window needs to be,
	// based on the desired size of the inside...
	RECT	rect;
	rect.left=0;
	rect.top=0;
	rect.right=mainwindowwidth;
	rect.bottom=mainwindowheight;
	AdjustWindowRect(&rect,
			WS_CAPTION|WS_SYSMENU|WS_THICKFRAME,
			false);

	// create the dialog window...
	mainwindow=CreateWindowEx(WS_EX_CONTROLPARENT,
				sqlrwindowclass,
				(frequest==ODBC_ADD_DSN)?
					"Create a New Data Source to SQL Relay":
					"SQL Relay Data Source Configuration",
				WS_OVERLAPPED|WS_CAPTION|
				WS_SYSMENU|WS_THICKFRAME,
				CW_USEDEFAULT,
				CW_USEDEFAULT,
				rect.right-rect.left,
				rect.bottom-rect.top,
				NULL,
				NULL,
				hinst,
				NULL);
	if (!mainwindow) {
		return FALSE;
	}

	// show the window and take input...
	ShowWindow(mainwindow,SW_SHOWDEFAULT);
	UpdateWindow(mainwindow);
	MSG	msg;
	while (GetMessage(&msg,NULL,0,0)>0) {
		if (IsDialogMessage(mainwindow,&msg)==FALSE) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	// clean up
	UnregisterClass(sqlrwindowclass,hinst);

	// FIXME: clean up dsndict

	return TRUE;
}

#endif

}
