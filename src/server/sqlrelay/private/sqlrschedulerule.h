// Copyright (c) 2016  David Muse
// See the file COPYING for more information

	private:
		void	init(bool allow,
				const char *years,
				const char *months,
				const char *daysofmonth,
				const char *daysofweek,
				const char *dayparts);
		void	splitTimePart(
				linkedlist< sqlrscheduleperiod * > *periods,
				const char *timepartlist);
		void	splitDayParts(const char *daypartlist);
		bool	inPeriods(
				linkedlist< sqlrscheduleperiod * > *periods,
				int32_t timepart);
		bool	inDayParts(int32_t hour, int32_t minute);

		bool	allow;

		linkedlist< sqlrscheduleperiod * >	years;
		linkedlist< sqlrscheduleperiod * >	months;
		linkedlist< sqlrscheduleperiod * >	daysofmonth;
		linkedlist< sqlrscheduleperiod * >	daysofweek;
		linkedlist< sqlrscheduledaypart * >	dayparts;
