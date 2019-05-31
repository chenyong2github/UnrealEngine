// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TraceServices/Model/LoadTimeProfiler.h"
#include "Model/LoadTimeProfilerPrivate.h"
#include "AnalysisServicePrivate.h"
#include "Common/TimelineStatistics.h"

namespace Trace
{

FLoadTimeProfilerProvider::FLoadTimeProfilerProvider(IAnalysisSession& InSession)
	: Session(InSession)
	, ClassInfos(Session.GetLinearAllocator(), 4096)
	, Packages(Session.GetLinearAllocator(), 4096)
	, Exports(Session.GetLinearAllocator(), 4090)
	, MainThreadCpuTimeline(MakeShared<CpuTimelineInternal>(Session.GetLinearAllocator()))
	, AsyncLoadingThreadCpuTimeline(MakeShared<CpuTimelineInternal>(Session.GetLinearAllocator()))
{
	
}

void FLoadTimeProfilerProvider::EnumeratePackages(TFunctionRef<void(const FPackageInfo&)> Callback) const
{
	Session.ReadAccessCheck();

	auto Iterator = Packages.GetIteratorFromItem(0);
	const FPackageInfo* Package = Iterator.GetCurrentItem();
	while (Package)
	{
		Callback(*Package);
		Package = Iterator.NextItem();
	}
}

void FLoadTimeProfilerProvider::ReadMainThreadCpuTimeline(TFunctionRef<void(const CpuTimeline &)> Callback) const
{
	Session.ReadAccessCheck();
	Callback(*MainThreadCpuTimeline);
}

void FLoadTimeProfilerProvider::ReadAsyncLoadingThreadCpuTimeline(TFunctionRef<void(const CpuTimeline &)> Callback) const
{
	Session.ReadAccessCheck();
	Callback(*AsyncLoadingThreadCpuTimeline);
}

ITable<FLoadTimeProfilerAggregatedStats>* FLoadTimeProfilerProvider::CreateEventAggregation(double IntervalStart, double IntervalEnd) const
{
	TArray<const CpuTimelineInternal*> Timelines;
	Timelines.Add(&MainThreadCpuTimeline.Get());
	Timelines.Add(&AsyncLoadingThreadCpuTimeline.Get());

	auto BucketMapper = [](const FLoadTimeProfilerCpuEvent& Event)
	{
		return Event.PackageEventType;
	};
	TMap<ELoadTimeProfilerPackageEventType, FAggregatedTimingStats> Aggregation;
	FTimelineStatistics::CreateAggregation(Timelines, BucketMapper, IntervalStart, IntervalEnd, Aggregation);
	TTable<FAggregatedStatsTableLayout>* Table = new TTable<FAggregatedStatsTableLayout>();
	for (const auto& KV : Aggregation)
	{
		FLoadTimeProfilerAggregatedStats& Row = Table->AddRow();
		switch (KV.Key)
		{
		case LoadTimeProfilerPackageEventType_CreateLinker:
			Row.Name = TEXT("CreateLinker");
			break;
		case LoadTimeProfilerPackageEventType_FinishLinker:
			Row.Name = TEXT("FinishLinker");
			break;
		case LoadTimeProfilerPackageEventType_StartImportPackages:
			Row.Name = TEXT("StartImportPackages");
			break;
		case LoadTimeProfilerPackageEventType_SetupImports:
			Row.Name = TEXT("SetupImports");
			break;
		case LoadTimeProfilerPackageEventType_SetupExports:
			Row.Name = TEXT("SetupExports");
			break;
		case LoadTimeProfilerPackageEventType_ProcessImportsAndExports:
			Row.Name = TEXT("ProcessImportsAndExports");
			break;
		case LoadTimeProfilerPackageEventType_ExportsDone:
			Row.Name = TEXT("ExportsDone");
			break;
		case LoadTimeProfilerPackageEventType_PostLoadWait:
			Row.Name = TEXT("PostLoadWait");
			break;
		case LoadTimeProfilerPackageEventType_StartPostLoad:
			Row.Name = TEXT("StartPostLoad");
			break;
		case LoadTimeProfilerPackageEventType_Tick:
			Row.Name = TEXT("Tick");
			break;
		case LoadTimeProfilerPackageEventType_DeferredPostLoad:
			Row.Name = TEXT("DeferredPostLoad");
			break;
		case LoadTimeProfilerPackageEventType_Finish:
			Row.Name = TEXT("Finish");
			break;
		}
		const FAggregatedTimingStats& Stats = KV.Value;
		Row.Count = Stats.InstanceCount;
		Row.Total = Stats.TotalExclusiveTime;
		Row.Min = Stats.MinExclusiveTime;
		Row.Max = Stats.MaxExclusiveTime;
		Row.Average = Stats.AverageExclusiveTime;
		Row.Median = Stats.MedianExclusiveTime;
	}
	return Table;
}

ITable<FLoadTimeProfilerAggregatedStats>* FLoadTimeProfilerProvider::CreateObjectTypeAggregation(double IntervalStart, double IntervalEnd) const
{
	TArray<const CpuTimelineInternal*> Timelines;
	Timelines.Add(&MainThreadCpuTimeline.Get());
	Timelines.Add(&AsyncLoadingThreadCpuTimeline.Get());

	auto BucketMapper = [](const FLoadTimeProfilerCpuEvent& Event) -> const FClassInfo*
	{
		return Event.Export ? Event.Export->Class : nullptr;
	};
	TMap<const Trace::FClassInfo*, FAggregatedTimingStats> Aggregation;
	FTimelineStatistics::CreateAggregation(Timelines, BucketMapper, IntervalStart, IntervalEnd, Aggregation);
	TTable<FAggregatedStatsTableLayout>* Table = new TTable<FAggregatedStatsTableLayout>();
	for (const auto& KV : Aggregation)
	{
		const FClassInfo* ClassInfo = KV.Key;
		if (ClassInfo)
		{
			FLoadTimeProfilerAggregatedStats& Row = Table->AddRow();
			const FAggregatedTimingStats& Stats = KV.Value;
			Row.Name = ClassInfo->Name;
			Row.Count = Stats.InstanceCount;
			Row.Total = Stats.TotalExclusiveTime;
			Row.Min = Stats.MinExclusiveTime;
			Row.Max = Stats.MaxExclusiveTime;
			Row.Average = Stats.AverageExclusiveTime;
			Row.Median = Stats.MedianExclusiveTime;
		}
	}
	return Table;
}

const Trace::FClassInfo& FLoadTimeProfilerProvider::AddClassInfo(const TCHAR* ClassName)
{
	Session.WriteAccessCheck();

	FClassInfo& ClassInfo = ClassInfos.PushBack();
	ClassInfo.Name = Session.StoreString(ClassName);
	return ClassInfo;
}

Trace::FPackageInfo& FLoadTimeProfilerProvider::CreatePackage(const TCHAR* PackageName)
{
	Session.WriteAccessCheck();

	uint32 PackageId = Packages.Num();
	FPackageInfo& Package = Packages.PushBack();
	Package.Id = PackageId;
	Package.Name = Session.StoreString(PackageName);
	return Package;
}

Trace::FPackageExportInfo& FLoadTimeProfilerProvider::CreateExport()
{
	Session.WriteAccessCheck();

	uint32 ExportId = Exports.Num();
	FPackageExportInfo& Export = Exports.PushBack();
	Export.Id = ExportId;
	return Export;
}

}