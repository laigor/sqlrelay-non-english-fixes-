// Copyright (c) 1999-2012  David Muse
// See the file COPYING for more information

#ifndef SQLRCONTROLLER_H
#define SQLRCONTROLLER_H

#include <config.h>
#include <defaults.h>
#include <rudiments/listener.h>
#include <rudiments/unixsocketserver.h>
#include <rudiments/inetsocketserver.h>
#include <rudiments/unixsocketclient.h>
#include <rudiments/memorypool.h>
#include <rudiments/stringbuffer.h>
#include <rudiments/regularexpression.h>
#include <rudiments/sharedmemory.h>
#include <rudiments/semaphoreset.h>

#include <tempdir.h>

#include <sqlrconnection.h>
#include <sqlrcursor.h>
#include <sqlrprotocol.h>
#include <sqlrauthenticator.h>
#include <sqlparser.h>
#include <sqltranslations.h>
#include <sqlwriter.h>
#include <sqltriggers.h>
#include <sqlrloggers.h>
#include <sqlrqueries.h>
#include <sqlrpwdencs.h>

#include <cmdline.h>

#include <defines.h>

class sqlrcontroller_svr : public listener {
	public:
			sqlrcontroller_svr();
		virtual	~sqlrcontroller_svr();

		const char	*connectStringValue(const char *variable);
		void		setAutoCommitBehavior(bool ac);
		void		setFakeTransactionBlocksBehavior(bool ftb);
		void		setFakeInputBinds(bool fake);
		void		setUser(const char *user);
		void		setPassword(const char *password);
		const char	*getUser();
		const char	*getPassword();
		bool		sendColumnInfo();
		void		addSessionTempTableForDrop(
						const char *tablename);
		void		addSessionTempTableForTrunc(
						const char *tablename);
		void		addTransactionTempTableForDrop(
						const char *tablename);
		void		addTransactionTempTableForTrunc(
						const char *tablename);
		virtual bool	createSharedMemoryAndSemaphores(const char *id);
		void		cleanUpAllCursorData();

		bool		getColumnNames(const char *query,
						stringbuffer *output);

		bool	init(int argc, const char **argv);
		sqlrconnection_svr	*initConnection(const char *dbase);
		bool	listen();
		void	closeConnection();

		bool	logIn(bool printerrors);
		void	logOut();
		void	reLogIn();

		sqlrcursor_svr	*initCursor();
		void	deleteCursor(sqlrcursor_svr *curs);
		bool	executeQuery(sqlrcursor_svr *curs,
						const char *query,
						uint32_t length);

		void	setUserAndGroup();
		bool	initCursors(int32_t count);
		void	incrementConnectionCount();
		void	decrementConnectionCount();
		void	decrementConnectedClientCount();
		void	announceAvailability(const char *unixsocket,
						uint16_t inetport,
						const char *connectionid);
		void	registerForHandoff();
		void	deRegisterForHandoff();
		bool	getUnixSocket();
		bool	openSequenceFile(file *sockseq);
		bool	lockSequenceFile(file *sockseq);
		bool	getAndIncrementSequenceNumber(file *sockseq);
		bool	unLockSequenceFile(file *sockseq);


		void		acquireAnnounceMutex();
		shmdata		*getAnnounceBuffer();
		void		signalListenerToRead();
		void		waitForListenerToFinishReading();
		void		releaseAnnounceMutex();
		void		acquireConnectionCountMutex();
		void		signalScalerToRead();
		void		releaseConnectionCountMutex();


