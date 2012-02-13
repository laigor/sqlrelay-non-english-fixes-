// Copyright (c) 1999-2011  David Muse
// See the file COPYING for more information

#include <sqltranslator.h>
#include <sqlrconnection.h>
#include <sqlrcursor.h>
#include <sqlparser.h>
#include <debugprint.h>

#include <rudiments/process.h>
#include <rudiments/character.h>

sqltranslator::sqltranslator() {
	debugFunction();
	xmld=NULL;
	tree=NULL;
	sqlrcon=NULL;
	sqlrcur=NULL;
	temptablepool=new memorypool(0,128,100);
	tempindexpool=new memorypool(0,128,100);
}

sqltranslator::~sqltranslator() {
	debugFunction();
	delete xmld;
	delete temptablepool;
	delete tempindexpool;
}

bool sqltranslator::loadRules(const char *rules) {
	debugFunction();
	delete xmld;
	xmld=new xmldom();
	if (xmld->parseString(rules)) {
		rulesnode=xmld->getRootNode()->
				getFirstTagChild("sqltranslationrules");
		return !rulesnode->isNullNode();
	}
	return false;
}

bool sqltranslator::applyRules(sqlrconnection_svr *sqlrcon,
					sqlrcursor_svr *sqlrcur,
					xmldom *querytree) {
	debugFunction();
	this->sqlrcon=sqlrcon;
	this->sqlrcur=sqlrcur;
	tree=querytree;
	return applyRulesToQuery(querytree->getRootNode());
}

void sqltranslator::endSession() {
	temptablepool->free();
	temptablemap.clear();
	tempindexmap.clear();
}

bool sqltranslator::applyRulesToQuery(xmldomnode *query) {
	debugFunction();

	for (xmldomnode *rule=rulesnode->getFirstTagChild();
		!rule->isNullNode(); rule=rule->getNextTagSibling()) {

		const char	*rulename=rule->getName();

		if (!charstring::compare(rulename,
					"translate_datatypes")) {
			if (!translateDatatypes(query,rule)) {
				return false;
			}
		} else if (!charstring::compare(rulename,
					"convert_datatypes")) {
			if (!convertDatatypes(query,rule)) {
				return false;
			}
		} else if (!charstring::compare(rulename,
				"trim_columns_compared_to_string_binds")) {
			if (!trimColumnsComparedToStringBinds(query,rule)) {
				return false;
			}
		} else if (!charstring::compare(rulename,
				"translate_date_times")) {
			if (!translateDateTimes(query,rule)) {
				return false;
			}
		} else if (!charstring::compare(rulename,
				"temp_tables_localize")) {
			if (!tempTablesLocalize(query,rule)) {
				return false;
			}
		} else if (!charstring::compare(rulename,
				"locks_nowait_by_default")) {
			if (!locksNoWaitByDefault(query)) {
				return false;
			}
		}
	}
	return true;
}

bool sqltranslator::translateDatatypes(xmldomnode *query, xmldomnode *rule) {
	debugFunction();
	return true;
}

bool sqltranslator::convertDatatypes(xmldomnode *query, xmldomnode *rule) {
	debugFunction();
	return true;
}

