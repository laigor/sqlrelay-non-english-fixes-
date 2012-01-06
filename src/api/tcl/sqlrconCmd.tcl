# Initiates a connection to "server" on "port"
# or to the unix "socket" on the local machine
# and authenticates with "user" and "password".
# Failed connections will be retried for 
# "tries" times on interval "retrytime".
# If "tries" is 0 then retries will continue
# forever.  If "retrytime" is 0 then retries
# will be attempted on a default interval.
#
# If the "socket" parameter is neither 
# NULL nor "" then an attempt will be made to 
# connect through it before attempting to 
# connect to "server" on "port".  If it is 
# NULL or "" then no attempt will be made to 
# connect through the socket.
proc sqlrconCmd {server port socket user password retrytime tries} 


# Disconnects and ends the session if
# it hasn't been ended already.
proc sqlrconDelete {} 



# Sets the server connect timeout in seconds
# and milliseconds.  Setting either parameter
# to -1 disables the timeout.
proc setTimeout {timeoutsec timeoutusec} 

# Ends the session.
proc endSession {} 

# Disconnects this connection from the current
# session but leaves the session open so 
# that another connection can connect to it 
# using resumeSession {}.
proc suspendSession {} 

# Returns the inet port that the connection is 
# communicating over. This parameter may be 
# passed to another connection for use in
# the resumeSession {} method.
# Note: The value this method returns is only
# valid after a call to suspendSession {}.
proc getConnectionPort {} 

# Returns the unix socket that the connection 
# is communicating over. This parameter may be 
# passed to another connection for use in
# the resumeSession {} method.
# Note: The value this method returns is only
# valid after a call to suspendSession {}.
proc getConnectionSocket {} 

# Resumes a session previously left open 
# using suspendSession {}.
# Returns true on success and false on failure.
proc resumeSession {port socket} 



# Returns true if the database is up and false
# if it's down.
proc ping {} 

# Returns the type of database: 
# oracle8 postgresql mysql etc.
proc identify {} 

# Returns the version of the database
proc dbVersion {} 

# Returns the version of the sqlrelay server software.
proc serverVersion {} 

# Returns the version of the sqlrelay client software.
proc clientVersion {} 

# Returns a string representing the format
# of the bind variables used in the db.
proc bindFormat {} 



# Sets the current database/schema to "database"
proc selectDatabase {database} 

# Returns the database/schema that is currently in use.
proc getCurrentDatabase {} 



# Returns the value of the autoincrement
# column for the last insert
proc getLastInsertId {} 



# Instructs the database to perform a commit
# after every successful query.
proc autoCommitOn {} 

# Instructs the database to wait for the 
# client to tell it when to commit.
proc autoCommitOff {} 



# Issues a commit.  Returns 1 if the commit
# succeeded 0 if it failed.
proc commit {} 

# Issues a rollback.  Returns 1 if the rollback
# succeeded 0 if it failed.
proc rollback {} 



# If an operation failed and generated an
# error the error message is available here.
# If there is no error then this method 
# returns NULL.
proc errorMessage {} 



# Causes verbose debugging information to be 
# sent to standard output.  Another way to do
# this is to start a query with "-- debug\n".
# Another way is to set the environment variable
# SQLRDEBUG to "ON"
proc debugOn {} 

# Turns debugging off.
proc debugOff {} 

# Returns false if debugging is off and true
# if debugging is on.
proc getDebug {} 
