// Copyright (c) 1999-2001  David Muse
// See the file COPYING for more information

#include <config.h>
#include <defaults.h>
#include <sqlrconfigfile.h>
#include <cmdline.h>
#include <rudiments/process.h>

// for ceil()
#include <math.h>

#include <stdio.h>

#ifdef RUDIMENTS_NAMESPACE
using namespace rudiments;
#endif

#define MAX_CONNECTIONS 200

int32_t getConnections(sqlrconfigfile *cfgfile, bool override) {
	int32_t	connections=cfgfile->getConnections();
	if (!override && connections>MAX_CONNECTIONS) {
		connections=MAX_CONNECTIONS;
	}
	return connections;
}

bool startListener(const char *id, const char *config,
			const char *localstatedir, bool listenerdebug) {

	// start the listener
	printf("\nStarting listener:\n");
	
	stringbuffer	command;
	command.append("sqlr-listener");
	command.append(" -id ")->append(id);
	command.append(" -config ")->append(config);
	if (charstring::length(localstatedir)) {
		command.append(" -localstatedir ")->append(localstatedir);
	}
	if (listenerdebug) {
		command.append(" -debug");
	}
	printf("  %s\n",command.getString());

	bool	success=!system(command.getString());

	if (!success) {
		printf("\nsqlr-listener failed to start.\n");
	}

	return success;
}


bool startConnection(bool strace, const char *dbase,
				const char *id, const char *connectionid,
				const char *config, const char *localstatedir,
				bool connectiondebug) {

	stringbuffer	command;
	if (strace) {
		command.append("strace -ff -o sqlr-connection-strace ");
	}
	command.append("sqlr-connection-")->append(dbase);
	command.append(" -id ")->append(id);
	if (connectionid) {
		command.append(" -connectionid ")->append(connectionid);
	}
	command.append(" -config ")->append(config);
	if (charstring::length(localstatedir)) {
		command.append(" -localstatedir ")->append(localstatedir);
	}
	if (connectiondebug) {
		command.append(" -debug");
	}
	if (strace) {
		command.append(" &");
	}

	printf("  %s\n",command.getString());

	bool	success=!system(command.getString());

	if (!success) {
		printf("\nsqlr-connection-%s failed to start.\n",dbase);
	}

	return success;
}

bool startConnections(sqlrconfigfile *cfgfile, bool strace,
				const char *id, const char *config,
				const char *localstatedir,
				bool connectiondebug,
				bool overridemaxconn) {

	// get the connection count and total metric
	linkedlist< connectstringcontainer *>	*connectionlist=
						cfgfile->getConnectStringList();

	// if the metrictotal was 0, start no connections
	if (!cfgfile->getMetricTotal()) {
		return true;
	}

	// if no connections were defined in the config file,
	// start 1 default one
	if (!cfgfile->getConnectionCount()) {
		return !startConnection(strace,cfgfile->getDbase(),id,config,
					localstatedir,NULL,connectiondebug);
	}

	// get number of connections
	int32_t	connections=getConnections(cfgfile,overridemaxconn);

	// start the connections
	connectstringnode	*csn=connectionlist->getFirstNode();
	connectstringcontainer	*csc;
	int32_t	metric=0;
	int32_t	startup=0;
	int32_t	totalstarted=0;
	bool	done=false;
	while (!done) {

		// get the appropriate connection
		csc=csn->getData();
	
		// scale the number of each connection to start by 
		// each connection's metric vs the total number of 
		// connections to start up
		metric=csc->getMetric();
		if (metric>0) {
			startup=(int32_t)ceil(
				((double)(metric*connections))/
				((double)cfgfile->getMetricTotal()));
		} else {
			startup=0;
		}

		// keep from starting too many connections
		if (totalstarted+startup>connections) {
			startup=connections-totalstarted;
			done=true;
		}

		printf("\nStarting %d connections to ",startup);
		printf("%s :\n",csc->getConnectionId());

		// fire them up
		for (int32_t i=0; i<startup; i++) {
			if (!startConnection(strace,cfgfile->getDbase(),
					id,csc->getConnectionId(),
					config,localstatedir,connectiondebug)) {
				// it's ok if at least 1 connection started up
				return (totalstarted>0 || i>0);
			}
		}

		// have we started enough connections?
		totalstarted=totalstarted+startup;
		if (totalstarted==connections) {
			done=true;
		}

		// next...
		csn=csn->getNext();
	}
	return true;
}