#include <parsedatetime.h>
char *sqltranslator::convertDateTime(const char *format,
			int16_t year, int16_t month, int16_t day,
			int16_t hour, int16_t minute, int16_t second) {

	// if no format was passed in
	if (!format) {
		return NULL;
	}

	// output buffer
	stringbuffer	output;

	// work buffer
	char		buf[5];

	// run through the format string
	const char	*ptr=format;
	while (*ptr) {

		if (!charstring::compare(ptr,"DD",2)) {
			snprintf(buf,5,"%02d",day);
			output.append(buf);
			ptr=ptr+2;
		} else if (!charstring::compare(ptr,"MM",2)) {
			snprintf(buf,5,"%02d",month);
			output.append(buf);
			ptr=ptr+2;
		} else if (!charstring::compare(ptr,"MON",3)) {
			if (month>0) {
				output.append(shortmonths[month-1]);
			}
			ptr=ptr+3;
		} else if (!charstring::compare(ptr,"Month",5)) {
			if (month>0) {
				output.append(longmonths[month-1]);
			}
			ptr=ptr+3;
		} else if (!charstring::compare(ptr,"YYYY",4)) {
			snprintf(buf,5,"%04d",year);
			output.append(buf);
			ptr=ptr+4;
		} else if (!charstring::compare(ptr,"YY",2)) {
			snprintf(buf,5,"%04d",year);
			output.append(buf+2);
			ptr=ptr+2;
		} else if (!charstring::compare(ptr,"HH24",4)) {
			snprintf(buf,5,"%02d",hour);
			output.append(buf);
			ptr=ptr+4;
		} else if (!charstring::compare(ptr,"HH",2)) {
			snprintf(buf,5,"%02d",(hour<13)?hour:hour-12);
			output.append(buf);
			ptr=ptr+2;
		} else if (!charstring::compare(ptr,"MI",2)) {
			snprintf(buf,5,"%02d",minute);
			output.append(buf);
			ptr=ptr+2;
		} else if (!charstring::compare(ptr,"SS",2)) {
			snprintf(buf,5,"%02d",second);
			output.append(buf);
			ptr=ptr+2;
		} else if (!charstring::compare(ptr,"AM",2)) {
			output.append((hour<13)?"AM":"PM");
			ptr=ptr+2;
		} else {
			output.append(*ptr);
			ptr=ptr+1;
		}
	}

	return output.detachString();
}

bool sqltranslator::translateDateTimesInQuery(xmldomnode *querynode,
							xmldomnode *rule) {
	debugFunction();
	
	// input format
	bool		ddmm=!charstring::compare(
				rule->getAttributeValue("ddmm"),
				"yes");

	// output format
	const char	*datetimeformat=rule->getAttributeValue("datetime");
	const char	*dateformat=rule->getAttributeValue("date");
	const char	*timeformat=rule->getAttributeValue("time");
	if (!datetimeformat) {
		datetimeformat="DD-MON-YYYY HH24:MI:SS";
	}
	if (!dateformat) {
		dateformat="DD-MON-YYYY";
	}
	if (!timeformat) {
		timeformat="HH24:MI:SS";
	}

	// convert this node...
	if (!charstring::compare(querynode->getName(),
					sqlparser::_verbatim) ||
		!charstring::compare(querynode->getName(),
					sqlparser::_value) ||
		!charstring::compare(querynode->getName(),
					sqlparser::_string_literal)) {

		// get the value
		const char	*value=querynode->getAttributeValue(
							sqlparser::_value);

		// leave it alone unless it's a string
		if (isString(value)) {

			// copy it and strip off the quotes
			char	*valuecopy=charstring::duplicate(value+1);
			valuecopy[charstring::length(valuecopy)-1]='\0';

			// variables
			int16_t	year=-1;
			int16_t	month=-1;
			int16_t	day=-1;
			int16_t	hour=-1;
			int16_t	minute=-1;
			int16_t	second=-1;
	
			// parse the date/time
			if (parseDateTime(valuecopy,ddmm,
						&year,&month,&day,
						&hour,&minute,&second)) {

				// decide which format to use
				bool	validdate=(year!=-1 &&
						month!=-1 && day!=-1);
				bool	validtime=(hour!=-1 &&
						minute!=-1 && second!=-1);
				const char	*format=NULL;
				if (validdate && validtime) {
					format=datetimeformat;
				} else if (validdate) {
					format=dateformat;
				} else if (validtime) {
					format=timeformat;
				}

				// convert it
				char	*converted=convertDateTime(
							format,
							year,month,day,
							hour,minute,second);
				if (converted) {

					if (sqlrcon->debugsqltranslation) {
						printf("    %s -> %s\n",
							valuecopy,converted);
					}

					// repackage as a string
					stringbuffer	output;
					output.append('\'');
					output.append(converted);
					output.append('\'');

					// update the value
					setAttribute(querynode,
							sqlparser::_value,
							output.getString());

					// clean up
					delete[] converted;
				}
			}
	
			// clean up
			delete[] valuecopy;
		}
	}

	// convert child nodes...
	for (xmldomnode *node=querynode->getFirstTagChild();
			!node->isNullNode(); node=node->getNextTagSibling()) {
		if (!translateDateTimesInQuery(node,rule)) {
			return false;
		}
	}
	return true;
}

