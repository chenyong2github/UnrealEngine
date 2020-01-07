// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/Model/AnalysisSession.h"
#include "Templates/Function.h"

namespace Trace
{

struct FThreadInfo
{
	uint32 Id;
	const TCHAR* Name;
	const TCHAR* GroupName;
};

class IThreadProvider
	: public IProvider
{
public:
	virtual ~IThreadProvider() = default;
	virtual uint64 GetModCount() const = 0;
	virtual void EnumerateThreads(TFunctionRef<void(const FThreadInfo&)> Callback) const = 0;
	virtual const TCHAR* GetThreadName(uint32 ThreadId) const = 0;
};

TRACESERVICES_API const IThreadProvider& ReadThreadProvider(const IAnalysisSession& Session);

}