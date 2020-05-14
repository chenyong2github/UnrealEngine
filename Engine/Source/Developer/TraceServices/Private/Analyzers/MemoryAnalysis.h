// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Analyzer.h"
#include "TraceServices/Containers/Allocators.h"
#include "Model/MemoryPrivate.h"
#include "Containers/UnrealString.h"
#include "Common/PagedArray.h"

namespace Trace
{
	class IAnalysisSession;
}

class FMemoryAnalyzer
	: public Trace::IAnalyzer
{
public:
	FMemoryAnalyzer(Trace::IAnalysisSession& Session);
	~FMemoryAnalyzer();
	virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override;
	virtual bool OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context) override;

private:
	enum : uint16
	{
		RouteId_TagsSpec,
		RouteId_TrackerSpec,
		RouteId_TagValue,
	};

	Trace::FMemoryProvider* Provider;
	Trace::IAnalysisSession& Session;
	uint64 Sample = 0;
};
