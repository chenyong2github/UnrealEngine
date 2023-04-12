// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/StringFwd.h"
#include "Templates/SharedPointer.h"

class FChaosVDEngine;
class FChaosVDTraceModule;

namespace TraceServices { class IAnalysisSession; }

/** Manager class used by Chaos VD to interact/control UE Trace systems */
class FChaosVDTraceManager
{
public:
	FChaosVDTraceManager();
	~FChaosVDTraceManager();

	FString LoadTraceFile(const FString& InTraceFilename);

	TSharedPtr<const TraceServices::IAnalysisSession> GetSession(const FString& InSessionName);

private:
	
	void ResetSession(const FString& InSessionName);

	/** The trace analysis session. */
	TMap<FString, TSharedPtr<const TraceServices::IAnalysisSession>> AnalysisSessionByName;

	TSharedPtr<FChaosVDTraceModule> ChaosVDTraceModule;
};
