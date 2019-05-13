// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/AnalysisService.h"
#include "Common/PagedArray.h"
#include "Model/IntervalTimeline.h"

namespace Trace
{

class FAnalysisSessionLock;

class FFileActivityProvider
	: public IFileActivityProvider
{
public:
	struct FTimelineSettings
	{
		enum
		{
			EventsPerPage = 128
		};
	};
	typedef TIntervalTimeline<FFileActivity, FTimelineSettings> TimelineInternal;

	FFileActivityProvider(FSlabAllocator& Allocator, FAnalysisSessionLock& SessionLock);
	virtual void EnumerateFileActivity(TFunctionRef<bool(const FFileInfo&, const Timeline&)> Callback) const override;
	uint32 GetFileIndex(const TCHAR* Path);
	uint64 BeginActivity(uint32 FileIndex, EFileActivityType Type, uint64 Offset, uint64 Size, double Time);
	void EndActivity(uint32 FileIndex, uint64 ActivityIndex, double Time, bool Failed);
	const TCHAR* GetFilePath(uint32 FileIndex) const;

private:
	struct FFileInfoInternal
	{
		FString Path;
		TSharedPtr<TimelineInternal> ActivityTimeline;
	};

	FSlabAllocator& Allocator;
	FAnalysisSessionLock& SessionLock;
	TPagedArray<FFileInfoInternal> Files;
};

}
