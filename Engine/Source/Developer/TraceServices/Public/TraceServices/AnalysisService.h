// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"
#include "Delegates/Delegate.h"
#include "Trace/DataStream.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/Model/Log.h"
#include "TraceServices/Model/Bookmarks.h"
#include "TraceServices/Model/Frames.h"
#include "TraceServices/Model/Threads.h"
#include "TraceServices/Model/TimingProfiler.h"
#include "TraceServices/Model/LoadTimeProfiler.h"
#include "TraceServices/Model/Counters.h"

namespace Trace
{

class IAnalysisService
{
public:
	virtual TSharedPtr<const IAnalysisSession> Analyze(const TCHAR* SessionUri) = 0;
	virtual TSharedPtr<const IAnalysisSession> StartAnalysis(const TCHAR* SessionUri) = 0;
	virtual TSharedPtr<const IAnalysisSession> StartAnalysis(const TCHAR* SessionName, TUniquePtr<Trace::IInDataStream>&& DataStream) = 0;

	DECLARE_EVENT_OneParam(IAnalysisService, FAnalysisStartedEvent, TSharedRef<const IAnalysisSession>)
	virtual FAnalysisStartedEvent& OnAnalysisStarted() = 0;

	DECLARE_EVENT_OneParam(IAnalysisService, FAnalysisFinishedEvent, TSharedRef<const IAnalysisSession>)
	virtual FAnalysisFinishedEvent& OnAnalysisFinished() = 0;
};

}