bool sqltranslator::translateDateTimesInBindVariables(
						xmldomnode *querynode,
						xmldomnode *rule) {
	debugFunction();

	// input format
	bool		ddmm=!charstring::compare(
				rule->getAttributeValue("ddmm"),
				"yes");

	// output format
	const char	*datetimeformat=rule->getAttributeValue("datetime");
	const char	*dateformat=rule->getAttributeValue("date");
	const char	*timeformat=rule->getAttributeValue("time");
	if (!datetimeformat) {
		datetimeformat="DD-MON-YYYY HH24:MI:SS";
	}
	if (!dateformat) {
		dateformat="DD-MON-YYYY";
	}
	if (!timeformat) {
		timeformat="HH24:MI:SS";
	}

	// run through the bind variables...
	for (uint16_t i=0; i<sqlrcur->inbindcount; i++) {

		// get the variable
		bindvar_svr	*bind=&(sqlrcur->inbindvars[i]);

		// ignore non-strings...
		if (bind->type!=STRING_BIND) {
			continue;
		}

		// variables
		int16_t	year=-1;
		int16_t	month=-1;
		int16_t	day=-1;
		int16_t	hour=-1;
		int16_t	minute=-1;
		int16_t	second=-1;
	
		// parse the date/time
		if (!parseDateTime(bind->value.stringval,ddmm,
						&year,&month,&day,
						&hour,&minute,&second)) {
			continue;
		}

		// decide which format to use
		bool	validdate=(year!=-1 && month!=-1 && day!=-1);
		bool	validtime=(hour!=-1 && minute!=-1 && second!=-1);
		const char	*format=NULL;
		if (validdate && validtime) {
			format=datetimeformat;
		} else if (validdate) {
			format=dateformat;
		} else if (validtime) {
			format=timeformat;
		}

		// attempt to convert the value
		char	*converted=convertDateTime(format,
							year,month,day,
							hour,minute,second);
		if (!converted) {
			continue;
		}

		if (sqlrcon->debugsqltranslation) {
			printf("    %s -> %s\n",
				bind->value.stringval,converted);
		}

		// replace the value with the converted string
		bind->valuesize=charstring::length(converted);
		bind->value.stringval=
			(char *)sqlrcon->bindmappingspool->
					calloc(bind->valuesize+1);
		charstring::copy(bind->value.stringval,converted);
		delete[] converted;
	}

	return true;
}

const char * const *sqltranslator::getShortMonths() {
	return shortmonths;
}

const char * const *sqltranslator::getLongMonths() {
	return longmonths;
}

bool sqltranslator::trimColumnsComparedToStringBinds(xmldomnode *query,
							xmldomnode *rule) {
	debugFunction();
	return true;
}

bool sqltranslator::translateDateTimes(xmldomnode *query, xmldomnode *rule) {
	debugFunction();
	if (sqlrcon->debugsqltranslation) {
		printf("date/time translation:\n");
		printf("    ddmm: %s\n",
			rule->getAttributeValue("ddmm"));
		printf("    datetime: %s\n",
			rule->getAttributeValue("datetime"));
		printf("    date: %s\n",
			rule->getAttributeValue("date"));
		printf("    time: %s\n",
			rule->getAttributeValue("time"));
		printf("  binds:\n");
	}
	if (!translateDateTimesInBindVariables(query,rule)) {
		return false;
	}
	if (sqlrcon->debugsqltranslation) {
		printf("  query:\n");
	}
	if (!translateDateTimesInQuery(query,rule)) {
		return false;
	}
	if (sqlrcon->debugsqltranslation) {
		printf("\n");
	}
	return true;
}

xmldomnode *sqltranslator::newNode(xmldomnode *parentnode, const char *type) {
	xmldomnode	*retval=new xmldomnode(tree,parentnode->getNullNode(),
						TAG_XMLDOMNODETYPE,type,NULL);
	parentnode->appendChild(retval);
	return retval;
}

xmldomnode *sqltranslator::newNode(xmldomnode *parentnode,
				const char *type, const char *value) {
	xmldomnode	*node=newNode(parentnode,type);
	setAttribute(node,sqlparser::_value,value);
	return node;
}

xmldomnode *sqltranslator::newNodeAfter(xmldomnode *parentnode,
						xmldomnode *node,
						const char *type) {

	// find the position after "node"
	uint64_t	position=1;
	for (xmldomnode *child=parentnode->getChild((uint64_t)0);
		!child->isNullNode(); child=child->getNextSibling()) {
		if (child==node) {
			break;
		}
		position++;
	}

	// create a new node and insert it at that position
	xmldomnode	*retval=new xmldomnode(tree,parentnode->getNullNode(),
						TAG_XMLDOMNODETYPE,type,NULL);
	parentnode->insertChild(retval,position);
	return retval;
}

