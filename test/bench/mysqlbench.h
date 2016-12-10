// Copyright (c) 2014  David Muse
// See the file COPYING for more information
#ifndef MYSQL_BENCH_H
#define MYSQL_BENCH_H

#include "bench.h"

class mysqlbenchmarks : public benchmarks {
	public:
		mysqlbenchmarks(const char *connectstring,
					const char *db,
					uint64_t queries,
					uint64_t rows,
					uint32_t cols,
					uint32_t colsize,
					uint16_t samples,
					uint64_t rsbs,
					bool debug);
};

#endif
