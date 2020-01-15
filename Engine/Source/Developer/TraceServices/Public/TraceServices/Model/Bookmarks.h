// Copyright Epic Games, Inc. All Rights Reserved.

#pragma  once

#include "TraceServices/Model/AnalysisSession.h"
#include "Templates/Function.h"

namespace Trace
{

struct FBookmark
{
	double Time;
	const TCHAR* Text;
};

class IBookmarkProvider
	: public IProvider
{
public:
	virtual ~IBookmarkProvider() = default;
	virtual uint64 GetBookmarkCount() const = 0;
	virtual void EnumerateBookmarks(double IntervalStart, double IntervalEnd, TFunctionRef<void(const FBookmark&)> Callback) const = 0;
};

TRACESERVICES_API const IBookmarkProvider& ReadBookmarkProvider(const IAnalysisSession& Session);

}