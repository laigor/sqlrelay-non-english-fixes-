sqlrcur_prepareQuery(cur,"execute procedure testproc ?, ?, ?");
sqlrcur_inputBindLong(cur,"1",1);
sqlrcur_inputBindDouble(cur,"2",1.1,2,1);
sqlrcur_inputBindString(cur,"3","hello");
sqlrcur_defineOutputBindInteger(cur,"1");
sqlrcur_executeQuery(cur);
int64_t    result=sqlrcur_getOutputBindInteger(cur,"1");
