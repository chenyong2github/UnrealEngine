// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/AnalysisService.h"
#include "Common/SlabAllocator.h"
#include "Common/PagedArray.h"
#include "Model/MonotonicTimeline.h"

namespace Trace
{

class FAnalysisSessionLock;

class FLoadTimeProfilerProvider
	: public ILoadTimeProfilerProvider
{
public:
	typedef TMonotonicTimeline<FLoadTimeProfilerCpuEvent> CpuTimelineInternal;

	FLoadTimeProfilerProvider(FSlabAllocator& Allocator, FAnalysisSessionLock& InSessionLock);
	virtual uint64 GetPackageCount() const override { return Packages.Num(); }
	virtual void EnumeratePackages(TFunctionRef<void(const FPackageInfo&)> Callback) const override;
	virtual void ReadMainThreadCpuTimeline(TFunctionRef<void(const CpuTimeline&)> Callback) const override;
	virtual void ReadAsyncLoadingThreadCpuTimeline(TFunctionRef<void(const CpuTimeline&)> Callback) const override;
	FPackageInfo& CreatePackage(const TCHAR* PackageName);
	FPackageExportInfo& CreateExport();
	TSharedRef<CpuTimelineInternal> EditMainThreadCpuTimeline() { return MainThreadCpuTimeline; }
	TSharedRef<CpuTimelineInternal> EditAsyncLoadingThreadCpuTimeline() { return AsyncLoadingThreadCpuTimeline; }

private:
	FSlabAllocator& Allocator;
	FAnalysisSessionLock& SessionLock;
	TPagedArray<FPackageInfo> Packages;
	TPagedArray<FPackageExportInfo> Exports;
	TSharedRef<CpuTimelineInternal> MainThreadCpuTimeline;
	TSharedRef<CpuTimelineInternal> AsyncLoadingThreadCpuTimeline;
};

}
