// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Model/LoadTimeProfiler.h"
#include "AnalysisServicePrivate.h"

namespace Trace
{

FLoadTimeProfilerProvider::FLoadTimeProfilerProvider(FSlabAllocator& InAllocator, FAnalysisSessionLock& InSessionLock)
	: Allocator(InAllocator)
	, SessionLock(InSessionLock)
	, Packages(Allocator, 4096)
	, Exports(Allocator, 4090)
	, MainThreadCpuTimeline(MakeShared<CpuTimelineInternal>(Allocator))
	, AsyncLoadingThreadCpuTimeline(MakeShared<CpuTimelineInternal>(Allocator))
{
	
}

void FLoadTimeProfilerProvider::EnumeratePackages(TFunctionRef<void(const FPackageInfo&)> Callback) const
{
	SessionLock.ReadAccessCheck();

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
	SessionLock.ReadAccessCheck();
	Callback(*MainThreadCpuTimeline);
}

void FLoadTimeProfilerProvider::ReadAsyncLoadingThreadCpuTimeline(TFunctionRef<void(const CpuTimeline &)> Callback) const
{
	SessionLock.ReadAccessCheck();
	Callback(*AsyncLoadingThreadCpuTimeline);
}

Trace::FPackageInfo& FLoadTimeProfilerProvider::CreatePackage(const TCHAR* PackageName)
{
	SessionLock.WriteAccessCheck();

	uint32 PackageId = Packages.Num();
	FPackageInfo& Package = Packages.PushBack();
	Package.Id = PackageId;
	Package.Name = PackageName;
	return Package;
}

Trace::FPackageExportInfo& FLoadTimeProfilerProvider::CreateExport()
{
	SessionLock.WriteAccessCheck();

	uint32 ExportId = Exports.Num();
	FPackageExportInfo& Export = Exports.PushBack();
	Export.Id = ExportId;
	return Export;
}

}