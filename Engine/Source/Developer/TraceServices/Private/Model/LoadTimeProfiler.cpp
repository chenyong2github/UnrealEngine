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
	, Requests(Session.GetLinearAllocator(), 4096)
	, Packages(Session.GetLinearAllocator(), 4096)
	, Exports(Session.GetLinearAllocator(), 4096)
	, MainThreadCpuTimeline(MakeShared<CpuTimelineInternal>(Session.GetLinearAllocator()))
	, AsyncLoadingThreadCpuTimeline(MakeShared<CpuTimelineInternal>(Session.GetLinearAllocator()))
	, RequestsTable(Requests)
{
	RequestsTable.EditLayout().
		AddColumn(&FLoadRequest::Name, TEXT("Name")).
		AddColumn(&FLoadRequest::ThreadId, TEXT("ThreadId")).
		AddColumn(&FLoadRequest::StartTime, TEXT("StartTime")).
		AddColumn(&FLoadRequest::EndTime, TEXT("EndTime")).
		AddColumn<int32>(
			[](const FLoadRequest& Row)
			{
				return Row.Packages.Num();
			}, 
			TEXT("PackageCount")).
		AddColumn(&FLoadTimeProfilerProvider::PackageSizeSum, TEXT("Size")).
		AddColumn<const TCHAR*>(
			[](const FLoadRequest& Row)
			{
				return Row.Packages.Num() ? Row.Packages[0]->Name : TEXT("N/A");
			},
			TEXT("FirstPackage"));

	AggregatedStatsTableLayout.
		AddColumn(&FLoadTimeProfilerAggregatedStats::Name, TEXT("Name")).
		AddColumn(&FLoadTimeProfilerAggregatedStats::Count, TEXT("Count")).
		AddColumn(&FLoadTimeProfilerAggregatedStats::Total, TEXT("Total")).
		AddColumn(&FLoadTimeProfilerAggregatedStats::Min, TEXT("Min")).
		AddColumn(&FLoadTimeProfilerAggregatedStats::Max, TEXT("Max")).
		AddColumn(&FLoadTimeProfilerAggregatedStats::Average, TEXT("Avg")).
		AddColumn(&FLoadTimeProfilerAggregatedStats::Median, TEXT("Med"));

	PackagesTableLayout.
		AddColumn<const TCHAR*>([](const FPackagesTableRow& Row)
			{
				return Row.PackageInfo->Name;
			},
			TEXT("Package")).
		AddColumn<const TCHAR*>([](const FPackagesTableRow& Row)
			{
				return GetLoadTimeProfilerPackageEventTypeString(Row.EventType);
			},
			TEXT("EventType")).
		AddColumn(&FPackagesTableRow::SerializedHeaderSize, TEXT("SerializedHeaderSize")).
		AddColumn(&FPackagesTableRow::SerializedExportsCount, TEXT("SerializedExportsCount")).
		AddColumn(&FPackagesTableRow::SerializedExportsSize, TEXT("SerializedExportsSize")).
		AddColumn(&FPackagesTableRow::MainThreadTime, TEXT("MainThreadTime")).
		AddColumn(&FPackagesTableRow::AsyncLoadingThreadTime, TEXT("AsyncLoadingThreadTime"));

	ExportsTableLayout.
		AddColumn<const TCHAR*>([](const FExportsTableRow& Row)
			{
				return Row.ExportInfo->Package ? Row.ExportInfo->Package->Name : TEXT("[unknown]");
			},
			TEXT("Package")).
		AddColumn<const TCHAR*>([](const FExportsTableRow& Row)
			{
				return Row.ExportInfo->Class ? Row.ExportInfo->Class->Name : TEXT("[unknown]");
			},
			TEXT("Class")).
		AddColumn<const TCHAR*>([](const FExportsTableRow& Row)
			{
				return GetLoadTimeProfilerObjectEventTypeString(Row.EventType);
			},
			TEXT("EventType")).
		AddColumn(&FExportsTableRow::SerializedSize, TEXT("SerializedSize")).
		AddColumn(&FExportsTableRow::MainThreadTime, TEXT("MainThreadTime")).
		AddColumn(&FExportsTableRow::AsyncLoadingThreadTime, TEXT("AsyncLoadingThreadTime"));
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
	TTable<FLoadTimeProfilerAggregatedStats>* Table = new TTable<FLoadTimeProfilerAggregatedStats>(AggregatedStatsTableLayout);
	for (const auto& KV : Aggregation)
	{
		FLoadTimeProfilerAggregatedStats& Row = Table->AddRow();
		Row.Name = GetLoadTimeProfilerPackageEventTypeString(KV.Key);
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
	TTable<FLoadTimeProfilerAggregatedStats>* Table = new TTable<FLoadTimeProfilerAggregatedStats>(AggregatedStatsTableLayout);
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

ITable<FPackagesTableRow>* FLoadTimeProfilerProvider::CreatePackageDetailsTable(double IntervalStart, double IntervalEnd) const
{
	TTable<FPackagesTableRow>* Table = new TTable<FPackagesTableRow>(PackagesTableLayout);

	TMap<TTuple<const FPackageInfo*, ELoadTimeProfilerPackageEventType>, FPackagesTableRow*> PackagesMap;

	auto FindRow = [Table, &PackagesMap](const FLoadTimeProfilerCpuEvent& Event) -> FPackagesTableRow*
	{
		if (Event.Package)
		{
			auto Key = MakeTuple(Event.Package, Event.PackageEventType);
			FPackagesTableRow** FindIt = PackagesMap.Find(Key);
			FPackagesTableRow* Row = nullptr;
			if (!FindIt)
			{
				Row = &Table->AddRow();
				Row->PackageInfo = Event.Package;
				Row->EventType = Event.PackageEventType;
				PackagesMap.Add(Key, Row);
			}
			else
			{
				Row = *FindIt;
			}

			if (Event.Export && Event.ExportEventType == LoadTimeProfilerObjectEventType_Serialize)
			{
				++Row->SerializedExportsCount;
				Row->SerializedExportsSize += Event.Export->SerialSize;
			}

			if (!Event.Export && Event.PackageEventType == LoadTimeProfilerPackageEventType_CreateLinker)
			{
				Row->SerializedHeaderSize += Event.Package->Summary.TotalHeaderSize;
			}

			return Row;
		}
		else
		{
			return nullptr;
		}
	};

	AsyncLoadingThreadCpuTimeline->EnumerateEvents(IntervalStart, IntervalEnd, [Table, &PackagesMap, FindRow](double StartTime, double EndTime, uint32, const FLoadTimeProfilerCpuEvent& Event)
	{
		FPackagesTableRow* Row = FindRow(Event);
		if (Row)
		{
			Row->AsyncLoadingThreadTime += EndTime - StartTime; // TODO: Should be exclusive time
		}
	});
	MainThreadCpuTimeline->EnumerateEvents(IntervalStart, IntervalEnd, [Table, &PackagesMap, FindRow](double StartTime, double EndTime, uint32, const FLoadTimeProfilerCpuEvent& Event)
	{
		FPackagesTableRow* Row = FindRow(Event);
		if (Row)
		{
			Row->MainThreadTime += EndTime - StartTime; // TODO: Should be exclusive time
		}
	});

	return Table;
}

ITable<FExportsTableRow>* FLoadTimeProfilerProvider::CreateExportDetailsTable(double IntervalStart, double IntervalEnd) const
{
	TTable<FExportsTableRow>* Table = new TTable<FExportsTableRow>(ExportsTableLayout);

	TMap<TTuple<const FPackageExportInfo*, ELoadTimeProfilerObjectEventType>, FExportsTableRow*> ExportsMap;

	auto FindRow = [Table, &ExportsMap](const FLoadTimeProfilerCpuEvent& Event) -> FExportsTableRow*
	{
		if (Event.Export)
		{
			auto Key = MakeTuple(Event.Export, Event.ExportEventType);
			FExportsTableRow** FindIt = ExportsMap.Find(Key);
			FExportsTableRow* Row = nullptr;
			if (!FindIt)
			{
				Row = &Table->AddRow();
				Row->ExportInfo = Event.Export;
				Row->EventType = Event.ExportEventType;
				ExportsMap.Add(Key, Row);
			}
			else
			{
				Row = *FindIt;
			}

			if (Event.ExportEventType == LoadTimeProfilerObjectEventType_Serialize)
			{
				Row->SerializedSize += Event.Export->SerialSize;
			}

			return Row;
		}
		else
		{
			return nullptr;
		}
	};

	AsyncLoadingThreadCpuTimeline->EnumerateEvents(IntervalStart, IntervalEnd, [Table, &ExportsMap, FindRow](double StartTime, double EndTime, uint32, const FLoadTimeProfilerCpuEvent& Event)
	{
		FExportsTableRow* Row = FindRow(Event);
		if (Row)
		{
			Row->AsyncLoadingThreadTime += EndTime - StartTime; // TODO: Should be exclusive time
		}
	});
	MainThreadCpuTimeline->EnumerateEvents(IntervalStart, IntervalEnd, [Table, &ExportsMap, FindRow](double StartTime, double EndTime, uint32, const FLoadTimeProfilerCpuEvent& Event)
	{
		FExportsTableRow* Row = FindRow(Event);
		if (Row)
		{
			Row->MainThreadTime += EndTime - StartTime; // TODO: Should be exclusive time
		}
	});

	return Table;
}

const Trace::FClassInfo& FLoadTimeProfilerProvider::AddClassInfo(const TCHAR* ClassName)
{
	Session.WriteAccessCheck();

	FClassInfo& ClassInfo = ClassInfos.PushBack();
	ClassInfo.Name = Session.StoreString(ClassName);
	return ClassInfo;
}

Trace::FLoadRequest& FLoadTimeProfilerProvider::CreateRequest()
{
	Session.WriteAccessCheck();

	FLoadRequest& RequestInfo = Requests.PushBack();
	return RequestInfo;
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

FLoadTimeProfilerProvider::CpuTimelineInternal& FLoadTimeProfilerProvider::EditAdditionalCpuTimeline(uint32 ThreadId)
{
	if (AdditionalCpuTimelinesMap.Contains(ThreadId))
	{
		return AdditionalCpuTimelinesMap[ThreadId].Get();
	}
	else
	{
		TSharedRef<CpuTimelineInternal> Timeline = MakeShared<CpuTimelineInternal>(Session.GetLinearAllocator());
		AdditionalCpuTimelinesMap.Add(ThreadId, Timeline);
		return Timeline.Get();
	}
}

uint64 FLoadTimeProfilerProvider::PackageSizeSum(const FLoadRequest& Row)
{
	uint64 Sum = 0;
	for (const FPackageInfo* Package : Row.Packages)
	{
		Sum += Package->Summary.TotalHeaderSize;
		for (const FPackageExportInfo* Export : Package->Exports)
		{
			Sum += Export->SerialSize;
		}
	}
	return Sum;
}

const TCHAR* GetLoadTimeProfilerPackageEventTypeString(ELoadTimeProfilerPackageEventType EventType)
{
	switch (EventType)
	{
	case LoadTimeProfilerPackageEventType_CreateLinker:
		return TEXT("CreateLinker");
	case LoadTimeProfilerPackageEventType_FinishLinker:
		return TEXT("FinishLinker");
	case LoadTimeProfilerPackageEventType_StartImportPackages:
		return TEXT("StartImportPackages");
	case LoadTimeProfilerPackageEventType_SetupImports:
		return TEXT("SetupImports");
	case LoadTimeProfilerPackageEventType_SetupExports:
		return TEXT("SetupExports");
	case LoadTimeProfilerPackageEventType_ProcessImportsAndExports:
		return TEXT("ProcessImportsAndExports");
	case LoadTimeProfilerPackageEventType_ExportsDone:
		return TEXT("ExportsDone");
	case LoadTimeProfilerPackageEventType_PostLoadWait:
		return TEXT("PostLoadWait");
	case LoadTimeProfilerPackageEventType_StartPostLoad:
		return TEXT("StartPostLoad");
	case LoadTimeProfilerPackageEventType_Tick:
		return TEXT("Tick");
	case LoadTimeProfilerPackageEventType_DeferredPostLoad:
		return TEXT("DeferredPostLoad");
	case LoadTimeProfilerPackageEventType_Finish:
		return TEXT("Finish");
	case LoadTimeProfilerPackageEventType_None:
		return TEXT("None");
	}
	return TEXT("[invalid]");
}

const TCHAR* GetLoadTimeProfilerObjectEventTypeString(ELoadTimeProfilerObjectEventType EventType)
{
	switch (EventType)
	{
	case LoadTimeProfilerObjectEventType_Create:
		return TEXT("Create");
	case LoadTimeProfilerObjectEventType_Serialize:
		return TEXT("Serialize");
	case LoadTimeProfilerObjectEventType_PostLoad:
		return TEXT("PostLoad");
	case LoadTimeProfilerObjectEventType_None:
		return TEXT("None");
	}
	return TEXT("[invalid]");
}

}