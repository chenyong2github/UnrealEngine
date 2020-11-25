// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Analyzer.h"

namespace TraceServices
{

class IAnalysisSession;

class FAllocationsProvider;

class FAllocationsAnalyzer : public UE::Trace::IAnalyzer
{
private:
	enum : uint16
	{
		RouteId_Init,
		RouteId_CoreAdd,
		RouteId_CoreRemove,
		RouteId_Alloc,
		RouteId_Realloc,
		RouteId_Free,
	};

public:
	FAllocationsAnalyzer(IAnalysisSession& Session, FAllocationsProvider& AllocationsProvider);
	~FAllocationsAnalyzer();
	virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override;
	virtual void OnAnalysisEnd() override;
	virtual bool OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context) override;
	double GetCurrentTime() const;

private:
	IAnalysisSession& Session;
	FAllocationsProvider& AllocationsProvider;
	uint64 EventCount = 0; // debug
};

} // namespace TraceServices
