// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Model/Bookmarks.h"
#include "AnalysisServicePrivate.h"
#include "Common/FormatArgs.h"

namespace Trace
{

FBookmarkProvider::FBookmarkProvider(const FAnalysisSessionLock& InSessionLock)
	: SessionLock(InSessionLock)
{

}

FBookmarkSpec& FBookmarkProvider::GetSpec(uint64 BookmarkPoint)
{
	SessionLock.WriteAccessCheck();
	if (SpecMap.Contains(BookmarkPoint))
	{
		return *SpecMap[BookmarkPoint].Get();
	}
	else
	{
		TSharedPtr<FBookmarkSpec> Spec = MakeShared<FBookmarkSpec>();
		SpecMap.Add(BookmarkPoint, Spec);
		return *Spec.Get();
	}
}

void FBookmarkProvider::AppendBookmark(double Time, uint64 BookmarkPoint, uint16 FormatArgsSize, const uint8* FormatArgs)
{
	SessionLock.WriteAccessCheck();
	FBookmarkSpec& Spec = GetSpec(BookmarkPoint);
	TSharedRef<FBookmarkInternal> Bookmark = MakeShared<FBookmarkInternal>();
	Bookmark->Time = Time;
	TCHAR Buffer[4096];
	FFormatArgsHelper::Format(Buffer, 4096, *Spec.FormatString, FormatArgs);
	Bookmark->Text = Buffer;
	Bookmarks.Add(Bookmark);
}

void FBookmarkProvider::EnumerateBookmarks(double IntervalStart, double IntervalEnd, TFunctionRef<void(const FBookmark &)> Callback) const
{
	SessionLock.ReadAccessCheck();
	if (IntervalStart > IntervalEnd)
	{
		return;
	}
	uint64 FirstBookmarkIndex = Algo::LowerBoundBy(Bookmarks, IntervalStart, [](const TSharedRef<FBookmarkInternal>& B)
	{
		return B->Time;
	});
	uint64 BookmarkCount = Bookmarks.Num();
	if (FirstBookmarkIndex >= BookmarkCount)
	{
		return;
	}
	uint64 LastBookmarkIndex = Algo::UpperBoundBy(Bookmarks, IntervalEnd, [](const TSharedRef<FBookmarkInternal>& B)
	{
		return B->Time;
	});
	if (LastBookmarkIndex == 0)
	{
		return;
	}
	--LastBookmarkIndex;
	for (uint64 Index = FirstBookmarkIndex; Index <= LastBookmarkIndex; ++Index)
	{
		const FBookmarkInternal& InternalBookmark = Bookmarks[Index].Get();
		FBookmark Bookmark;
		Bookmark.Time = InternalBookmark.Time;
		Bookmark.Text = *InternalBookmark.Text;
		Callback(Bookmark);
	}
}

}