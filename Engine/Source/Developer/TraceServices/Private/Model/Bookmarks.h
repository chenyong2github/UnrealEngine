// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/AnalysisService.h"
#include "Templates/SharedPointer.h"

namespace Trace
{

class FAnalysisSessionLock;

struct FBookmarkSpec
{
	FString File;
	FString FormatString;
	int32 Line;
};

struct FBookmarkInternal
{
	double Time;
	FString Text;
};

class FBookmarkProvider
	: public IBookmarkProvider
{
public:
	FBookmarkProvider(const FAnalysisSessionLock& SessionLock);

	FBookmarkSpec& GetSpec(uint64 BookmarkPoint);
	void AppendBookmark(double Time, uint64 BookmarkPoint, uint16 FormatArgsSize, const uint8* FormatArgs);
	virtual void EnumerateBookmarks(double IntervalStart, double IntervalEnd, TFunctionRef<void(const FBookmark&)> Callback) const override;

private:
	const FAnalysisSessionLock& SessionLock;
	TMap<uint64, TSharedPtr<FBookmarkSpec>> SpecMap;
	TArray<TSharedRef<FBookmarkInternal>> Bookmarks;
};

}