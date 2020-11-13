// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

namespace TraceServices
{
	struct FLogMessage;
	class IAnalysisSession;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

class FLogMessageRecord
{
public:
	FLogMessageRecord();
	FLogMessageRecord(const TraceServices::FLogMessage& Message);

	FText GetIndexAsText() const;
	FText GetTimeAsText() const;
	FText GetVerbosityAsText() const;
	FText GetCategoryAsText() const;
	FText GetMessageAsText() const;
	FText GetFileAsText() const;
	FText GetLineAsText() const;

	FText ToDisplayString() const;

public:
	int32 Index;
	double Time;
	ELogVerbosity::Type Verbosity;
	FText Category;
	FText Message;
	FText File;
	uint32 Line;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FLogMessageCache
{
public:
	FLogMessageCache();

	void SetSession(TSharedPtr<const TraceServices::IAnalysisSession> InSession);
	void Reset();

	FLogMessageRecord& Get(uint64 Index);
	TSharedPtr<FLogMessageRecord> GetUncached(uint64 Index) const;

private:
	FCriticalSection CriticalSection;
	TSharedPtr<const TraceServices::IAnalysisSession> Session;
	TMap<uint64, FLogMessageRecord> Map;
	FLogMessageRecord InvalidEntry;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FLogMessageCategory
{
public:
	FName Name;
	bool bIsVisible;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FLogMessage
{
public:
	FLogMessage(const int32 InIndex) : Index(InIndex) {}

	int32 GetIndex() const { return Index; }

private:
	int32 Index;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
