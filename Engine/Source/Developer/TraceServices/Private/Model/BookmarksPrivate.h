// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/AnalysisService.h"
#include "Templates/SharedPointer.h"

namespace TraceServices
{

class FAnalysisSessionLock;
class FStringStore;

struct FBookmarkSpec
{
	const TCHAR* File = nullptr;
	const TCHAR* FormatString = nullptr;
	int32 Line = 0;
};

struct FBookmarkInternal
{
	double Time = 0.0;
	const TCHAR* Text = nullptr;
};

class FBookmarkProvider
	: public IBookmarkProvider
{
public:
	static const FName ProviderName;

	explicit FBookmarkProvider(IAnalysisSession& Session);
	virtual ~FBookmarkProvider() {}

	FBookmarkSpec& GetSpec(uint64 BookmarkPoint);
	virtual uint64 GetBookmarkCount() const override { return Bookmarks.Num(); }
	void AppendBookmark(double Time, uint64 BookmarkPoint, const uint8* FormatArgs);
	virtual void EnumerateBookmarks(double IntervalStart, double IntervalEnd, TFunctionRef<void(const FBookmark&)> Callback) const override;

private:
	enum
	{
		FormatBufferSize = 65536
	};

	IAnalysisSession& Session;
	TMap<uint64, TSharedPtr<FBookmarkSpec>> SpecMap;
	TArray<TSharedRef<FBookmarkInternal>> Bookmarks;
	TCHAR FormatBuffer[FormatBufferSize];
	TCHAR TempBuffer[FormatBufferSize];
};

} // namespace TraceServices
