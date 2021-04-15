// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "TraceServices/Model/Callstack.h"
#include "TraceServices/Model/Modules.h"

/////////////////////////////////////////////////////////////////////
enum EStackFrameFormatFlags : uint8
{
	Module			= 1 << 0,
	FileAndLine		= 1 << 1
};

/////////////////////////////////////////////////////////////////////
inline void FormatStackFrame(const TraceServices::FStackFrame& Frame, FStringBuilderBase& OutString, uint8 FormatFlags)
{
	using namespace TraceServices;
	const ESymbolQueryResult Result = Frame.Symbol->GetResult();
	switch(Result)
	{
		case ESymbolQueryResult::OK:
			if (FormatFlags & (uint8)EStackFrameFormatFlags::Module)
			{
				OutString.Appendf(TEXT("%s!"), Frame.Symbol->Module);
			}
			OutString.Append(Frame.Symbol->Name);
			if (FormatFlags & (uint8)EStackFrameFormatFlags::FileAndLine)
			{
				OutString.Appendf(TEXT(" %s(%d)"), Frame.Symbol->File, Frame.Symbol->Line);
			}
			break;
		case ESymbolQueryResult::Mismatch:
		case ESymbolQueryResult::NotFound:
		case ESymbolQueryResult::NotLoaded:
			if (FormatFlags & (uint8)EStackFrameFormatFlags::Module)
			{
				OutString.Appendf(TEXT("%s!0x%08x (%s)"), Frame.Symbol->Module, Frame.Addr, QueryResultToString((Result)));
			}
			else
			{
				OutString.Appendf(TEXT("0x%08x (%s)"), Frame.Symbol->Name, Frame.Addr, QueryResultToString((Result)));
			}
			break;
		case ESymbolQueryResult::Pending:
		default:
			OutString.Append(QueryResultToString(Result));
			break;
	}
}
