#pragma once

class CConsoleLogPrinter :
    public ILogPrinter
{
	virtual void PrintLogDirect(int Level, LPCTSTR Tag, LPCTSTR Msg);
	virtual void PrintLogVL(int Level, LPCTSTR Tag, LPCTSTR Format, va_list vl);
};

