// Copyright Epic Games, Inc. All Rights Reserved.

#include "MemoryModule.h"
#include "Analyzers/AllocationsAnalysis.h"
#include "Analyzers/CallstacksAnalysis.h"
#include "Analyzers/MemoryAnalysis.h"
#include "Model/AllocationsProvider.h"
#include "Model/CallstacksProvider.h"
#include "TraceServices/Model/AnalysisSession.h"

namespace TraceServices
{

static void TEMP_TestAllocationsProviderImpl(ILinearAllocator& Allocator, double TimeA, double TimeB)
{
	FAllocationsProvider Provider(Allocator);

	// 1. Query is started in response to some form of user interaction.
	IAllocationsProvider::FQueryHandle Query;
	{
		const IAllocationsProvider::FQueryParams Params = { IAllocationsProvider::EQueryRule::aABf, TimeA, TimeB, 0.0, 0.0 };
		Query = Provider.StartQuery(Params);
	}

	// 2. At regular intervals the query is polled for results.
	while (true)
	{
		const IAllocationsProvider::FQueryStatus Status = Provider.PollQuery(Query);
		if (Status.Status <= IAllocationsProvider::EQueryStatus::Done)
		{
			break;
		}

		// Multiple 'pages' of results will be returned. No guarantees are made
		// about the order of pages or the allocations they report.
		IAllocationsProvider::FQueryResult Result = Status.NextResult();
		while (Result.IsValid())
		{
			for (uint32 i = 0, n = Result->Num(); i < n; ++i)
			{
				const IAllocationsProvider::FAllocation* Allocation = Result->Get(i);

				auto Addr = Allocation->GetAddress();

				auto Size = Allocation->GetSize();
				auto Alignment = Allocation->GetAlignment();
				auto Waste = Allocation->GetWaste();

				uint64 BacktraceId = Allocation->GetBacktraceId();
				/*
				auto Backtrace = BacktraceProvider.Get(BacktraceId);
				for (auto& RetAddress : Backtrace)
				{
				}
				*/

				auto Tag = Allocation->GetTag();

				//... report to user (e.g.build treeview)
			}

			Result = Status.NextResult();
		}
	}

	// 3a. Queries can be cancelled.
	if (/*HasUserPressedCance()*/ false)
	{
		Provider.CancelQuery(Query);
	}

	// 3b. Queries that are polled to completion... */
	/* Queries will automatically clean themselves up */
}

static void TEMP_TestAllocationsProvider(ILinearAllocator& Allocator)
{
	double TimeA = 3.0;
	double TimeB = 11.0;
	TEMP_TestAllocationsProviderImpl(Allocator, TimeA, TimeB);
}

static const FName MemoryModuleName("TraceModule_Memory");

void FMemoryModule::GetModuleInfo(FModuleInfo& OutModuleInfo)
{
	OutModuleInfo.Name = MemoryModuleName;
	OutModuleInfo.DisplayName = TEXT("Memory");
}

void FMemoryModule::OnAnalysisBegin(IAnalysisSession& Session)
{
	FAllocationsProvider* AllocationsProvider = new FAllocationsProvider(Session.GetLinearAllocator());
	Session.AddProvider(AllocationsProvider->GetName(), AllocationsProvider);
	Session.AddAnalyzer(new FAllocationsAnalyzer(Session, *AllocationsProvider));

	//TEMP_TestAllocationsProvider(Session.GetLinearAllocator());

	FCallstacksProvider* CallstacksProvider = new FCallstacksProvider(Session);
	Session.AddProvider(CallstacksProvider->GetName(), CallstacksProvider);
	Session.AddAnalyzer(new FCallstacksAnalyzer(Session, CallstacksProvider));

	Session.AddAnalyzer(new FMemoryAnalyzer(Session));
}

void FMemoryModule::GetLoggers(TArray<const TCHAR *>& OutLoggers)
{
	OutLoggers.Add(TEXT("Memory"));
}

} // namespace TraceServices
