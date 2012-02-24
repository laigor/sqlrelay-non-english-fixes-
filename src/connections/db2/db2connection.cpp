// Copyright (c) 1999-2001  David Muse
// See the file COPYING for more information

#include <db2connection.h>

#include <rudiments/environment.h>

#include <datatypes.h>

#include <config.h>

#include <stdlib.h>

uint16_t db2connection::getNumberOfConnectStringVars() {
	return NUM_CONNECT_STRING_VARS;
}

void db2connection::handleConnectString() {

	// override legacy "server" parameter with modern "db" parameter
	server=connectStringValue("server");
	const char	*tmp=connectStringValue("db");
	if (tmp && tmp[0]) {
		server=tmp;
	}

	setUser(connectStringValue("user"));
	setPassword(connectStringValue("password"));
	const char	*autocom=connectStringValue("autocommit");
	setAutoCommitBehavior((autocom &&
		!charstring::compareIgnoringCase(autocom,"yes")));
	lang=connectStringValue("lang");
	setFakeTransactionBlocksBehavior(
		!charstring::compare(
			connectStringValue("faketransactionblocks"),"yes"));
	fakeinputbinds=
		!charstring::compare(connectStringValue("fakebinds"),"yes");
}

bool db2connection::logIn(bool printerrors) {

	// set the LANG environment variable
	if (charstring::length(lang)) {
		if (!environment::setValue("LANG",lang)) {
			if (printerrors) {
				fprintf(stderr,"Failed to set LANG environment variable.\n");
			}
			return false;
		}
	}

	// allocate environment handle
	erg=SQLAllocHandle(SQL_HANDLE_ENV,SQL_NULL_HANDLE,&env);
	if (erg!=SQL_SUCCESS && erg!=SQL_SUCCESS_WITH_INFO) {
		SQLFreeHandle(SQL_HANDLE_ENV,env);
		return false;
	}

	// allocate connection handle
	erg=SQLAllocHandle(SQL_HANDLE_DBC,env,&dbc);
	if (erg!=SQL_SUCCESS && erg!=SQL_SUCCESS_WITH_INFO) {
		SQLFreeHandle(SQL_HANDLE_DBC,dbc);
		SQLFreeHandle(SQL_HANDLE_ENV,env);
		return false;
	}

	// set the connect timeout
	SQLSetConnectAttr(dbc,SQL_LOGIN_TIMEOUT,(SQLPOINTER *)5,0);

	// connect to the database
	erg=SQLConnect(dbc,(SQLCHAR *)server,SQL_NTS,
				(SQLCHAR *)getUser(),SQL_NTS,
				(SQLCHAR *)getPassword(),SQL_NTS);
	if (erg!=SQL_SUCCESS && erg!=SQL_SUCCESS_WITH_INFO) {
		SQLFreeHandle(SQL_HANDLE_DBC,dbc);
		SQLFreeHandle(SQL_HANDLE_ENV,env);
		return false;
	}
	return true;
}

sqlrcursor_svr *db2connection::initCursor() {
	return (sqlrcursor_svr *)new db2cursor((sqlrconnection_svr *)this);
}

void db2connection::deleteCursor(sqlrcursor_svr *curs) {
	delete (db2cursor *)curs;
}

void db2connection::logOut() {
	SQLDisconnect(dbc);
	SQLFreeHandle(SQL_HANDLE_DBC,dbc);
	SQLFreeHandle(SQL_HANDLE_ENV,env);
}

int16_t db2connection::nullBindValue() {
	return SQL_NULL_DATA;
}

bool db2connection::bindValueIsNull(int16_t isnull) {
	if (isnull==SQL_NULL_DATA) {
		return true;
	}
	return false;
}

bool db2connection::autoCommitOn() {
	return (SQLSetConnectAttr(dbc,SQL_ATTR_AUTOCOMMIT,
				(SQLPOINTER)SQL_AUTOCOMMIT_ON,
				sizeof(SQLINTEGER))==SQL_SUCCESS);
}

bool db2connection::autoCommitOff() {
	return (SQLSetConnectAttr(dbc,SQL_ATTR_AUTOCOMMIT,
				(SQLPOINTER)SQL_AUTOCOMMIT_OFF,
				sizeof(SQLINTEGER))==SQL_SUCCESS);
}

