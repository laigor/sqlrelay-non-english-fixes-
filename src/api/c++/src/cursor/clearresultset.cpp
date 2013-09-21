// Copyright (c) 1999-2001  David Muse
// See the file COPYING for more information

#include <config.h>
#include <sqlrelay/sqlrclient.h>
#include <defines.h>
#include <datatypes.h>

void sqlrcursor::clearResultSet() {

	clearCacheDest();
	clearCacheSource();
	clearError();

	// columns is cleared after rows because colcount is used in 
	// clearRows() and set to 0 in clearColumns()
	clearRows();
	clearColumns();

	// clear row counters, since fetchRowIntoBuffer() and clearResultSet()
	// are the only methods that call clearRows() and fetchRowIntoBuffer()
	// needs these values not to be cleared, we'll clear them here...
	firstrowindex=0;
	previousrowcount=rowcount;
	rowcount=0;
	actualrows=0;
	affectedrows=0;
	endofresultset=true;
	suspendresultsetsent=0;
}

void sqlrcursor::clearError() {
	delete[] error;
	error=NULL;
	errorno=0;
	if (sqlrc) {
		sqlrc->clearError();
	}
}

void sqlrcursor::clearRows() {

	// delete data in rows for long datatypes
	uint32_t	rowbuffercount=rowcount-firstrowindex;
	for (uint32_t i=0; i<rowbuffercount; i++) {
	        for (uint32_t j=0; j<colcount; j++) {
			if (getColumnInternal(j)->longdatatype) {
				// don't delete null columns
				// (who's lengths will be 0)
				if (getFieldLengthInternal(i,j)) {
					delete[] getFieldInternal(i,j);
				}
			}
		}
	}

	// delete linked list storing extra result set fields
	row	*currentrow;
	if (firstextrarow) {
		currentrow=firstextrarow;
		while (currentrow) {
			firstextrarow=currentrow->next;
			delete currentrow;
			currentrow=firstextrarow;
		}
		firstextrarow=NULL;
	}
	currentrow=NULL;

	// delete array pointing to linked list items
	delete[] extrarows;
	extrarows=NULL;

	// delete arrays of fields and field lengths
	if (fields) {
		for (uint32_t i=0; i<rowbuffercount; i++) {
			delete[] fields[i];
		}
		delete[] fields;
		fields=NULL;
	}
	if (fieldlengths) {
		for (uint32_t i=0; i<rowbuffercount; i++) {
			delete[] fieldlengths[i];
		}
		delete[] fieldlengths;
		fieldlengths=NULL;
	}

	// reset the row storage pool
	rowstorage->deallocate();
}

void sqlrcursor::clearColumns() {

	// delete the column type strings (if necessary)
	if (sentcolumninfo==SEND_COLUMN_INFO &&
				columntypeformat!=COLUMN_TYPE_IDS) {
		for (uint32_t i=0; i<colcount; i++) {
			delete[] getColumnInternal(i)->typestring;
		}
	}

	// reset the column storage pool
	colstorage->deallocate();

	// reset the column count
	previouscolcount=colcount;
	colcount=0;

	// delete array pointing to each column name
	delete[] columnnamearray;
	columnnamearray=NULL;
}
