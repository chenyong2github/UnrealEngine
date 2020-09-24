// Copyright Epic Games, Inc. All Rights Reserved.

#include "AllocationsAnalysis.h"

#if 0

////////////////////////////////////////////////////////////////////////////////
enum
{
	RouteId_Init,
	RouteId_Alloc,
	RouteId_Realloc,
	RouteId_Free,
};

////////////////////////////////////////////////////////////////////////////////
FAllocationsAnalyzer::FAllocationsAnalyzer(Trace::IAnalysisSession& Session)
{
}

////////////////////////////////////////////////////////////////////////////////
void FAllocationsAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
	/*
	FInterfaceBuilder& Builder = Context.InterfaceBuilder;
	Builder.RouteEvent(RouteId_Init,    "Memory", "Init");
	Builder.RouteEvent(RouteId_Alloc,   "Memory", "Alloc");
	Builder.RouteEvent(RouteId_Realloc, "Memory", "Realloc");
	Builder.RouteEvent(RouteId_Free,    "Memory", "Free");
	*/
}

#endif // 0
