// Copyright Epic Games, Inc. All Rights Reserved.

#include "MemoryModule.h"
#include "Analyzers/AllocationsAnalysis.h"
#include "Analyzers/MemoryAnalysis.h"
#include "Model/AllocationsProvider.h"
#include "TraceServices/Model/AnalysisSession.h"

#if defined(UE_USE_ALLOCATIONS_PROVIDER)

static void TEMP_TestAllocationsProviderImpl(double TimeA, double TimeB)
{
	FAllocationsProvider Provider;

	// 1. Query is started in response to some form of user interaction.
	IAllocationsProvider::QueryHandle Query;
	{
		Query = Provider.StartQuery(TimeA, TimeB, IAllocationsProvider::ECrosses::Xor);
	}



	// 2. At regular intervals the query is polled for results.
	while (true)
	{
		IAllocationsProvider::FQueryStatus Status = Provider.PollQuery(Query);
		if (Status.Status <= IAllocationsProvider::EQueryStatus::Done)
		{
			break;
		}

		// Multiple 'pages' of results will be returned. No guarantees are made
		// about the order of pages or the allocations they report.
		IAllocationsProvider::QueryResult Result = Status.NextResult();
		while (Result.IsValid())
		{
			for (uint32 i = 0, n = Result->Num(); i < n; ++i)
			{
				const IAllocationsProvider::FAllocation* Allocation = Result->Get(i);

				uint64 BacktraceId = Allocation->GetBacktraceId();
				/*
				auto Backtrace = BacktraceProvider.Get(BacktraceId);
				for (auto& RetAddress : Backtrace)
				{
					auto Addr = Allocation->GetAddress();
					auto Size = Allocation->GetSize();
					auto LlmTag = Allocation->GetTag();

					... report to user (e.g. build treeview)
				}
				*/
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

static void TEMP_TestAllocationsProvider()
{
	double TimeA = 3.0;
	double TimeB = 11.0;
	TEMP_TestAllocationsProviderImpl(TimeA, TimeB);
}

#endif // UE_USE_ALLOCATIONS_PROVIDER



namespace Trace
{

static const FName MemoryModuleName("TraceModule_Memory");

void FMemoryModule::GetModuleInfo(FModuleInfo& OutModuleInfo)
{
	OutModuleInfo.Name = MemoryModuleName;
	OutModuleInfo.DisplayName = TEXT("Memory");
}

void FMemoryModule::OnAnalysisBegin(IAnalysisSession& Session)
{
#if defined(UE_USE_ALLOCATIONS_PROVIDER)
	FAllocationsProvider* AllocationsProvider = new FAllocationsProvider();
	Session.AddProvider(AllocationsProvider->GetName(), AllocationsProvider);
	//Session.AddAnalyzer(new FAllocationsAnalyzer(Session));

	TEMP_TestAllocationsProvider();
#endif // UE_USE_ALLOCATIONS_PROVIDER

	Session.AddAnalyzer(new FMemoryAnalyzer(Session));
}

void FMemoryModule::GetLoggers(TArray<const TCHAR *>& OutLoggers)
{
	OutLoggers.Add(TEXT("Memory"));
}

}