xmldomnode *sqltranslator::newNodeAfter(xmldomnode *parentnode,
						xmldomnode *node,
						const char *type,
						const char *value) {
	xmldomnode	*retval=newNodeAfter(parentnode,node,type);
	setAttribute(retval,sqlparser::_value,value);
	return retval;
}

xmldomnode *sqltranslator::newNodeBefore(xmldomnode *parentnode,
						xmldomnode *node,
						const char *type) {

	// find the position after "node"
	uint64_t	position=0;
	for (xmldomnode *child=parentnode->getChild((uint64_t)0);
		!child->isNullNode(); child=child->getNextSibling()) {
		if (child==node) {
			break;
		}
		position++;
	}

	// create a new node and insert it at that position
	xmldomnode	*retval=new xmldomnode(tree,parentnode->getNullNode(),
						TAG_XMLDOMNODETYPE,type,NULL);
	parentnode->insertChild(retval,position);
	return retval;
}

xmldomnode *sqltranslator::newNodeBefore(xmldomnode *parentnode,
						xmldomnode *node,
						const char *type,
						const char *value) {
	xmldomnode	*retval=newNodeBefore(parentnode,node,type);
	setAttribute(retval,sqlparser::_value,value);
	return retval;
}

void sqltranslator::setAttribute(xmldomnode *node,
				const char *name, const char *value) {
	// FIXME: I shouldn't have to do this.
	// setAttribute should append it automatically
	if (node->getAttribute(name)!=node->getNullNode()) {
		node->setAttributeValue(name,value);
	} else {
		node->appendAttribute(name,value);
	}
}

bool sqltranslator::isString(const char *value) {
	size_t	length=charstring::length(value);
	return ((value[0]=='\'' && value[length-1]=='\'') ||
			(value[0]=='"' && value[length-1]=='"'));
}

bool sqltranslator::tempTablesLocalize(xmldomnode *query, xmldomnode *rule) {

	// get the unique id
	const char	*uniqueid=rule->getAttributeValue("uniqueid");
	if (!uniqueid) {
		uniqueid="";
	}

	// for "create temporary table" queries, find the table name,
	// come up with a session-local name for it and put it in the map...
	mapCreateTemporaryTableName(query,uniqueid);

	// for "create index" queries that refer to temporary tables, find the
	// index name, come up with a session-local name for it and put it in
	// the map...
	mapCreateIndexOnTemporaryTableName(query,uniqueid);

	// for all queries, look for table name nodes or verbatim nodes and
	// apply the mapping
	return replaceTempNames(query);
}

void sqltranslator::mapCreateTemporaryTableName(xmldomnode *node,
						const char *uniqueid) {

	// create...
	node=node->getFirstTagChild(sqlparser::_create);
	if (node->isNullNode()) {
		return;
	}

	// temporary...
	node=node->getFirstTagChild(sqlparser::_temporary);
	if (node->isNullNode()) {
		return;
	}

	// table...
	node=node->getNextTagSibling(sqlparser::_table);
	if (node->isNullNode()) {
		return;
	}

	// table name...
	node=node->getFirstTagChild(sqlparser::_table_name);
	if (node->isNullNode()) {
		return;
	}

	// create a session-local name and put it in the map...
	const char	*oldtablename=
			node->getAttributeValue(sqlparser::_value);
	uint64_t	size=charstring::length(oldtablename)+1;
	char		*oldtablenamecopy=(char *)temptablepool->malloc(size);
	charstring::copy(oldtablenamecopy,oldtablename);
	const char	*newtablename=
			generateTempTableName(oldtablename,uniqueid);
	temptablemap.setData((char *)oldtablenamecopy,(char *)newtablename);
}

const char *sqltranslator::generateTempTableName(const char *oldname,
							const char *uniqueid) {

	uint64_t	pid=process::getProcessId();
	uint64_t	size=charstring::length(oldname)+1+
				charstring::length(uniqueid)+1+
				charstring::integerLength(pid)+1;
	char	*newname=(char *)temptablepool->malloc(size);
	snprintf(newname,size,"%s_%s_%lld",oldname,uniqueid,pid);
	return newname;
}