bool db2connection::supportsTransactionBlocks() {
	return false;
}

bool db2connection::commit() {
	return (SQLEndTran(SQL_HANDLE_ENV,env,SQL_COMMIT)==SQL_SUCCESS);
}

bool db2connection::rollback() {
	return (SQLEndTran(SQL_HANDLE_ENV,env,SQL_ROLLBACK)==SQL_SUCCESS);
}

const char *db2connection::pingQuery() {
	return "values 1";
}

const char *db2connection::identify() {
	return "db2";
}

const char *db2connection::dbVersion() {
	SQLSMALLINT	dbversionlen;
	SQLGetInfo(dbc,SQL_DBMS_VER,
			(SQLPOINTER)dbversion,
			(SQLSMALLINT)sizeof(dbversion),
			&dbversionlen);
	return dbversion;
}

const char *db2connection::getDatabaseListQuery(bool wild) {
	return (wild)?
		"select "
		"	schemaname "
		"from "
		"	syscat.schemata "
		"where "
		"	schemaname like '%s'":

		"select "
		"	schemaname "
		"from "
		"	syscat.schemata ";
}

const char *db2connection::getTableListQuery(bool wild) {
	return (wild)?
		"select "
		"	tabname "
		"from "
		"	syscat.tables "
		"where "
		"	type in ('T','U','V','W') "
		"	and "
		"	tabname like '%s' "
		"order by "
		"	tabname":

		"select "
		"	tabname "
		"from "
		"	syscat.tables "
		"where "
		"	type in ('T','U','V','W') "
		"order by "
		"	tabname";
}

const char *db2connection::getColumnListQuery(bool wild) {
	return (wild)?
		"select "
		"	colname, "
		"	typename, "
		"	length, "
		"	length as precision, "
		"	scale, "
		"	nulls, "
		"	keyseq as key, "
		"	default, "
		"	'' as extra "
		"from "
		"	syscat.columns "
		"where "
		"	upper(tabame)=upper('%s') "
		"	and "
		"	colname like '%s' "
		"order by "
		"	colno":

		"select "
		"	colname, "
		"	typename, "
		"	length, "
		"	length as precision, "
		"	scale, "
		"	nulls, "
		"	keyseq as key, "
		"	default, "
		"	'' as extra "
		"from "
		"	syscat.columns "
		"where "
		"	upper(tabname)=upper('%s') "
		"order by "
		"	colno";
}

const char *db2connection::bindFormat() {
	return "?";
}

const char *db2connection::selectDatabaseQuery() {
	return "set schema %s";
}

const char *db2connection::getCurrentDatabaseQuery() {
	return "values current schema";
}

const char *db2connection::getLastInsertIdQuery() {
	return "values identity_val_local()";
}

const char *db2connection::setIsolationLevelQuery() {
        return "set current isolation %s";
}

db2cursor::db2cursor(sqlrconnection_svr *conn) : sqlrcursor_svr(conn) {
	db2conn=(db2connection *)conn;
	errormsg=NULL;
	stmt=0;
}

db2cursor::~db2cursor() {
	if (errormsg) {
		delete errormsg;
	}
}

bool db2cursor::prepareQuery(const char *query, uint32_t length) {

	if (stmt) {
		SQLFreeHandle(SQL_HANDLE_STMT,stmt);
	}

	// allocate the cursor
	erg=SQLAllocHandle(SQL_HANDLE_STMT,db2conn->dbc,&stmt);
	if (erg!=SQL_SUCCESS && erg!=SQL_SUCCESS_WITH_INFO) {
		return false;
	}

	// set the row array size
	erg=SQLSetStmtAttr(stmt,SQL_ATTR_ROW_ARRAY_SIZE,
				(SQLPOINTER)FETCH_AT_ONCE,0);
	if (erg!=SQL_SUCCESS && erg!=SQL_SUCCESS_WITH_INFO) {
		return false;
	}

#if (DB2VERSION>7)
	// set the row status ptr
	erg=SQLSetStmtAttr(stmt,SQL_ATTR_ROW_STATUS_PTR,
				(SQLPOINTER)rowstat,0);
	if (erg!=SQL_SUCCESS && erg!=SQL_SUCCESS_WITH_INFO) {
		return false;
	}
#endif

	// prepare the query
	erg=SQLPrepare(stmt,(SQLCHAR *)query,length);
	if (erg!=SQL_SUCCESS && erg!=SQL_SUCCESS_WITH_INFO) {
		return false;
	}
	return true;
}

