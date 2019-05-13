// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Trace.h"
#include "Trace/Analyzer.h"
#include "Templates/SharedPointer.h"

namespace Trace
{
	class FAnalysisSession;
	class FLogProvider;
}

class FLogTraceAnalyzer
	: public Trace::IAnalyzer
{
public:
	FLogTraceAnalyzer(TSharedRef<Trace::FAnalysisSession> Session);
	virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override;
	virtual void OnEvent(uint16 RouteId, const FOnEventContext& Context) override;
	virtual void OnAnalysisEnd() override {};

private:
	enum : uint16
	{
		RouteId_LogCategory,
		RouteId_LogMessageSpec,
		RouteId_LogMessage,
	};

	TSharedRef<Trace::FAnalysisSession> Session;
	TSharedRef<Trace::FLogProvider> LogProvider;
};