void sqltranslator::mapCreateIndexOnTemporaryTableName(xmldomnode *node,
							const char *uniqueid) {

	// create...
	node=node->getFirstTagChild(sqlparser::_create);
	if (node->isNullNode()) {
		return;
	}

	// index...
	node=node->getFirstTagChild(sqlparser::_index);
	if (node->isNullNode()) {
		return;
	}

	// index name...
	xmldomnode	*indexnamenode=
			node->getFirstTagChild(sqlparser::_index_name);
	if (indexnamenode->isNullNode()) {
		return;
	}

	// table name...
	node=node->getFirstTagChild(sqlparser::_table_name);
	if (node->isNullNode()) {
		return;
	}

	// if the table name isn't in the temp table map then ignore this query
	char	*newname;
	if (!temptablemap.getData((char *)node->getAttributeValue(
					sqlparser::_value),&newname)) {
		return;
	}

	// create a session-local name and put it in the map...
	const char	*oldindexname=
			indexnamenode->getAttributeValue(sqlparser::_value);
	uint64_t	size=charstring::length(oldindexname)+1;
	char		*oldindexnamecopy=(char *)tempindexpool->malloc(size);
	charstring::copy(oldindexnamecopy,oldindexname);
	const char	*newindexname=
			generateTempTableName(oldindexname,uniqueid);
	tempindexmap.setData((char *)oldindexnamecopy,(char *)newindexname);
}

bool sqltranslator::replaceTempNames(xmldomnode *node) {

	// if the current node is a table name
	// then see if it needs to be replaced
	if (!charstring::compare(node->getName(),
					sqlparser::_table_name) ||
		!charstring::compare(node->getName(),
					sqlparser::_column_name_table)) {

		const char	*newname=NULL;
		const char	*value=node->getAttributeValue(
						sqlparser::_value);

		if (getReplacementTableName(value,&newname)) {
			node->setAttributeValue(sqlparser::_value,newname);
		}
	}

	// if the current node is an index name
	// then see if it needs to be replaced
	if (!charstring::compare(node->getName(),sqlparser::_index_name)) {

		const char	*newname=NULL;
		const char	*value=node->getAttributeValue(
						sqlparser::_value);

		if (getReplacementIndexName(value,&newname)) {
			node->setAttributeValue(sqlparser::_value,newname);
		}
	}

	// run through child nodes too...
	for (node=node->getFirstTagChild();
			!node->isNullNode();
			node=node->getNextTagSibling()) {
		if (!replaceTempNames(node)) {
			return false;
		}
	}
	return true;
}

bool	sqltranslator::getReplacementTableName(const char *oldname,
						const char **newname) {
	return temptablemap.getData((char *)oldname,(char **)newname);
}

bool	sqltranslator::getReplacementIndexName(const char *oldname,
						const char **newname) {
	return tempindexmap.getData((char *)oldname,(char **)newname);
}

bool sqltranslator::locksNoWaitByDefault(xmldomnode *query) {

	// lock query...
	xmldomnode	*locknode=query->getFirstTagChild(sqlparser::_lock);
	if (!locknode->isNullNode()) {

		// see if there's already a nowait argument
		xmldomnode	*nowaitnode=
				locknode->getFirstTagChild(sqlparser::_nowait);
		if (!nowaitnode->isNullNode()) {
			// if there was one then we don't have anything to do
			return true;
		}

		// add a nowait argument
		newNodeAfter(locknode,
				locknode->getChild(locknode->getChildCount()-1),
				sqlparser::_nowait);
		return true;
	}


	// select query...
	xmldomnode	*selectnode=query->getFirstTagChild(sqlparser::_select);
	if (!selectnode->isNullNode()) {

		// see if there's already a nowait argument
		xmldomnode	*nowaitnode=
			selectnode->getFirstTagChild(sqlparser::_nowait);
		if (!nowaitnode->isNullNode()) {
			// if there was one then we don't have anything to do
			return true;
		}

		// find the for update node
		xmldomnode	*forupdatenode=
			selectnode->getFirstTagChild(sqlparser::_for_update);
		if (forupdatenode->isNullNode()) {
			// if there wasn't one then we don't have anything to do
			return true;
		}

		// add a nowait argument
		newNodeAfter(selectnode,forupdatenode,sqlparser::_nowait);
		return true;
	}

	return true;
}
