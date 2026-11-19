// date_node.h: the script Date object. Holds one FILETIME and answers the
// usual JavaScript getters from it, local or UTC. Bodies in
// shared/date_node.cpp, binary notes in docs/decomp/Date.md.

#pragma once

class CDateObject : public CObject
{
	DECLARE_NODE(CDateObject, CObject)
	DECLARE_NODE_FUNCTIONS()
public:
	CDateObject();

	// Local time.
	int getFullYear();
	int getYear();
	int getMonth();
	int getDate();
	int getDay();
	int getHours();
	int getMinutes();
	int getSeconds();
	int getMilliseconds();

	// Same fields in UTC.
	int getUTCFullYear();
	int getUTCMonth();
	int getUTCDate();
	int getUTCDay();
	int getUTCHours();
	int getUTCMinutes();
	int getUTCSeconds();
	int getUTCMilliseconds();

	int isLeapYear(int year);
	int getDaysInMonth(int month, int year);

	CStrObject* toGMTString();
	CStrObject* toUTCString();
	CStrObject* toLocaleString();

	void SetSystemClock();

	FILETIME m_time;	// 100 ns ticks since 1601
};