		sqlrcursor_svr	*getCursor(uint16_t command);
		sqlrcursor_svr	*findAvailableCursor();
		void	closeCursors(bool destroy);
		void	setUnixSocketDirectory();
		bool	handlePidFile();
		void	initSession();
		int32_t	waitForClient();
		void	clientSession();
		sqlrprotocol_t	getClientProtocol();
		void	sqlrClientSession();
		bool	authenticateCommand();
		void	suspendSessionCommand();
		void	selectDatabaseCommand();
		void	getCurrentDatabaseCommand();
		void	getLastInsertIdCommand();
		void	dbHostNameCommand();
		void	dbIpAddressCommand();
		void	pingCommand();
		void	identifyCommand();
		void	autoCommitCommand();
		bool	autoCommitOn();
		bool	autoCommitOff();
		void	translateBeginTransaction(sqlrcursor_svr *cursor);
		bool	handleFakeTransactionQueries(sqlrcursor_svr *cursor,
						bool *wasfaketransactionquery);
		bool	beginFakeTransactionBlock();
		bool	endFakeTransactionBlock();
		bool	isBeginTransactionQuery(sqlrcursor_svr *cursor);
		bool	isCommitQuery(sqlrcursor_svr *cursor);
		bool	isRollbackQuery(sqlrcursor_svr *cursor);
		void	beginCommand();
		bool	begin();
		void	commitCommand();
		bool	commit();
		void	rollbackCommand();
		bool	rollback();
		void	dbVersionCommand();
		void	serverVersionCommand();
		void	bindFormatCommand();
		bool	newQueryCommand(sqlrcursor_svr *cursor);
		bool	getDatabaseListCommand(sqlrcursor_svr *cursor);
		bool	getTableListCommand(sqlrcursor_svr *cursor);
		bool	getColumnListCommand(sqlrcursor_svr *cursor);
		bool	getListCommand(sqlrcursor_svr *cursor,
						int which, bool gettable);
		bool	getListByApiCall(sqlrcursor_svr *cursor,
							int which,
							const char *table,
							const char *wild);
		bool	getListByQuery(sqlrcursor_svr *cursor,
							int which,
							const char *table,
							const char *wild);
		bool	buildListQuery(sqlrcursor_svr *cursor,
							const char *query,
							const char *table,
							const char *wild);
		void	escapeParameter(stringbuffer *buffer,
							const char *parameter);
		bool	reExecuteQueryCommand(sqlrcursor_svr *cursor);
		bool	fetchFromBindCursorCommand(sqlrcursor_svr *cursor);
		bool	fetchResultSetCommand(sqlrcursor_svr *cursor);
		void	abortResultSetCommand(sqlrcursor_svr *cursor);
		void	suspendResultSetCommand(sqlrcursor_svr *cursor);
		bool	resumeResultSetCommand(sqlrcursor_svr *cursor);
		bool	getQueryTreeCommand(sqlrcursor_svr *cursor);
		void	closeClientSocket();
		void	closeSuspendedSessionSockets();
		bool	authenticate();
		bool	getUserFromClient();
		bool	getPasswordFromClient();
		bool	connectionBasedAuth(const char *userbuffer,
						const char *passwordbuffer);
		bool	databaseBasedAuth(const char *userbuffer,
						const char *passwordbuffer);
		bool	handleQueryOrBindCursor(sqlrcursor_svr *cursor,
						bool reexecute,
						bool bindcursor,
						bool getquery);
		void	endSession();
		bool	getCommand(uint16_t *command);
		void	noAvailableCursors(uint16_t command);
		bool	getClientInfo(sqlrcursor_svr *cursor);
		bool	getQuery(sqlrcursor_svr *cursor);
		bool	getInputBinds(sqlrcursor_svr *cursor);
		bool	getOutputBinds(sqlrcursor_svr *cursor);
		bool	getBindVarCount(sqlrcursor_svr *cursor,
						uint16_t *count);
		bool	getBindVarName(sqlrcursor_svr *cursor,
						bindvar_svr *bv);
		bool	getBindVarType(bindvar_svr *bv);
		void	getNullBind(bindvar_svr *bv);
		bool	getBindSize(sqlrcursor_svr *cursor,
					bindvar_svr *bv, uint32_t *maxsize);
		bool	getStringBind(sqlrcursor_svr *cursor, bindvar_svr *bv);
		bool	getIntegerBind(bindvar_svr *bv);
		bool	getDoubleBind(bindvar_svr *bv);
		bool	getDateBind(bindvar_svr *bv);
		bool	getLobBind(sqlrcursor_svr *cursor, bindvar_svr *bv);
		bool	getSendColumnInfo();
		bool	getSkipAndFetch(sqlrcursor_svr *cursor);
		bool	handleBinds(sqlrcursor_svr *cursor);
		bool	processQuery(sqlrcursor_svr *cursor,
						bool reexecute,
						bool bindcursor);
		void	rewriteQuery(sqlrcursor_svr *cursor);
		bool	translateQuery(sqlrcursor_svr *cursor);
		void	translateBindVariables(sqlrcursor_svr *cursor);
		bool	matchesNativeBindFormat(const char *bind);
		void	translateBindVariableInStringAndArray(
					sqlrcursor_svr *cursor,
					stringbuffer *currentbind,
					uint16_t bindindex,
					stringbuffer *newquery);
		void	translateBindVariableInArray(
						sqlrcursor_svr *cursor,
						const char *currentbind,
						uint16_t bindindex);
		void	translateBindVariablesFromMappings(
						sqlrcursor_svr *cursor);
		void	commitOrRollback(sqlrcursor_svr *cursor);
		void	returnResultSet();
		void	returnOutputBindValues(sqlrcursor_svr *cursor);
		void	returnOutputBindBlob(sqlrcursor_svr *cursor,
							uint16_t index);
		void	returnOutputBindClob(sqlrcursor_svr *cursor,
							uint16_t index);
		void	sendLobOutputBind(sqlrcursor_svr *cursor,
							uint16_t index);
		void	returnResultSetHeader(sqlrcursor_svr *cursor);
		void	returnColumnInfo(sqlrcursor_svr *cursor,
						uint16_t format);
		bool	returnResultSetData(sqlrcursor_svr *cursor,
						bool getskipandfetch);
		void	sendRowCounts(bool knowsactual,
						uint64_t actual,
						bool knowsaffected,
						uint64_t affected);
		void	sendColumnDefinition(const char *name, 
						uint16_t namelen, 
						uint16_t type, 
						uint32_t size,
						uint32_t precision,
						uint32_t scale,
						uint16_t nullable,
						uint16_t primarykey,
						uint16_t unique,
						uint16_t partofkey,
						uint16_t unsignednumber,
						uint16_t zerofill,
						uint16_t binary,
						uint16_t autoincrement);
		void	sendColumnDefinitionString(const char *name, 
						uint16_t namelen, 
						const char *type, 
						uint16_t typelen, 
						uint32_t size,
						uint32_t precision,
						uint32_t scale,
						uint16_t nullable,
						uint16_t primarykey,
						uint16_t unique,
						uint16_t partofkey,
						uint16_t unsignednumber,
						uint16_t zerofill,
						uint16_t binary,
						uint16_t autoincrement);
		bool	skipRows(sqlrcursor_svr *cursor, uint64_t rows);
		void	returnRow(sqlrcursor_svr *cursor);
		void	sendNullField();
		void	sendField(sqlrcursor_svr *cursor,
						uint32_t index,
						const char *data,
						uint32_t size);
		void	sendField(const char *data, uint32_t size);
		void	sendLobField(sqlrcursor_svr *cursor, uint32_t col);
		void	startSendingLong(uint64_t longlength);
		void	sendLongSegment(const char *data, uint32_t size);
		void	endSendingLong();
		void	returnError(bool disconnect);
		void	returnError(sqlrcursor_svr *cursor, bool disconnect);

