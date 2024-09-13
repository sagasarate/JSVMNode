#include "stdafx.h"


void CConsoleLogPrinter::PrintLogDirect(int Level, LPCTSTR Tag, LPCTSTR Msg)
{
	if (Tag)
	{
		printf("[");
		printf(Tag);
		printf("]");
	}
	printf(Msg);
	printf("\r\n");
}
void CConsoleLogPrinter::PrintLogVL(int Level, LPCTSTR Tag, LPCTSTR Format, va_list vl)
{
	if (Tag)
	{
		printf("[");
		printf(Tag);
		printf("]");
	}
	TCHAR MsgBuff[4096];
	_vstprintf_s(MsgBuff, 4096, Format, vl);
	printf(MsgBuff);
	printf("\r\n");
}