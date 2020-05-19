// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "TraceServices/AnalysisService.h"
#include "TraceServices/Model/Memory.h"
#include "Common/PagedArray.h"

namespace Trace
{

class FMemoryProvider : public IMemoryProvider
{
public:
	FMemoryProvider(IAnalysisSession& Session);
	virtual ~FMemoryProvider() {}

	void AddEventSpec(FMemoryTagId Tag, const TCHAR* Name, FMemoryTagId ParentTag);
	void AddTrackerSpec(FMemoryTrackerId TrackerId, const TCHAR* Name);
	void AddTagSnapshot(FMemoryTrackerId TrackerId, double Time, const int64* Tags, const FMemoryTagSample* Values, uint32 TagCount);

	virtual uint32 GetTagSerial() const override;
	virtual uint32 GetTagCount() const override;
	virtual void EnumerateTags(TFunctionRef<void(const FMemoryTag&)> Callback) const override;
	virtual const FMemoryTag* GetTag(FMemoryTagId Id) const override;
	virtual uint32 GetTrackerCount() const override;
	virtual void EnumerateTrackers(TFunctionRef<void(const FMemoryTracker&)> Callback) const override;
	virtual uint64 GetTagSampleCount(FMemoryTrackerId Tracker, FMemoryTagId Tag) const override;
	virtual void EnumerateTagSamples(
		FMemoryTrackerId Tracker, 
		FMemoryTagId Tag, 
		double StartTime, 
		double EnddTime, 
		bool bIncludeRangeNeighbours,
		TFunctionRef<void(double Time, double Duration, const FMemoryTagSample&)> Callback) const override;

public:
	static FName ProviderName;

private:
	enum {
		DefaultPageSize = 65536,
	};

	struct FTagSampleData
	{
		// Sample values.
		TPagedArray<FMemoryTagSample> Values;

		// Cached pointer to the actual memory tag.
		FMemoryTag* TagPtr;

		FTagSampleData(ILinearAllocator& Allocator) : Values(Allocator, DefaultPageSize), TagPtr(nullptr) {}
		FTagSampleData(const FTagSampleData& Other) : Values(Other.Values), TagPtr(Other.TagPtr) {}
	};

	struct FTrackerData
	{
		// Timestamps for samples
		TArray<double> SampleTimes;

		// Samples for each tag
		TMap<FMemoryTagId, FTagSampleData> Samples;

		FTrackerData() {}

	private:
		FTrackerData(const FTrackerData&) = delete;
	};

	IAnalysisSession& Session;
	TMap<FMemoryTagId, FMemoryTag> TagDescs;
	TMap<FMemoryTrackerId, FMemoryTracker> TrackerDescs;
	TArray<FTrackerData> Trackers;
	uint32 TagsSerial;
};

} // namespace Trace