bool db2cursor::inputBindString(const char *variable,
					uint16_t variablesize,
					const char *value,
					uint32_t valuesize,
					int16_t *isnull) {

	if (*isnull==SQL_NULL_DATA) {
		erg=SQLBindParameter(stmt,
				charstring::toInteger(variable+1),
				SQL_PARAM_INPUT,
				SQL_C_CHAR,
				SQL_CHAR,
				0,
				0,
				(SQLPOINTER)value,
				valuesize,
				(SQLINTEGER *)isnull);
	} else {
		erg=SQLBindParameter(stmt,
				charstring::toInteger(variable+1),
				SQL_PARAM_INPUT,
				SQL_C_CHAR,
				SQL_CHAR,
				0,
				0,
				(SQLPOINTER)value,
				valuesize,
				(SQLINTEGER *)NULL);
	}
	if (erg!=SQL_SUCCESS && erg!=SQL_SUCCESS_WITH_INFO) {
		return false;
	}
	return true;
}

bool db2cursor::inputBindInteger(const char *variable,
					uint16_t variablesize,
					int64_t *value) {

	erg=SQLBindParameter(stmt,
				charstring::toInteger(variable+1),
				SQL_PARAM_INPUT,
				SQL_C_LONG,
				SQL_INTEGER,
				0,
				0,
				value,
				sizeof(int64_t),
				(SQLINTEGER *)NULL);
	if (erg!=SQL_SUCCESS && erg!=SQL_SUCCESS_WITH_INFO) {
		return false;
	}
	return true;
}

bool db2cursor::inputBindDouble(const char *variable,
					uint16_t variablesize,
					double *value,
					uint32_t precision,
					uint32_t scale) {

	erg=SQLBindParameter(stmt,
				charstring::toInteger(variable+1),
				SQL_PARAM_INPUT,
				SQL_C_DOUBLE,
				SQL_DOUBLE,
				precision,
				scale,
				value,
				sizeof(double),
				(SQLINTEGER *)NULL);
	if (erg!=SQL_SUCCESS && erg!=SQL_SUCCESS_WITH_INFO) {
		return false;
	}
	return true;
}

bool db2cursor::outputBindString(const char *variable, 
					uint16_t variablesize,
					char *value, 
					uint16_t valuesize, 
					int16_t *isnull) {

	erg=SQLBindParameter(stmt,
				charstring::toInteger(variable+1),
				SQL_PARAM_OUTPUT,
				SQL_C_CHAR,
				SQL_CHAR,
				0,
				0,
				value,
				valuesize,
				(SQLINTEGER *)isnull);
	if (erg!=SQL_SUCCESS && erg!=SQL_SUCCESS_WITH_INFO) {
		return false;
	}
	return true;
}

bool db2cursor::outputBindInteger(const char *variable,
						uint16_t variablesize,
						int64_t *value,
						int16_t *isnull) {

	erg=SQLBindParameter(stmt,
				charstring::toInteger(variable+1),
				SQL_PARAM_OUTPUT,
				SQL_C_LONG,
				SQL_INTEGER,
				0,
				0,
				value,
				sizeof(int64_t),
				(SQLINTEGER *)isnull);
	if (erg!=SQL_SUCCESS && erg!=SQL_SUCCESS_WITH_INFO) {
		return false;
	}
	return true;
}

bool db2cursor::outputBindDouble(const char *variable,
						uint16_t variablesize,
						double *value,
						uint32_t *precision,
						uint32_t *scale,
						int16_t *isnull) {

	erg=SQLBindParameter(stmt,
				charstring::toInteger(variable+1),
				SQL_PARAM_OUTPUT,
				SQL_C_DOUBLE,
				SQL_DOUBLE,
				0,
				0,
				value,
				sizeof(double),
				(SQLINTEGER *)isnull);
	if (erg!=SQL_SUCCESS && erg!=SQL_SUCCESS_WITH_INFO) {
		return false;
	}
	return true;
}

