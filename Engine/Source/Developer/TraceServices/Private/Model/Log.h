// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/AnalysisService.h"
#include "Templates/SharedPointer.h"
#include "Common/PagedArray.h"

namespace Trace
{

class FAnalysisSessionLock;

struct FLogMessageSpec
{
	FLogCategory* Category;
	FString File;
	FString FormatString;
	int32 Line;
	ELogVerbosity::Type Verbosity;
};

struct FLogMessageInternal
{
	FLogMessageSpec* Spec;
	double Time;
	uint64 FormatArgsOffset;
};

class FLogProvider :
	public ILogProvider
{
public:
	enum
	{
		ReservedLogCategory_Bookmark = 0
	};
	
	FLogProvider(FSlabAllocator& Allocator, FAnalysisSessionLock& SessionLock);

	FLogCategory& GetCategory(uint64 CategoryPointer);
	FLogMessageSpec& GetMessageSpec(uint64 LogPoint);
	void AppendMessage(uint64 LogPoint, double Time, uint16 FormatArgsSize, const uint8* FormatArgs);

	virtual uint64 GetMessageCount() const override;
	virtual bool ReadMessage(uint64 Index, TFunctionRef<void(const FLogMessage &)> Callback, bool ResolveFormatString) const override;
	virtual void EnumerateMessages(double IntervalStart, double IntervalEnd, TFunctionRef<void(const FLogMessage&)> Callback, bool ResolveFormatString) const override;
	virtual void EnumerateMessagesByIndex(uint64 Start, uint64 End, TFunctionRef<void(const FLogMessage&)> Callback, bool ResolveFormatString) const override;
	virtual void EnumerateCategories(TFunctionRef<void(const FLogCategory&)> Callback) const override;

private:
	void ConstructMessage(uint64 Id, TFunctionRef<void(const FLogMessage &)> Callback, bool ResolveFormatString) const;
	
	FSlabAllocator& Allocator;
	FAnalysisSessionLock& SessionLock;
	TMap<uint64, FLogCategory*> CategoryMap;
	TMap<uint64, FLogMessageSpec*> SpecMap;
	TPagedArray<FLogCategory> Categories;
	TPagedArray<FLogMessageSpec> MessageSpecs;
	TArray<FLogMessageInternal> Messages;
	TArray<uint8> FormatArgsMemory;
};

}