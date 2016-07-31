// Copyright (c) 2014  David Muse
// See the file COPYING for more information

#include <sqlrelay/sqlrserver.h>

#include <rudiments/stdio.h>
//#define DEBUG_MESSAGES 1
#include <rudiments/debugprint.h>

#include <config.h>

#ifndef SQLRELAY_ENABLE_SHARED
	extern "C" {
		#include "sqlrprotocoldeclarations.cpp"
	}
#endif

sqlrprotocols::sqlrprotocols(sqlrservercontroller *cont,
				sqlrpaths *sqlrpth, bool debug) {
	debugFunction();
	this->cont=cont;
	libexecdir=sqlrpth->getLibExecDir();
	this->debug=debug;
}

sqlrprotocols::~sqlrprotocols() {
	debugFunction();
	unload();
}

bool sqlrprotocols::load(xmldomnode *parameters) {
	debugFunction();

	unload();

	// run through the listeners
	uint16_t	i=0;
	for (xmldomnode *listener=parameters->getFirstTagChild();
			!listener->isNullNode();
			listener=listener->getNextTagSibling()) {

		debugPrintf("loading protocol ...\n");

		// load protocol
		loadProtocol(i,listener);

		i++;
	}
	return true;
}

void sqlrprotocols::unload() {
	debugFunction();
	for (linkedlistnode< dictionarynode< uint16_t, sqlrprotocolplugin * > *>
			*node=protos.getList()->getFirst();
			node; node=node->getNext()) {
		sqlrprotocolplugin	*sqlrpp=node->getValue()->getValue();
		delete sqlrpp->pr;
		delete sqlrpp->dl;
		delete sqlrpp;
	}
	protos.clear();
}

void sqlrprotocols::loadProtocol(uint16_t index, xmldomnode *listener) {
	debugFunction();

	// ignore any non-listener entries
	if (charstring::compare(listener->getName(),"listener")) {
		return;
	}

	// get the protocol name
	const char	*module=listener->getAttributeValue("protocol");

	debugPrintf("loading protocol: %s\n",module);

#ifdef SQLRELAY_ENABLE_SHARED
	// load the protocol module
	stringbuffer	modulename;
	modulename.append(libexecdir);
	modulename.append(SQLR);
	modulename.append("protocol_");
	modulename.append(module)->append(".")->append(SQLRELAY_MODULESUFFIX);
	dynamiclib	*dl=new dynamiclib();
	if (!dl->open(modulename.getString(),true,true)) {
		stdoutput.printf("failed to load protocol module: %s\n",module);
		char	*error=dl->getError();
		stdoutput.printf("%s\n",error);
		delete[] error;
		delete dl;
		return;
	}

	// load the protocol itself
	stringbuffer	functionname;
	functionname.append("new_sqlrprotocol_")->append(module);
	sqlrprotocol *(*newProtocol)
				(sqlrservercontroller *, xmldomnode *, bool)=
			(sqlrprotocol *(*)
				(sqlrservercontroller *, xmldomnode *, bool))
				dl->getSymbol(functionname.getString());
	if (!newProtocol) {
		stdoutput.printf("failed to create protocol: %s\n",module);
		char	*error=dl->getError();
		stdoutput.printf("%s\n",error);
		delete[] error;
		dl->close();
		delete dl;
		return;
	}
	sqlrprotocol	*pr=(*newProtocol)(cont,listener,debug);

#else

	dynamiclib	*dl=NULL;
	sqlrprotocol	*pr;
	#include "sqlrprotocolassignments.cpp"
	{
		pr=NULL;
	}
#endif

	// add the plugin to the list
	sqlrprotocolplugin	*sqlrpp=new sqlrprotocolplugin;
	sqlrpp->pr=pr;
	sqlrpp->dl=dl;
	protos.setValue(index,sqlrpp);
}

sqlrprotocol *sqlrprotocols::getProtocol(uint16_t index) {
	debugFunction();
	sqlrprotocolplugin	*pp=NULL;
	if (!protos.getValue(index,&pp)) {
		return NULL;
	}
	return pp->pr;
}
