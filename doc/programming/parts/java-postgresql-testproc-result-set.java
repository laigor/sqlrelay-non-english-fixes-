cur.sendQuery("select * from testfunc() as (testint int, testfloat float, testchar char(40))");
String  field00=cur.getField(0,0);
String  field01=cur.getField(0,1);
String  field02=cur.getField(0,2);
String  field10=cur.getField(1,0);
String  field11=cur.getField(1,1);
String  field12=cur.getField(1,2);
