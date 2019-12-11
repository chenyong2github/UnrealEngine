// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/AnalysisService.h"
#include "Templates/SharedPointer.h"
#include "Common/PagedArray.h"
#include "Model/Tables.h"
#include "Misc/OutputDeviceHelper.h"

namespace Trace
{

class FAnalysisSessionLock;
class FStringStore;

struct FLogMessageSpec
{
	FLogCategory* Category = nullptr;
	const TCHAR* File = nullptr;
	const TCHAR* FormatString = nullptr;
	int32 Line;
	ELogVerbosity::Type Verbosity;
};

struct FLogMessageInternal
{
	FLogMessageSpec* Spec = nullptr;
	double Time;
	const TCHAR* Message = nullptr;
};

class FLogProvider :
	public ILogProvider
{
public:
	enum
	{
		ReservedLogCategory_Bookmark = 0
	};

	static const FName ProviderName;

	FLogProvider(IAnalysisSession& Session);

	FLogCategory& GetCategory(uint64 CategoryPointer);
	FLogMessageSpec& GetMessageSpec(uint64 LogPoint);
	void AppendMessage(uint64 LogPoint, double Time, const uint8* FormatArgs);

	virtual uint64 GetMessageCount() const override;
	virtual bool ReadMessage(uint64 Index, TFunctionRef<void(const FLogMessage &)> Callback) const override;
	virtual void EnumerateMessages(double IntervalStart, double IntervalEnd, TFunctionRef<void(const FLogMessage&)> Callback) const override;
	virtual void EnumerateMessagesByIndex(uint64 Start, uint64 End, TFunctionRef<void(const FLogMessage&)> Callback) const override;
	virtual uint64 GetCategoryCount() const override { return Categories.Num(); }
	virtual void EnumerateCategories(TFunctionRef<void(const FLogCategory&)> Callback) const override;
	virtual const IUntypedTable& GetMessagesTable() const override { return MessagesTable; }

private:
	void ConstructMessage(uint64 Id, TFunctionRef<void(const FLogMessage &)> Callback) const;

	enum
	{
		FormatBufferSize = 65536
	};

	IAnalysisSession& Session;
	TMap<uint64, FLogCategory*> CategoryMap;
	TMap<uint64, FLogMessageSpec*> SpecMap;
	TPagedArray<FLogCategory> Categories;
	TPagedArray<FLogMessageSpec> MessageSpecs;
	TPagedArray<FLogMessageInternal> Messages;
	TCHAR FormatBuffer[FormatBufferSize];
	TCHAR TempBuffer[FormatBufferSize];
	TTableView<FLogMessageInternal> MessagesTable;
};

}