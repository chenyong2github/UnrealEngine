// Copyright Epic Games, Inc. All Rights Reserved.

#include "TraceServices/Model/Log.h"
#include "Model/LogPrivate.h"
#include "AnalysisServicePrivate.h"
#include "Common/FormatArgs.h"

namespace Trace
{

const FName FLogProvider::ProviderName("LogProvider");

FLogProvider::FLogProvider(IAnalysisSession& InSession)
	: Session(InSession)
	, Categories(InSession.GetLinearAllocator(), 128)
	, MessageSpecs(InSession.GetLinearAllocator(), 1024)
	, Messages(InSession.GetLinearAllocator(), 1024)
	, MessagesTable(Messages)
{
	MessagesTable.EditLayout().
		AddColumn(&FLogMessageInternal::Time, TEXT("Time")).
		AddColumn<const TCHAR*>([](const FLogMessageInternal& Message)
			{
				return ToString(Message.Spec->Verbosity);
			},
			TEXT("Verbosity")).
		AddColumn<const TCHAR*>([](const FLogMessageInternal& Message)
			{
				return Message.Spec->Category->Name;
			},
			TEXT("Category")).
		AddColumn<const TCHAR*>([](const FLogMessageInternal& Message)
			{
				return Message.Spec->File;
			},
			TEXT("File")).
		AddColumn<int32>([](const FLogMessageInternal& Message)
			{
				return Message.Spec->Line;
			},
			TEXT("Line")).
		AddColumn(&FLogMessageInternal::Message, TEXT("Message"));
}

FLogCategory& FLogProvider::GetCategory(uint64 CategoryPointer)
{
	Session.WriteAccessCheck();
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
	Session.WriteAccessCheck();
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
	Session.WriteAccessCheck();
	FLogMessageSpec** FindSpec = SpecMap.Find(LogPoint);
	if (FindSpec && (*FindSpec)->Verbosity != ELogVerbosity::SetColor)
	{
		FLogMessageInternal& InternalMessage = Messages.PushBack();
		InternalMessage.Time = Time;
		InternalMessage.Spec = *FindSpec;
		FFormatArgsHelper::Format(FormatBuffer, FormatBufferSize - 1, TempBuffer, FormatBufferSize - 1, InternalMessage.Spec->FormatString, FormatArgs);
		InternalMessage.Message = Session.StoreString(FormatBuffer);
		Session.UpdateDurationSeconds(Time);
	}
}

uint64 FLogProvider::GetMessageCount() const
{
	Session.ReadAccessCheck();
	return Messages.Num();
}

bool FLogProvider::ReadMessage(uint64 Index, TFunctionRef<void(const FLogMessage &)> Callback) const
{
	Session.ReadAccessCheck();
	if (Index >= Messages.Num())
	{
		return false;
	}
	ConstructMessage(Index, Callback);
	return true;
}

void FLogProvider::EnumerateMessages(double IntervalStart, double IntervalEnd, TFunctionRef<void(const FLogMessage&)> Callback) const
{
	Session.ReadAccessCheck();
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
	Session.ReadAccessCheck();
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
	Session.ReadAccessCheck();
	for (auto Iterator = Categories.GetIteratorFromItem(0); Iterator; ++Iterator)
	{
		Callback(*Iterator);
	}
}

const ILogProvider& ReadLogProvider(const IAnalysisSession& Session)
{
	return *Session.ReadProvider<ILogProvider>(FLogProvider::ProviderName);
}

}