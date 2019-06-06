// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Model/FileActivity.h"
#include "Model/IntervalTimeline.h"
#include "AnalysisServicePrivate.h"

namespace Trace
{

FFileActivityProvider::FFileActivityProvider(IAnalysisSession& InSession)
	: Session(InSession)
	, Files(InSession.GetLinearAllocator(), 1024)
{
}

void FFileActivityProvider::EnumerateFileActivity(TFunctionRef<bool(const FFileInfo&, const ITimeline<FFileActivity>&)> Callback) const
{
	for (uint64 FileIndex = 0, FileCount = Files.Num(); FileIndex < FileCount; ++FileIndex)
	{
		const FFileInfoInternal& FileInfoInternal = Files[FileIndex];
		FFileInfo FileInfo;
		FileInfo.Id = FileIndex;
		FileInfo.Path = *FileInfoInternal.Path;
		if (!Callback(FileInfo, *FileInfoInternal.ActivityTimeline))
		{
			return;
		}
	}
}

uint32 FFileActivityProvider::GetFileIndex(const TCHAR* Path)
{
	uint32 FileIndex = Files.Num();
	FFileInfoInternal& FileInfo = Files.PushBack();
	FileInfo.Path = Path;
	FileInfo.ActivityTimeline = MakeShared<TimelineInternal>(Session.GetLinearAllocator());
	return FileIndex;
}

uint64 FFileActivityProvider::BeginActivity(uint32 FileIndex, EFileActivityType Type, uint64 Offset, uint64 Size, double Time)
{
	FFileInfoInternal& FileInfo = Files[FileIndex];
	FFileActivity FileActivity;
	FileActivity.ActivityType = Type;
	FileActivity.Offset = Offset;
	FileActivity.Size = Size;
	FileActivity.Failed = false;
	return FileInfo.ActivityTimeline->AppendBeginEvent(Time, FileActivity);
}

void FFileActivityProvider::EndActivity(uint32 FileIndex, uint64 ActivityIndex, double Time, bool Failed)
{
	FFileInfoInternal& FileInfo = Files[FileIndex];
	FFileActivity& Activity = FileInfo.ActivityTimeline->EndEvent(ActivityIndex, Time);
	Activity.Failed = Failed;
}

const TCHAR* FFileActivityProvider::GetFilePath(uint32 FileIndex) const
{
	return *Files[FileIndex].Path;
}

}