		void	dropTempTables(sqlrcursor_svr *cursor);
		void	dropTempTable(sqlrcursor_svr *cursor,
						const char *tablename);
		void	truncateTempTables(sqlrcursor_svr *cursor);
		void	truncateTempTable(sqlrcursor_svr *cursor,
						const char *tablename);

		void	initDatabaseAvailableFileName();
		void	waitForAvailableDatabase();
		void	markDatabaseUnavailable();
		void	markDatabaseAvailable();

		bool	attemptLogIn(bool printerrors);
		void	setAutoCommit(bool ac);
		bool	openSockets();

		void	sessionStartQueries();
		void	sessionEndQueries();
		void	sessionQuery(const char *query);

		bool	initQueryLog();
		bool	writeQueryLog(sqlrcursor_svr *cursor);

		void	initConnStats();
		void	clearConnStats();
		void	updateState(enum sqlrconnectionstate_t state);
		void	updateClientSessionStartTime();
		void	updateCurrentQuery(const char *query,
						uint32_t querylen);
		void	updateClientInfo(const char *info,
						uint32_t infolen);
		void	updateClientAddr();
		void	incrementOpenDatabaseConnections();
		void	decrementOpenDatabaseConnections();
		void	incrementOpenDatabaseCursors();
		void	decrementOpenDatabaseCursors();
		void	incrementOpenClientConnections();
		void	decrementOpenClientConnections();
		void	incrementTimesNewCursorUsed();
		void	incrementTimesCursorReused();
		void	incrementQueryCounts(sqlrquerytype_t querytype);
		void	incrementTotalErrors();
		void	incrementAuthenticateCount();
		void	incrementSuspendSessionCount();
		void	incrementEndSessionCount();
		void	incrementPingCount();
		void	incrementIdentifyCount();
		void	incrementAutocommitCount();
		void	incrementBeginCount();
		void	incrementCommitCount();
		void	incrementRollbackCount();
		void	incrementDbVersionCount();
		void	incrementBindFormatCount();
		void	incrementServerVersionCount();
		void	incrementSelectDatabaseCount();
		void	incrementGetCurrentDatabaseCount();
		void	incrementGetLastInsertIdCount();
		void	incrementDbHostNameCount();
		void	incrementDbIpAddressCount();
		void	incrementNewQueryCount();
		void	incrementReexecuteQueryCount();
		void	incrementFetchFromBindCursorCount();
		void	incrementFetchResultSetCount();
		void	incrementAbortResultSetCount();
		void	incrementSuspendResultSetCount();
		void	incrementResumeResultSetCount();
		void	incrementGetDbListCount();
		void	incrementGetTableListCount();
		void	incrementGetColumnListCount();
		void	incrementGetQueryTreeCount();
		void	incrementReLogInCount();

