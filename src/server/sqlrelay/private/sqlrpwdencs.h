// Copyright (c) 2016  David Muse
// See the file COPYING for more information

	private:
		void	unload();
		void	loadPasswordEncryption(xmldomnode *pwdenc);

		const char	*libexecdir;

		singlylinkedlist< sqlrpwdencplugin * >	llist;
