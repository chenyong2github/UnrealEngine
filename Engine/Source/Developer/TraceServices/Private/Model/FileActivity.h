// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/Model/AnalysisSession.h"
#include "Common/PagedArray.h"
#include "Model/IntervalTimeline.h"
#include "TraceServices/Model/LoadTimeProfiler.h"
#include "Containers/UnrealString.h"
#include "Templates/SharedPointer.h"
#include "Model/Tables.h"

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
	typedef TIntervalTimeline<FFileActivity*, FTimelineSettings> TimelineInternal;

	FFileActivityProvider(IAnalysisSession& Session);
	virtual void EnumerateFileActivity(TFunctionRef<bool(const FFileInfo&, const Timeline&)> Callback) const override;
	virtual const ITable<FFileActivity>& GetFileActivityTable() const override;
	uint32 GetFileIndex(const TCHAR* Path);
	uint32 GetUnknownFileIndex();
	uint64 BeginActivity(uint32 FileIndex, EFileActivityType Type, uint32 ThreadId, uint64 Offset, uint64 Size, double Time);
	void EndActivity(uint32 FileIndex, uint64 ActivityIndex, uint64 ActualSize, double Time, bool Failed);
	const TCHAR* GetFilePath(uint32 FileIndex) const;

private:
	struct FFileInfoInternal
	{
		FFileInfo FileInfo;
		TSharedPtr<TimelineInternal> ActivityTimeline;
	};

	IAnalysisSession& Session;
	TPagedArray<FFileInfoInternal> Files;
	TPagedArray<FFileActivity> FileActivities;
	TTableView<FFileActivity> FileActivityTable;
};

}
