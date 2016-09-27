#include <sqlrelay/sqlrclientwrapper.h>
#include <stdio.h>

main() {

        int     done=0;
        int     row=0;
        int     col;
        char    *field;

        sqlrcon      con=sqlrcon_alloc("sqlrserver",9000,"/tmp/test.socket","user","password",0,1);
        sqlrcur      cur=sqlrcur_alloc(con);

        sqlrcur_setResultSetBufferSize(cur,5);

        sqlrcur_sendQuery(cur,"select * from my_table");

        while (!done) {
                for (col=0; col<sqlrcur_colCount(cur); col++) {
                        if (field=sqlrcur_getFieldByIndex(cur,row,col)) {
                                printf("%s,",field);
                        } else {
                                done=1;
                        }
                }
                printf("\n");
                row++;
        }

        sqlrcur_sendQuery(cur,"select * from my_other_table");

        ... process this query's result set in chunks also ...

        sqlrcur_setResultSetBufferSize(cur,0);

        sqlrcur_sendQuery(cur,"select * from my_third_table");

        ... process this query's result set all at once ...

        sqlrcon_endSession(con);

        sqlrcur_free(cur);
        sqlrcon_free(con);
}
