// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Analyzer.h"
#include "TraceServices/Containers/Allocators.h"
#include "Model/DiagnosticsPrivate.h"
#include "Containers/UnrealString.h"

namespace Trace
{
	class IAnalysisSession;
}

class FDiagnosticsAnalyzer
	: public Trace::IAnalyzer
{
public:
	FDiagnosticsAnalyzer(Trace::IAnalysisSession& Session);
	~FDiagnosticsAnalyzer();
	virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override;
	virtual bool OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context) override;

private:
	enum : uint16
	{
		RouteId_Session,
		RouteId_Session2,
	};
	Trace::FDiagnosticsProvider* Provider;
	Trace::IAnalysisSession& Session;
};
