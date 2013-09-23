// Copyright (c) 1999-2012  David Muse
// See the file COPYING for more information

#ifndef SQLRLISTENER_H
#define SQLRLISTENER_H

#include <config.h>

#include <cmdline.h>
#include <tempdir.h>
#include <sqlrconfigfile.h>
#include <sqlrauthenticator.h>
#include <sqlrloggers.h>

#include <rudiments/signalclasses.h>
#include <rudiments/daemonprocess.h>
#include <rudiments/listener.h>
#include <rudiments/unixsocketserver.h>
#include <rudiments/inetsocketserver.h>
#include <rudiments/filedescriptor.h>
#include <rudiments/semaphoreset.h>
#include <rudiments/sharedmemory.h>
#include <rudiments/regularexpression.h>

#include <defaults.h>
#include <defines.h>

class handoffsocketnode {
	friend class sqlrlistener;
	private:
		uint32_t	pid;
		filedescriptor	*sock;
};

class sqlrlistener : public daemonprocess, public listener {
	public:
			sqlrlistener();
			~sqlrlistener();
		void	handleSignals(void (*shutdownfunction)(int32_t));
		bool	initListener(int argc, const char **argv);
		void	listen();
	private:
		void	cleanUp();
		void	setUserAndGroup();
		bool	verifyAccessToConfigFile(const char *configfile);
		bool	handlePidFile(const char *id);
		void	handleDynamicScaling();
		void	setHandoffMethod(const char *id);
		void	setIpPermissions();
		bool	createSharedMemoryAndSemaphores(const char *id);
		void	ipcFileError(const char *idfilename);
		void	keyError(const char *idfilename);
		void	shmError(const char *id, int shmid);
		void	semError(const char *id, int semid);
		void	setStartTime();
		bool	listenOnClientSockets();
		bool	listenOnHandoffSocket(const char *id);
		bool	listenOnDeregistrationSocket(const char *id);
		bool	listenOnFixupSocket(const char *id);
		filedescriptor	*waitForTraffic();
		bool	handleTraffic(filedescriptor *fd);
		bool	registerHandoff(filedescriptor *sock);
		bool	deRegisterHandoff(filedescriptor *sock);
		bool	fixup(filedescriptor *sock);
		bool	deniedIp(filedescriptor *clientsock);
		void	forkChild(filedescriptor *clientsock);
		void	clientSession(filedescriptor *clientsock);
		void    errorClientSession(filedescriptor *clientsock,
					int64_t errnum, const char *err);
		bool	acquireShmAccess();
		bool	releaseShmAccess();
		bool	acceptAvailableConnection(bool *alldbsdown);
		bool	doneAcceptingAvailableConnection();
		bool	handOffOrProxyClient(filedescriptor *sock);
		bool	getAConnection(uint32_t *connectionpid,
					uint16_t *inetport,
					char *unixportstr,
					uint16_t *unixportstrlen,
					filedescriptor *sock);
		bool	findMatchingSocket(uint32_t connectionpid,
					filedescriptor *connectionsock);
		bool	requestFixup(uint32_t connectionpid,
					filedescriptor *connectionsock);
		bool	proxyClient(pid_t connectionpid,
					filedescriptor *connectionsock,
					filedescriptor *clientsock);
		bool	connectionIsUp(const char *connectionid);
		void	pingDatabase(uint32_t connectionpid,
					const char *unixportstr,
					uint16_t inetport);
		void	waitForClientClose(bool passstatus,
					filedescriptor *clientsock);

		void		setMaxListeners(uint32_t maxlisteners);
		void		incrementMaxListenersErrors();
		void		incrementConnectedClientCount();
		void		decrementConnectedClientCount();
		uint32_t	incrementForkedListeners();
		uint32_t	decrementForkedListeners();
		void		incrementBusyListeners();
		void		decrementBusyListeners();
		int32_t		getBusyListeners();

		void	logDebugMessage(const char *info);
		void	logClientProtocolError(const char *info,
						ssize_t result);
		void	logClientConnectionRefused(const char *info);
		void	logInternalError(const char *info);

		static void	alarmHandler(int32_t signum);
		static void	sigUsr1Handler(int32_t signum);

	public:
		uint32_t	maxconnections;
		bool		dynamicscaling;

		int64_t		maxlisteners;
		uint64_t	listenertimeout;

		char		*pidfile;
		tempdir		*tmpdir;

		sqlrauthenticator	*authc;
		sqlrloggers		*sqlrlg;

		stringbuffer	debugstr;

		// FIXME: these shouldn't have to be pointers, right, but
		// it appears that they do have to be or their destructors don't
		// get called for some reason.
		semaphoreset	*semset;
		sharedmemory	*idmemory;
		shmdata		*shm;

		bool	init;

		unixsocketserver	*clientsockun;
		inetsocketserver	**clientsockin;
		uint64_t			clientsockincount;

		unixsocketserver	*mysqlclientsockun;
		inetsocketserver	**mysqlclientsockin;
		uint64_t		mysqlclientsockincount;

		char	*unixport;
		char	*mysqlunixport;

		unixsocketserver	*handoffsockun;
		unixsocketserver	*removehandoffsockun;
		unixsocketserver	*fixupsockun;
		char			*fixupsockname;

		uint16_t		handoffmode;
		handoffsocketnode	*handoffsocklist;

		regularexpression	*allowed;
		regularexpression	*denied;

		cmdline		*cmdl;

		uint32_t	maxquerysize;
		uint16_t	maxbindcount;
		uint16_t	maxbindnamelength;
		int32_t		idleclienttimeout;

		bool	isforkedchild;

		sqlrconfigfile		cfgfl;

		uint32_t	runningconnections;

		static	signalhandler		alarmhandler;
		static	volatile sig_atomic_t		alarmrang;

		static	signalhandler		sigusr1handler;
		static	volatile sig_atomic_t		gotsigusr1;
};

#endif
