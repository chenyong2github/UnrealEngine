// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LogMessage.h"

#include "Misc/OutputDeviceHelper.h"
#include "Misc/ScopeLock.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/SessionService.h"

// Insights
#include "Insights/Common/TimeUtils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////
// FLogMessageRecord
////////////////////////////////////////////////////////////////////////////////////////////////////

FLogMessageRecord::FLogMessageRecord()
	: Index(0)
	, Time(0)
	, Verbosity(ELogVerbosity::Type::NoLogging)
	, Category()
	, Message()
	, File()
	, Line(0)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FLogMessageRecord::FLogMessageRecord(const Trace::FLogMessage& TraceLogMessage)
	: Index(static_cast<int32>(TraceLogMessage.Index))
	, Time(TraceLogMessage.Time)
	, Verbosity(TraceLogMessage.Verbosity)
	//, Category(FText::FromString(TraceLogMessage.Category))
	//, Message(FText::FromString(TraceLogMessage.Message))
	, File(FText::FromString(TraceLogMessage.File))
	, Line(TraceLogMessage.Line)
{
	// Strip the "Log" prefix.
	FString CategoryStr(TraceLogMessage.Category->Name);
	if (CategoryStr.StartsWith(TEXT("Log")))
	{
		CategoryStr = CategoryStr.RightChop(3);
	}
	Category = FText::FromString(CategoryStr);

	// Strip the trailing whitespaces (ex. some messages ends with "\n" and we do not want the LogView rows to have an unnecessary increased height).
	FString MessageStr(TraceLogMessage.Message);
	MessageStr.TrimEndInline();
	Message = FText::FromString(MessageStr);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText FLogMessageRecord::GetIndexAsText() const
{
	return FText::AsNumber(Index);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText FLogMessageRecord::GetTimeAsText() const
{
	return FText::FromString(TimeUtils::FormatTimeHMS(Time, TimeUtils::Microsecond));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText FLogMessageRecord::GetVerbosityAsText() const
{
	return FText::FromString(FString(FOutputDeviceHelper::VerbosityToString(Verbosity)));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText FLogMessageRecord::GetCategoryAsText() const
{
	return Category;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText FLogMessageRecord::GetMessageAsText() const
{
	return Message;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText FLogMessageRecord::GetFileAsText() const
{
	return File;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText FLogMessageRecord::GetLineAsText() const
{
	return FText::AsNumber(Line);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText FLogMessageRecord::ToDisplayString() const
{
	FTextBuilder TextBuilder;
	TextBuilder.AppendLine(GetTimeAsText());
	TextBuilder.AppendLineFormat(NSLOCTEXT("SLogView", "CategoryLine", "Category: {0}"), Category);
	TextBuilder.AppendLineFormat(NSLOCTEXT("SLogView", "MessageLine", "Message: {0}"), Message);
	return TextBuilder.ToText();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FLogMessageCache
////////////////////////////////////////////////////////////////////////////////////////////////////

FLogMessageCache::FLogMessageCache()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FLogMessageCache::Reset()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FLogMessageRecord& FLogMessageCache::Get(uint64 Index)
{
	{
		FScopeLock Lock(&CriticalSection);
		if (Map.Contains(Index))
		{
			return Map[Index];
		}

		// Just an arbitrary limit. Will purge the cache after this limit, to avoid using too much memory.
		if (Map.Num() > 10000)
		{
			Map.Reset();
		}
	}

	if (Session.IsValid())
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
		const Trace::ILogProvider& LogProvider = Trace::ReadLogProvider(*Session.Get());
		LogProvider.ReadMessage(Index, [this, Index](const Trace::FLogMessage& Message)
		{
			FScopeLock Lock(&CriticalSection);
			FLogMessageRecord Entry(Message);
			Map.Add(Index, Entry);
		});
	}

	{
		FScopeLock Lock(&CriticalSection);
		if (Map.Contains(Index))
		{
			return Map[Index];
		}
	}

	return InvalidEntry;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FLogMessageRecord> FLogMessageCache::GetUncached(uint64 Index) const
{
	TSharedPtr<FLogMessageRecord> EntryPtr;

	if (Session.IsValid())
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
		const Trace::ILogProvider& LogProvider = Trace::ReadLogProvider(*Session.Get());
		LogProvider.ReadMessage(Index, [&EntryPtr](const Trace::FLogMessage& Message)
		{
			EntryPtr = MakeShareable(new FLogMessageRecord(Message));
		});
	}

	return EntryPtr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
