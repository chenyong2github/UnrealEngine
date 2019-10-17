// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

namespace Trace
{
	struct FLogMessage;
	class IAnalysisSession;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

class FLogMessageRecord
{
public:
	FLogMessageRecord();
	FLogMessageRecord(const Trace::FLogMessage& Message);

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

	void SetSession(TSharedPtr<const Trace::IAnalysisSession> InSession);
	void Reset();

	FLogMessageRecord& Get(uint64 Index);
	TSharedPtr<FLogMessageRecord> GetUncached(uint64 Index) const;

private:
	FCriticalSection CriticalSection;
	TSharedPtr<const Trace::IAnalysisSession> Session;
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
