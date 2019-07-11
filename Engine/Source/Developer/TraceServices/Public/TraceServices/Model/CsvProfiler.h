// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/Model/AnalysisSession.h"
#include "Templates/Function.h"

namespace Trace
{

class IUntypedTable;

struct FCaptureInfo
{
	const TMap<const TCHAR*, const TCHAR*>& Metadata;
	const TCHAR* Filename = nullptr;
	uint32 Id = uint32(-1);
	uint32 FrameCount = 0;
};

class ICsvProfilerProvider
	: public IProvider
{
public:
	virtual void EnumerateCaptures(TFunctionRef<void(const FCaptureInfo&)> Callback) const = 0;
	virtual const IUntypedTable& GetTable(uint32 CaptureId) const = 0;
};

const ICsvProfilerProvider* ReadCsvProfilerProvider(const IAnalysisSession& Session);

}