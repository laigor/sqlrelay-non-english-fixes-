package com.firstworks.sql;

import java.sql.*;

import java.util.Properties;
import java.util.Map;
import java.util.concurrent.Executor;

public class SQLRelayConnection implements Connection {

	public SQLRelayConnection(String host,
					int port,
					String socket,
					String user,
					String password,
					int retrytime,
					int tries) throws SQLException {
	}

	public void	abort(Executor executor) throws SQLException {
	}

	public void	clearWarnings() throws SQLException {
	}

	public void	close() throws SQLException {
	}

	public void	commit() throws SQLException {
	}

	public Array	createArrayOf(String typeName,
					Object[] elements)
					throws SQLException {
		return null;
	}

	public Blob	createBlob() throws SQLException {
		return null;
	}

	public Clob	createClob() throws SQLException {
		return null;
	}

	public NClob	createNClob() throws SQLException {
		return null;
	}

	public SQLXML	createSQLXML() throws SQLException {
		return null;
	}

	public Statement	createStatement() throws SQLException {
		return null;
	}

	public Statement	createStatement(int resultSetType,
						int resultSetConcurrency)
						throws SQLException {
		return null;
	}

	public Statement	createStatement(int resultSetType,
						int resultSetConcurrency,
						int resultSetHoldability)
						throws SQLException {
		return null;
	}

	public Struct	createStruct(String typeName,
						Object[] attributes)
						throws SQLException {
		return null;
	}

	public boolean	getAutoCommit() throws SQLException {
		return false;
	}

	public String	getCatalog() throws SQLException {
		return null;
	}

	public Properties	getClientInfo() throws SQLException {
		return null;
	}

	public String	getClientInfo(String name) throws SQLException {
		return null;
	}

	public int	getHoldability() throws SQLException {
		return 0;
	}

	public DatabaseMetaData	getMetaData() throws SQLException {
		return null;
	}

	public int	getNetworkTimeout() throws SQLException {
		return 0;
	}

	public String	getSchema() throws SQLException {
		return null;
	}

	public int	getTransactionIsolation() throws SQLException {
		return 0;
	}

	public Map<String,Class<?>>	getTypeMap() throws SQLException {
		return null;
	}

	public SQLWarning	getWarnings() throws SQLException {
		return null;
	}

	public boolean	isClosed() throws SQLException {
		return false;
	}

	public boolean	isReadOnly() throws SQLException {
		return false;
	}

	public boolean	isValid(int timeout) throws SQLException {
		return false;
	}

	public String	nativeSQL(String sql) throws SQLException {
		return null;
	}

	public CallableStatement	prepareCall(String sql)
						throws SQLException {
		return null;
	}

	public CallableStatement	prepareCall(String sql,
						int resultSetType,
						int resultSetConcurrency)
						throws SQLException {
		return null;
	}

	public CallableStatement	prepareCall(String sql,
						int resultSetType,
						int resultSetConcurrency,
						int resultSetHoldability)
						throws SQLException {
		return null;
	}

	public PreparedStatement	prepareStatement(String sql)
						throws SQLException {
		return null;
	}

	public PreparedStatement	prepareStatement(String sql,
						int autoGeneratedKeys)
						throws SQLException {
		return null;
	}

	public PreparedStatement	prepareStatement(String sql,
						int[] columnIndexes)
						throws SQLException {
		return null;
	}

	public PreparedStatement	prepareStatement(String sql,
						int resultSetType,
						int resultSetConcurrency)
						throws SQLException {
		return null;
	}

	public PreparedStatement	prepareStatement(String sql,
						int resultSetType,
						int resultSetConcurrency,
						int resultSetHoldability)
						throws SQLException {
		return null;
	}

	public PreparedStatement	prepareStatement(String sql,
						String[] columnNames)
						throws SQLException {
		return null;
	}

	public void	releaseSavepoint(Savepoint savepoint)
						throws SQLException {
	}

	public void	rollback() throws SQLException {
	}

	public void	rollback(Savepoint savepoint) throws SQLException {
	}

	public void	setAutoCommit(boolean autoCommit) throws SQLException {
	}

	public void	setCatalog(String catalog) throws SQLException {
	}

	public void	setClientInfo(Properties properties)
						throws SQLClientInfoException {
	}

	public void	setClientInfo(String name, String value)
						throws SQLClientInfoException {
	}

	public void	setHoldability(int holdability) throws SQLException {
	}

	public void	setNetworkTimeout(Executor executor,
						int milliseconds)
						throws SQLException {
	}

	public void	setReadOnly(boolean readOnly) throws SQLException {
	}

	public Savepoint	setSavepoint() throws SQLException {
		return null;
	}

	public Savepoint	setSavepoint(String name) throws SQLException {
		return null;
	}

	public void	setSchema(String schema) throws SQLException {
	}

	public void	setTransactionIsolation(int level) throws SQLException {
	}

	public void	setTypeMap(Map<String,Class<?>> map)
						throws SQLException {
	}

	public boolean	isWrapperFor(Class<?> iface) throws SQLException {
		return false;
	}

	public <T> T	unwrap(Class<T> iface) throws SQLException {
		return null;
	}
}
