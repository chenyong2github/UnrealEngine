// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "LogTraceAnalysis.h"
#include "AnalysisServicePrivate.h"
#include "Logging/LogTrace.h"
#include "Model/Log.h"

FLogTraceAnalyzer::FLogTraceAnalyzer(TSharedRef<Trace::FAnalysisSession> InSession)
	: Session(InSession)
	, LogProvider(InSession->EditLogProvider())
{

}

void FLogTraceAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
	auto& Builder = Context.InterfaceBuilder;

	Builder.RouteEvent(RouteId_LogCategory, "Logging", "LogCategory");
	Builder.RouteEvent(RouteId_LogMessageSpec, "Logging", "LogMessageSpec");
	Builder.RouteEvent(RouteId_LogMessage, "Logging", "LogMessage");
}

void FLogTraceAnalyzer::OnEvent(uint16 RouteId, const FOnEventContext& Context)
{
	Trace::FAnalysisSessionEditScope _(Session.Get());

	const auto& EventData = Context.EventData;
	switch (RouteId)
	{
	case RouteId_LogCategory:
	{
		uint64 CategoryPointer = EventData.GetValue("CategoryPointer").As<uint64>();
		Trace::FLogCategory& Category = LogProvider->GetCategory(CategoryPointer);
		uint16 NameLength = EventData.GetValue("NameLength").As<uint16>();
		Category.Name = FString(NameLength, reinterpret_cast<const TCHAR*>(EventData.GetAttachment()));
		Category.DefaultVerbosity = static_cast<ELogVerbosity::Type>(EventData.GetValue("DefaultVerbosity").As<uint8>());
		break;
	}
	case RouteId_LogMessageSpec:
	{
		uint64 LogPoint = EventData.GetValue("LogPoint").As<uint64>();
		Trace::FLogMessageSpec& Spec = LogProvider->GetMessageSpec(LogPoint);
		uint64 CategoryPointer = EventData.GetValue("CategoryPointer").As<uint64>();
		Trace::FLogCategory& Category = LogProvider->GetCategory(CategoryPointer);
		Spec.Category = &Category;
		Spec.Line = EventData.GetValue("Line").As<int32>();
		Spec.Verbosity = static_cast<ELogVerbosity::Type>(EventData.GetValue("Verbosity").As<uint8>());
		uint16 FileNameLength = EventData.GetValue("FileNameLength").As<uint16>();
		Spec.File = FString(FileNameLength, reinterpret_cast<const ANSICHAR*>(EventData.GetAttachment()));
		uint16 FormatStringLength = EventData.GetValue("FormatStringLength").As<uint16>();
		Spec.FormatString = FString(FormatStringLength, reinterpret_cast<const TCHAR*>(EventData.GetAttachment() + FileNameLength));
		break;
	}
	case RouteId_LogMessage:
	{
		uint64 LogPoint = EventData.GetValue("LogPoint").As<uint64>();
		uint64 Cycle = EventData.GetValue("Cycle").As<uint64>();
		LogProvider->AppendMessage(LogPoint, Context.SessionContext.TimestampFromCycle(Cycle), EventData.GetAttachmentSize(), EventData.GetAttachment());
		break;
	}
	}
}
