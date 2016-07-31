// Copyright (c) 1999-2012  David Muse
// See the file COPYING for more information

#include <sqlrelay/sqlrserver.h>

#include <rudiments/xmldomnode.h>
#include <rudiments/stdio.h>
//#define DEBUG_MESSAGES 1
#include <rudiments/debugprint.h>

#include <config.h>

#ifndef SQLRELAY_ENABLE_SHARED
	extern "C" {
		#include "sqlrloggerdeclarations.cpp"
	}
#endif

sqlrloggers::sqlrloggers(sqlrpaths *sqlrpth) {
	debugFunction();
	libexecdir=sqlrpth->getLibExecDir();
}

sqlrloggers::~sqlrloggers() {
	debugFunction();
	unload();
}

bool sqlrloggers::load(xmldomnode *parameters) {
	debugFunction();

	unload();

	// run through the logger list
	for (xmldomnode *logger=parameters->getFirstTagChild();
		!logger->isNullNode(); logger=logger->getNextTagSibling()) {

		debugPrintf("loading logger ...\n");

		// load logger
		loadLogger(logger);
	}
	return true;
}

void sqlrloggers::unload() {
	debugFunction();
	for (singlylinkedlistnode< sqlrloggerplugin * > *node=llist.getFirst();
						node; node=node->getNext()) {
		sqlrloggerplugin	*sqlrlp=node->getValue();
		delete sqlrlp->lg;
		delete sqlrlp->dl;
		delete sqlrlp;
	}
	llist.clear();
}

void sqlrloggers::loadLogger(xmldomnode *logger) {

	debugFunction();

	// ignore non-loggers
	if (charstring::compare(logger->getName(),"logger")) {
		return;
	}

	// get the logger name
	const char	*module=logger->getAttributeValue("module");
	if (!charstring::length(module)) {
		// try "file", that's what it used to be called
		module=logger->getAttributeValue("file");
		if (!charstring::length(module)) {
			return;
		}
	}

	debugPrintf("loading logger: %s\n",module);

#ifdef SQLRELAY_ENABLE_SHARED
	// load the logger module
	stringbuffer	modulename;
	modulename.append(libexecdir);
	modulename.append(SQLR);
	modulename.append("logger_");
	modulename.append(module)->append(".")->append(SQLRELAY_MODULESUFFIX);
	dynamiclib	*dl=new dynamiclib();
	if (!dl->open(modulename.getString(),true,true)) {
		stdoutput.printf("failed to load logger module: %s\n",module);
		char	*error=dl->getError();
		stdoutput.printf("%s\n",error);
		delete[] error;
		delete dl;
		return;
	}

	// load the logger itself
	stringbuffer	functionname;
	functionname.append("new_sqlrlogger_")->append(module);
	sqlrlogger *(*newLogger)(xmldomnode *)=
			(sqlrlogger *(*)(xmldomnode *))
				dl->getSymbol(functionname.getString());
	if (!newLogger) {
		stdoutput.printf("failed to create logger: %s\n",module);
		char	*error=dl->getError();
		stdoutput.printf("%s\n",error);
		delete[] error;
		dl->close();
		delete dl;
		return;
	}
	sqlrlogger	*lg=(*newLogger)(logger);

#else

	dynamiclib	*dl=NULL;
	sqlrlogger	*lg;
	#include "sqlrloggerassignments.cpp"
	{
		lg=NULL;
	}
#endif

	// add the plugin to the list
	sqlrloggerplugin	*sqlrlp=new sqlrloggerplugin;
	sqlrlp->lg=lg;
	sqlrlp->dl=dl;
	llist.append(sqlrlp);
}

void sqlrloggers::init(sqlrlistener *sqlrl,
				sqlrserverconnection *sqlrcon) {
	debugFunction();
	for (singlylinkedlistnode< sqlrloggerplugin * > *node=llist.getFirst();
						node; node=node->getNext()) {
		node->getValue()->lg->init(sqlrl,sqlrcon);
	}
}

void sqlrloggers::run(sqlrlistener *sqlrl,
				sqlrserverconnection *sqlrcon,
				sqlrservercursor *sqlrcur,
				sqlrlogger_loglevel_t level,
				sqlrevent_t event,
				const char *info) {
	debugFunction();
	for (singlylinkedlistnode< sqlrloggerplugin * > *node=llist.getFirst();
						node; node=node->getNext()) {
		node->getValue()->lg->run(sqlrl,sqlrcon,sqlrcur,
						level,event,info);
	}
}
