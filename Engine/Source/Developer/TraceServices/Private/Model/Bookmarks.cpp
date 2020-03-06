// Copyright Epic Games, Inc. All Rights Reserved.

#include "TraceServices/Model/Bookmarks.h"
#include "Model/BookmarksPrivate.h"
#include "AnalysisServicePrivate.h"
#include "Common/FormatArgs.h"

namespace Trace
{

const FName FBookmarkProvider::ProviderName("BookmarkProvider");

FBookmarkProvider::FBookmarkProvider(IAnalysisSession& InSession)
	: Session(InSession)
{

}

FBookmarkSpec& FBookmarkProvider::GetSpec(uint64 BookmarkPoint)
{
	Session.WriteAccessCheck();
	if (SpecMap.Contains(BookmarkPoint))
	{
		return *SpecMap[BookmarkPoint].Get();
	}
	else
	{
		TSharedPtr<FBookmarkSpec> Spec = MakeShared<FBookmarkSpec>();
		Spec->File = TEXT("<unknown>");
		Spec->FormatString = TEXT("<unknown>");
		SpecMap.Add(BookmarkPoint, Spec);
		return *Spec.Get();
	}
}

void FBookmarkProvider::AppendBookmark(double Time, uint64 BookmarkPoint, const uint8* FormatArgs)
{
	Session.WriteAccessCheck();
	FBookmarkSpec& Spec = GetSpec(BookmarkPoint);
	TSharedRef<FBookmarkInternal> Bookmark = MakeShared<FBookmarkInternal>();
	Bookmark->Time = Time;
	FFormatArgsHelper::Format(FormatBuffer, FormatBufferSize - 1, TempBuffer, FormatBufferSize - 1, Spec.FormatString, FormatArgs);
	Bookmark->Text = Session.StoreString(FormatBuffer);
	Bookmarks.Add(Bookmark);
	Session.UpdateDurationSeconds(Time);
}

void FBookmarkProvider::EnumerateBookmarks(double IntervalStart, double IntervalEnd, TFunctionRef<void(const FBookmark &)> Callback) const
{
	Session.ReadAccessCheck();
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
		Bookmark.Text = InternalBookmark.Text;
		Callback(Bookmark);
	}
}

const IBookmarkProvider& ReadBookmarkProvider(const IAnalysisSession& Session)
{
	return *Session.ReadProvider<IBookmarkProvider>(FBookmarkProvider::ProviderName);
}

}