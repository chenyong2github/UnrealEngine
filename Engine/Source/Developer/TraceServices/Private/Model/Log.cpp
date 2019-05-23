// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Model/Log.h"
#include "AnalysisServicePrivate.h"
#include "Common/FormatArgs.h"

namespace Trace
{

FLogProvider::FLogProvider(FSlabAllocator& InAllocator, FAnalysisSessionLock& InSessionLock, FStringStore& InStringStore)
	: Allocator(InAllocator)
	, SessionLock(InSessionLock)
	, StringStore(InStringStore)
	, Categories(Allocator, 128)
	, MessageSpecs(Allocator, 1024)
	, Messages(Allocator, 1024)
	, MessagesTable(Messages)
{

}

FLogCategory& FLogProvider::GetCategory(uint64 CategoryPointer)
{
	SessionLock.WriteAccessCheck();
	if (CategoryMap.Contains(CategoryPointer))
	{
		return *CategoryMap[CategoryPointer];
	}
	else
	{
		FLogCategory& Category = Categories.PushBack();
		CategoryMap.Add(CategoryPointer, &Category);
		return Category;
	}
}

FLogMessageSpec& FLogProvider::GetMessageSpec(uint64 LogPoint)
{
	SessionLock.WriteAccessCheck();
	if (SpecMap.Contains(LogPoint))
	{
		return *SpecMap[LogPoint];
	}
	else
	{
		FLogMessageSpec& Spec = MessageSpecs.PushBack();
		SpecMap.Add(LogPoint, &Spec);
		return Spec;
	}
}

void FLogProvider::AppendMessage(uint64 LogPoint, double Time, const uint8* FormatArgs)
{
	SessionLock.WriteAccessCheck();
	check(SpecMap.Contains(LogPoint));
	FLogMessageInternal& InternalMessage = Messages.PushBack();
	InternalMessage.Time = Time;
	InternalMessage.Spec = SpecMap[LogPoint];
	FFormatArgsHelper::Format(FormatBuffer, FormatBufferSize - 1, InternalMessage.Spec->FormatString, FormatArgs);
	InternalMessage.Message = StringStore.Store(FormatBuffer);
}

uint64 FLogProvider::GetMessageCount() const
{
	SessionLock.ReadAccessCheck();
	return Messages.Num();
}

bool FLogProvider::ReadMessage(uint64 Index, TFunctionRef<void(const FLogMessage &)> Callback) const
{
	SessionLock.ReadAccessCheck();
	if (Index >= Messages.Num())
	{
		return false;
	}
	ConstructMessage(Index, Callback);
	return true;
}

void FLogProvider::EnumerateMessages(double IntervalStart, double IntervalEnd, TFunctionRef<void(const FLogMessage&)> Callback) const
{
	SessionLock.ReadAccessCheck();
	if (IntervalStart > IntervalEnd)
	{
		return;
	}
	uint64 MessageCount = Messages.Num();
	for (uint64 Index = 0; Index < MessageCount; ++Index)
	{
		double Time = Messages[Index].Time;
		if (IntervalStart <= Time && Time <= IntervalEnd)
		{
			ConstructMessage(Index, Callback);
		}
	}
}

void FLogProvider::EnumerateMessagesByIndex(uint64 Start, uint64 End, TFunctionRef<void(const FLogMessage &)> Callback) const
{
	SessionLock.ReadAccessCheck();
	uint64 Count = Messages.Num();
	if (Start >= Count)
	{
		return;
	}
	if (End > Count)
	{
		End = Count;
	}
	if (Start >= End)
	{
		return;
	}
	for (uint64 Index = Start; Index < End; ++Index)
	{
		ConstructMessage(Index, Callback);
	}
}

void FLogProvider::ConstructMessage(uint64 Index, TFunctionRef<void(const FLogMessage &)> Callback) const
{
	const FLogMessageInternal& InternalMessage = Messages[Index];
	FLogMessage Message;
	Message.Index = Index;
	Message.Time = InternalMessage.Time;
	Message.Category = InternalMessage.Spec->Category;
	Message.File = InternalMessage.Spec->File;
	Message.Line = InternalMessage.Spec->Line;
	Message.Verbosity = InternalMessage.Spec->Verbosity;
	Message.Message = InternalMessage.Message;
	Callback(Message);
}

void FLogProvider::EnumerateCategories(TFunctionRef<void(const FLogCategory &)> Callback) const
{
	auto Iterator = Categories.GetIteratorFromItem(0);
	const FLogCategory* Category = Iterator.GetCurrentItem();
	while (Category)
	{
		Callback(*Category);
		Category = Iterator.NextItem();
	}
}

}