bool db2cursor::executeQuery(const char *query, uint32_t length, bool execute) {

	// initialize counts
	ncols=0;
	rowgroupindex=0;
	totalinrowgroup=0;
	totalrows=0;

	// execute the query
	erg=SQLExecute(stmt);
	if (erg!=SQL_SUCCESS &&
		erg!=SQL_SUCCESS_WITH_INFO &&
		erg!=SQL_NO_DATA) {
		return false;
	}

	checkForTempTable(query,length);

	// get the column count
	erg=SQLNumResultCols(stmt,&ncols);
	if (erg!=SQL_SUCCESS && erg!=SQL_SUCCESS_WITH_INFO) {
		return false;
	}
	if (ncols>MAX_SELECT_LIST_SIZE) {
		ncols=MAX_SELECT_LIST_SIZE;
	}

	// run through the columns
	for (SQLSMALLINT i=0; i<ncols; i++) {

		if (conn->sendColumnInfo()) {

			// column name
			erg=SQLColAttribute(stmt,i+1,SQL_COLUMN_LABEL,
					col[i].name,MAX_ITEM_BUFFER_SIZE,
					(SQLSMALLINT *)&(col[i].namelength),
					NULL);
			if (erg!=SQL_SUCCESS && erg!=SQL_SUCCESS_WITH_INFO) {
				return false;
			}

			// column length
			erg=SQLColAttribute(stmt,i+1,SQL_COLUMN_LENGTH,
					NULL,0,NULL,&(col[i].length));
			if (erg!=SQL_SUCCESS && erg!=SQL_SUCCESS_WITH_INFO) {
				return false;
			}

			// column type
			erg=SQLColAttribute(stmt,i+1,SQL_COLUMN_TYPE,
					NULL,0,NULL,&(col[i].type));
			if (erg!=SQL_SUCCESS && erg!=SQL_SUCCESS_WITH_INFO) {
				return false;
			}

			// column precision
			erg=SQLColAttribute(stmt,i+1,SQL_COLUMN_PRECISION,
					NULL,0,NULL,&(col[i].precision));
			if (erg!=SQL_SUCCESS && erg!=SQL_SUCCESS_WITH_INFO) {
				return false;
			}

			// column scale
			erg=SQLColAttribute(stmt,i+1,SQL_COLUMN_SCALE,
					NULL,0,NULL,&(col[i].scale));
			if (erg!=SQL_SUCCESS && erg!=SQL_SUCCESS_WITH_INFO) {
				return false;
			}

			// column nullable
			erg=SQLColAttribute(stmt,i+1,SQL_COLUMN_NULLABLE,
					NULL,0,NULL,&(col[i].nullable));
			if (erg!=SQL_SUCCESS && erg!=SQL_SUCCESS_WITH_INFO) {
				return false;
			}

			// primary key

			// unique

			// part of key

			// unsigned number
			erg=SQLColAttribute(stmt,i+1,SQL_COLUMN_UNSIGNED,
					NULL,0,NULL,&(col[i].unsignednumber));
			if (erg!=SQL_SUCCESS && erg!=SQL_SUCCESS_WITH_INFO) {
				return false;
			}

			// zero fill

			// binary

			// autoincrement
			erg=SQLColAttribute(stmt,i+1,
					SQL_COLUMN_AUTO_INCREMENT,
					NULL,0,NULL,&(col[i].autoincrement));
			if (erg!=SQL_SUCCESS && erg!=SQL_SUCCESS_WITH_INFO) {
				return false;
			}
		}

		// bind the column to a buffer
		erg=SQLBindCol(stmt,i+1,SQL_C_CHAR,
				field[i],MAX_ITEM_BUFFER_SIZE,
				(SQLINTEGER *)&indicator[i]);
		if (erg!=SQL_SUCCESS && erg!=SQL_SUCCESS_WITH_INFO) {
			return false;
		}
	}

	// get the row count
	erg=SQLRowCount(stmt,&affectedrows);
	if (erg!=SQL_SUCCESS && erg!=SQL_SUCCESS_WITH_INFO) {
		return false;
	}
	return true;
}

