// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Analyzer.h"

namespace TraceServices
{
	class IAnalysisSession;
	class FTasksProvider;

	// deserialises tasks traces and feeds them to FTasksProvider
	class FTasksAnalyzer : public UE::Trace::IAnalyzer
	{
	public:
		FTasksAnalyzer(IAnalysisSession& Session, FTasksProvider& TasksProvider);
		virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override;
		virtual bool OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context) override;

	private:
		enum : uint16
		{
			RouteId_Init,
			RouteId_Created,
			RouteId_Launched,
			RouteId_Scheduled,
			RouteId_SubsequentAdded,
			RouteId_Started,
			RouteId_NestedAdded,
			RouteId_Finished,
			RouteId_Completed,

			RouteId_WaitingStarted,
			RouteId_WaitingFinished,
		};

		IAnalysisSession& Session;
		FTasksProvider& TasksProvider;
	};
}
