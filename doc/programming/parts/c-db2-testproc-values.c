sqlrcur_prepareQuery(cur,"call testproc(?,?,?,?,?,?)");
sqlrcur_inputBindLong(cur,"1",1);
sqlrcur_inputBindDouble(cur,"2",1.1,2,1);
sqlrcur_inputBindString(cur,"3","hello");
sqlrcur_defineOutputBindInteger(cur,"4");
sqlrcur_defineOutputBindDouble(cur,"5");
sqlrcur_defineOutputBindString-(cur,"6",25);
sqlrcur_executeQuery(cur);
int64_t out1=sqlrcur_getOutputBindInteger(cur,"4");
double  out2=sqlrcur_getOutputBindDouble(cur,"5");
char    *out3=sqlrcur_getOutputBindString(cur,"6");