void db2cursor::errorMessage(const char **errorstring,
				int64_t *errorcode,
				bool *liveconnection) {

	SQLCHAR		error[501];
	SQLCHAR		state[10];
	SQLINTEGER	nativeerrnum;
	SQLSMALLINT	errnum;

	// need to use SQLGetDiagRec and SQLGetDiagField here...
	SQLError(db2conn->env,db2conn->dbc,
			stmt,state,&nativeerrnum,error,500,&errnum);
	if (errormsg) {
		delete errormsg;
	}
	errormsg=new stringbuffer();
	errormsg->append((const char *)error);

	// When the DB goes down, DB2 first reports one error:
	// 	[IBM][CLI Driver] SQL1224N  A database agent could not be
	//	started to service a request, or was terminated as a result of
	//	a database system shutdown or a force command.  SQLSTATE=55032
	//	(in this case nativeerrnum==-1224 and errnum==184)
	// then upon repeated attempts to run a query, it reports:
	//	[IBM][CLI Driver] CLI0106E  Connection is closed. SQLSTATE=08003
	//	(in this case nativeerrnum==-99999 and errnum==64)
	// here's another one
	//	[IBM][CLI Driver] SQL1224N  The database manager is not able to
	//	 accept new requests, has terminated all requests in progress,
	//	or has terminated your particular request due to a problem with
	//	your request.  SQLSTATE=55032

	// We need to catch both...
	if ((nativeerrnum==-1224 && errnum==184) ||
		(nativeerrnum==-99999 && errnum==64) ||
		(nativeerrnum==-1224 && errnum==220)) {
		*liveconnection=false;
	} else {
		*liveconnection=true;
	}

	// set return values
	*errorstring=errormsg->getString();
	*errorcode=errnum;
}

bool db2cursor::knowsRowCount() {
	return false;
}

uint64_t db2cursor::rowCount() {
	return 0;
}

bool db2cursor::knowsAffectedRows() {
	return true;
}

uint64_t db2cursor::affectedRows() {
	return affectedrows;
}

uint32_t db2cursor::colCount() {
	return ncols;
}

const char * const *db2cursor::columnNames() {
	for (SQLSMALLINT i=0; i<ncols; i++) {
		columnnames[i]=col[i].name;
	}
	return columnnames;
}

uint16_t db2cursor::columnTypeFormat() {
	return (uint16_t)COLUMN_TYPE_IDS;
}

void db2cursor::returnColumnInfo() {

	// a useful variable
	uint16_t	type;

	// for each column...
	for (SQLSMALLINT i=0; i<ncols; i++) {

		uint16_t	binary=0;
		if (col[i].type==SQL_BIGINT) {
			type=BIGINT_DATATYPE;
		} else if (col[i].type==SQL_BINARY) {
			type=BINARY_DATATYPE;
			binary=1;
		} else if (col[i].type==SQL_BIT) {
			type=BIT_DATATYPE;
		} else if (col[i].type==SQL_CHAR) {
			type=CHAR_DATATYPE;
		} else if (col[i].type==SQL_TYPE_DATE) {
			type=DATE_DATATYPE;
		} else if (col[i].type==SQL_DECIMAL) {
			type=DECIMAL_DATATYPE;
		} else if (col[i].type==SQL_DOUBLE) {
			type=DOUBLE_DATATYPE;
		} else if (col[i].type==SQL_FLOAT) {
			type=FLOAT_DATATYPE;
		} else if (col[i].type==SQL_INTEGER) {
			type=INTEGER_DATATYPE;
		} else if (col[i].type==SQL_LONGVARBINARY) {
			type=LONGVARBINARY_DATATYPE;
			binary=1;
		} else if (col[i].type==SQL_LONGVARCHAR) {
			type=LONGVARCHAR_DATATYPE;
		} else if (col[i].type==SQL_NUMERIC) {
			type=NUMERIC_DATATYPE;
		} else if (col[i].type==SQL_REAL) {
			type=REAL_DATATYPE;
		} else if (col[i].type==SQL_SMALLINT) {
			type=SMALLINT_DATATYPE;
		} else if (col[i].type==SQL_TYPE_TIME) {
			type=TIME_DATATYPE;
		} else if (col[i].type==SQL_TYPE_TIMESTAMP) {
			type=TIMESTAMP_DATATYPE;
		} else if (col[i].type==SQL_TINYINT) {
			type=TINYINT_DATATYPE;
		} else if (col[i].type==SQL_VARBINARY) {
			type=VARBINARY_DATATYPE;
			binary=1;
		} else if (col[i].type==SQL_VARCHAR) {
			type=VARCHAR_DATATYPE;
		// DB2 has more datatypes than ODBC...
		} else if (col[i].type==SQL_GRAPHIC) {
			type=GRAPHIC_DATATYPE;
			binary=1;
		} else if (col[i].type==SQL_VARGRAPHIC) {
			type=VARGRAPHIC_DATATYPE;
			binary=1;
		} else if (col[i].type==SQL_LONGVARGRAPHIC) {
			type=LONGVARGRAPHIC_DATATYPE;
			binary=1;
		} else if (col[i].type==SQL_BLOB) {
			type=BLOB_DATATYPE;
			binary=1;
		} else if (col[i].type==SQL_CLOB) {
			type=CLOB_DATATYPE;
		} else if (col[i].type==SQL_DBCLOB) {
			type=DBCLOB_DATATYPE;
		} else if (col[i].type==SQL_DATALINK) {
			type=DATALINK_DATATYPE;
		} else if (col[i].type==SQL_USER_DEFINED_TYPE) {
			type=USER_DEFINED_TYPE_DATATYPE;
		} else {
			type=UNKNOWN_DATATYPE;
		}

		// send column definition
		conn->sendColumnDefinition(col[i].name,col[i].namelength,type,
					col[i].length,col[i].precision,
					col[i].scale,col[i].nullable,0,0,
					0,col[i].unsignednumber,0,binary,
					col[i].autoincrement);
	}
}

