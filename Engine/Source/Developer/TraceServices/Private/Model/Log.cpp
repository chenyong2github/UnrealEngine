// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Model/Log.h"
#include "AnalysisServicePrivate.h"
#include "Common/FormatArgs.h"

namespace Trace
{

FLogProvider::FLogProvider(FSlabAllocator& InAllocator, FAnalysisSessionLock& InSessionLock)
	: Allocator(InAllocator)
	, SessionLock(InSessionLock)
	, Categories(Allocator, 128)
	, MessageSpecs(Allocator, 1024)
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

void FLogProvider::AppendMessage(uint64 LogPoint, double Time, uint16 FormatArgsSize, const uint8* FormatArgs)
{
	SessionLock.WriteAccessCheck();
	check(SpecMap.Contains(LogPoint));
	FLogMessageInternal& InternalMessage = Messages.AddDefaulted_GetRef();
	InternalMessage.Time = Time;
	InternalMessage.Spec = SpecMap[LogPoint];
	InternalMessage.FormatArgsOffset = FormatArgsMemory.Num();
	FormatArgsMemory.AddUninitialized(FormatArgsSize);
	memcpy(FormatArgsMemory.GetData() + InternalMessage.FormatArgsOffset, FormatArgs, FormatArgsSize);
}

uint64 FLogProvider::GetMessageCount() const
{
	SessionLock.ReadAccessCheck();
	return Messages.Num();
}

bool FLogProvider::ReadMessage(uint64 Index, TFunctionRef<void(const FLogMessage &)> Callback, bool ResolveFormatString) const
{
	SessionLock.ReadAccessCheck();
	if (Index >= Messages.Num())
	{
		return false;
	}
	ConstructMessage(Index, Callback, ResolveFormatString);
	return true;
}

void FLogProvider::EnumerateMessages(double IntervalStart, double IntervalEnd, TFunctionRef<void(const FLogMessage&)> Callback, bool ResolveFormatString) const
{
	SessionLock.ReadAccessCheck();
	if (IntervalStart > IntervalEnd)
	{
		return;
	}
	uint64 FirstMessageIndex = Algo::LowerBoundBy(Messages, IntervalStart, [](const FLogMessageInternal& M)
	{
		return M.Time;
	});
	uint64 MessageCount = Messages.Num();
	if (FirstMessageIndex >= MessageCount)
	{
		return;
	}
	uint64 LastMessageIndex = Algo::UpperBoundBy(Messages, IntervalEnd, [](const FLogMessageInternal& M)
	{
		return M.Time;
	});
	if (LastMessageIndex == 0)
	{
		return;
	}
	--LastMessageIndex;
	for (uint64 Index = FirstMessageIndex; Index <= LastMessageIndex; ++Index)
	{
		ConstructMessage(Index, Callback, ResolveFormatString);
	}
}

void FLogProvider::EnumerateMessagesByIndex(uint64 Start, uint64 End, TFunctionRef<void(const FLogMessage &)> Callback, bool ResolveFormatString) const
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
		ConstructMessage(Index, Callback, ResolveFormatString);
	}
}

void FLogProvider::ConstructMessage(uint64 Index, TFunctionRef<void(const FLogMessage &)> Callback, bool ResolveFormatString) const
{
	const FLogMessageInternal& InternalMessage = Messages[Index];
	FLogMessage Message;
	Message.Index = Index;
	Message.Time = InternalMessage.Time;
	Message.Category = InternalMessage.Spec->Category;
	Message.File = *InternalMessage.Spec->File;
	Message.Line = InternalMessage.Spec->Line;
	Message.Verbosity = InternalMessage.Spec->Verbosity;
	if (ResolveFormatString)
	{
		TCHAR Buffer[65535];
		FFormatArgsHelper::Format(Buffer, 65534, *InternalMessage.Spec->FormatString, FormatArgsMemory.GetData() + InternalMessage.FormatArgsOffset);
		Message.Message = Buffer;
	}
	else
	{
		Message.Message = *InternalMessage.Spec->FormatString;
	}
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