bool startScaler(sqlrconfigfile *cfgfile, const char *id, const char *config,
			const char *localstatedir, bool connectiondebug) {

	// don't start the scalar if unless dynamic scaling is enabled
	if (!cfgfile->getDynamicScaling()) {
		return true;
	}

	printf("\nStarting scaler:\n");
	
	stringbuffer	command;
	command.append("sqlr-scaler")->append(" -id ")->append(id);
	if (connectiondebug) {
		command.append(" -debug ");
	}
	command.append(" -config ")->append(config);
	if (charstring::length(localstatedir)) {
		command.append(" -localstatedir ")->append(localstatedir);
	}
	printf("  %s\n",command.getString());

	bool	success=!system(command.getString());

	if (!success) {
		printf("\nsqlr-scaler failed to start.\n");
	}

	return success;
}

bool startCacheManager(const char *localstatedir) {

	// create a ps command that will detect if the cachemanager is running
	stringbuffer	command;
	command.append(PS)->append(" | grep sqlr-cachemanager | grep -v grep");
	
	// run the command
	FILE	*cmd=popen(command.getString(),"r");
	command.clear();
	
	// get the result
	stringbuffer	contents;
	char		character;
	while (!feof(cmd) && (character=fgetc(cmd))>-1) {
		contents.append(character);
	}
	pclose(cmd);
	
	// if the cachemanger isn't running, start it
	if (!charstring::length(contents.getString())) {
	
		printf("\nStarting cache manager:\n");
	
		command.append("sqlr-cachemanager");
		if (charstring::length(localstatedir)) {
			command.append(" -cachedirs ")->append(localstatedir);
			command.append("/sqlrelay/cache");
			command.append(" -localstatedir ");
			command.append(localstatedir);
		}
		printf("  %s\n",command.getString());

		bool	success=!system(command.getString());

		if (!success) {
			printf("\nsqlr-cachemanager failed to start.\n");
			return false;
		}
	
	} else {
		printf("\ncache manager already running.\n");
	}

	return true;
}

int main(int argc, const char **argv) {

	#include <version.h>

	// get the command line args
	cmdline	cmdl(argc,argv);
	const char	*localstatedir=cmdl.getValue("-localstatedir");
	bool		strace=cmdl.found("-strace");
	const char	*id=cmdl.getId();
	const char	*config=cmdl.getConfig();
	bool		overridemaxconn=cmdl.found("-overridemaxconnections");

	// default id warning
	if (!charstring::compare(cmdl.getId(),DEFAULT_ID)) {
		fprintf(stderr,"Warning: using default id.\n");
	}

	// parse the config file(s)
	sqlrconfigfile	cfgfile;
	if (!cfgfile.parse(config,id)) {
		process::exit(1);
	}

	// start listener, connections, scaler, cachemanager
	bool	exitstatus=!(startListener(id,config,
				localstatedir,cfgfile.getDebugListener()) &&
			startConnections(&cfgfile,strace,id,config,
				localstatedir,cfgfile.getDebugConnection(),
				overridemaxconn) &&
			startScaler(&cfgfile,id,config,localstatedir,
				cfgfile.getDebugConnection()) &&
			startCacheManager(localstatedir));

	// many thanks...
	// these companies don't exist any more so it's
	// probably ok not to display the attribution any more
	/*printf("\n\nThanks to MP3.com for sponsoring: \n");
	printf("	Clustered/Replicated database support.\n");
	printf("	Perl API.\n");
	printf("Thanks to FeedLounge for sponsoring: \n");
	printf("	Query routing and filtering.\n");*/
	
	// successful exit
	process::exit(exitstatus);
}