bool db2cursor::noRowsToReturn() {
	// if there are no columns, then there can't be any rows either
	return (ncols)?false:true;
}

bool db2cursor::skipRow() {
	if (fetchRow()) {
		rowgroupindex++;
		return true;
	}
	return false;
}

bool db2cursor::fetchRow() {

	if (rowgroupindex==FETCH_AT_ONCE) {
		rowgroupindex=0;
	}
	if (rowgroupindex>0 && rowgroupindex==totalinrowgroup) {
		return false;
	}
	if (!rowgroupindex) {

		// SQLFetchScroll should return SQL_SUCCESS or
		// SQL_SUCCESS_WITH_INFO if it successfully fetched a group of
		// rows, otherwise we're at the end of the result and there are
		// no more rows to fetch.
		SQLRETURN	result=SQLFetchScroll(stmt,SQL_FETCH_NEXT,0);
		if (result!=SQL_SUCCESS && result!=SQL_SUCCESS_WITH_INFO) {
			// there are no more rows to be fetched
			return false;
		}

		// Determine the current rownumber
#if (DB2VERSION>7)
		// An apparant bug in version 8.1 causes the
		// SQL_ATTR_ROW_NUMBER to always be 1, running through
		// the row status buffer appears to work.
		int32_t	index=0;
		while (index<FETCH_AT_ONCE) {
			index++;
		}
		index=0;
		while (index<FETCH_AT_ONCE &&
			(rowstat[index]==SQL_ROW_SUCCESS ||
			rowstat[index]==SQL_ROW_SUCCESS_WITH_INFO)) {
			index++;
		}
		rownumber=totalrows+index;
#else
		SQLGetStmtAttr(stmt,SQL_ATTR_ROW_NUMBER,
				(SQLPOINTER)&rownumber,0,NULL);
#endif

		// In the event that there's a bug in SQLFetchScroll and it
		// returns SQL_SUCCESS or SQL_SUCCESS_WITH_INFO even if we were
		// at the end of the result set and there were no more rows to
		// fetch, this will also catch the end of the result set.
		// I think there was a bug like that in DB2 version 7.2.
		if (rownumber==totalrows) {
			return false;
		}
		totalinrowgroup=rownumber-totalrows;
		totalrows=rownumber;
	}
	return true;
}

void db2cursor::getField(uint32_t col,
				const char **fld, uint64_t *fldlength,
				bool *blob, bool *null) {

	// handle NULLs
	if (indicator[col][rowgroupindex]==SQL_NULL_DATA) {
		*null=true;
		return;
	}

	// handle normal datatypes
	*fld=field[col][rowgroupindex];
	*fldlength=indicator[col][rowgroupindex];
}

void db2cursor::nextRow() {
	rowgroupindex++;
}