		void	logDebugMessage(const char *info);
		void	logClientConnected();
		void	logClientConnectionRefused(const char *info);
		void	logClientDisconnected(const char *info);
		void	logClientProtocolError(sqlrcursor_svr *cursor,
							const char *info,
							ssize_t result);
		void	logDbLogIn();
		void	logDbLogOut();
		void	logDbError(sqlrcursor_svr *cursor, const char *info);
		void	logQuery(sqlrcursor_svr *cursor);
		void	logInternalError(sqlrcursor_svr *cursor,
							const char *info);

		const char	*user;
		const char	*password;

		bool		dbselected;
		char		*originaldb;

		tempdir		*tmpdir;

		connectstringcontainer	*constr;

		char		*updown;

		uint16_t	inetport;
		char		*unixsocket;
		char		*unixsocketptr;
		size_t		unixsocketptrlen;

		uint16_t	sendcolumninfo;

		sqlrauthenticator	*authc;

		char		userbuffer[USERSIZE];
		char		passwordbuffer[USERSIZE];

		char		lastuserbuffer[USERSIZE];
		char		lastpasswordbuffer[USERSIZE];
		bool		lastauthsuccess;

		bool		commitorrollback;

		bool		autocommitforthissession;

		bool		translatebegins;
		bool		faketransactionblocks;
		bool		faketransactionblocksautocommiton;
		bool		intransactionblock;

		bool		translatebinds;

		const char	*isolationlevel;
		bool		ignoreselectdb;

		int32_t		accepttimeout;
		bool		suspendedsession;

		inetsocketserver	**serversockin;
		uint64_t		serversockincount;
		unixsocketserver	*serversockun;

		filedescriptor	*clientsock;

		memorypool	*bindpool;
		memorypool	*bindmappingspool;
		namevaluepairs	*inbindmappings;
		namevaluepairs	*outbindmappings;

		bool		debugsqltranslation;
		bool		debugtriggers;

		dynamiclib		dl;
		sqlrconnection_svr	*conn;

		uint16_t	cursorcount;
		uint16_t	mincursorcount;
		uint16_t	maxcursorcount;
		sqlrcursor_svr	**cur;

		sqlparser	*sqlp;
		sqltranslations	*sqlt;
		sqlwriter	*sqlw;
		sqltriggers	*sqltr;
		sqlrloggers	*sqlrlg;
		sqlrqueries	*sqlrq;
		sqlrpwdencs	*sqlrpe;

		char		*decrypteddbpassword;

		unixsocketclient	handoffsockun;
		bool			proxymode;
		uint32_t		proxypid;

		bool		connected;
		bool		inclientsession;
		bool		loggedin;

		bool		scalerspawned;
		const char	*connectionid;
		int32_t		ttl;

		char		*pidfile;

		bool		fakeinputbinds;

		uint64_t	skip;
		uint64_t	fetch;

		semaphoreset	*semset;
		sharedmemory	*idmemory;
		cmdline		*cmdl;
		sqlrconfigfile	*cfgfl;

		shmdata			*shm;
		sqlrconnstatistics	*connstats;

		char		*clientinfo;
		uint64_t	clientinfolen;

		stringlist	sessiontemptablesfordrop;
		stringlist	sessiontemptablesfortrunc;
		stringlist	transtemptablesfordrop;
		stringlist	transtemptablesfortrunc;

		int32_t		idleclienttimeout;

		bool		decrementonclose;
		bool		silent;

		stringbuffer	debugstr;

		uint64_t	maxclientinfolength;
		uint32_t	maxquerysize;
		uint16_t	maxbindcount;
		uint16_t	maxbindnamelength;
		uint32_t	maxstringbindvaluelength;
		uint32_t	maxlobbindvaluelength;
		uint32_t	maxerrorlength;

		int64_t		loggedinsec;
		int64_t		loggedinusec;

		const char	*dbhostname;
		const char	*dbipaddress;

		bool		reformatdatetimes;
};

#endif
