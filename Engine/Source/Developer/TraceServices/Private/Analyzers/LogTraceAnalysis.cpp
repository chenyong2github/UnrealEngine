// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "LogTraceAnalysis.h"
#include "AnalysisServicePrivate.h"
#include "Logging/LogTrace.h"
#include "Model/LogPrivate.h"

FLogTraceAnalyzer::FLogTraceAnalyzer(Trace::IAnalysisSession& InSession, Trace::FLogProvider& InLogProvider)
	: Session(InSession)
	, LogProvider(InLogProvider)
{

}

void FLogTraceAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
	auto& Builder = Context.InterfaceBuilder;

	Builder.RouteEvent(RouteId_LogCategory, "Logging", "LogCategory");
	Builder.RouteEvent(RouteId_LogMessageSpec, "Logging", "LogMessageSpec");
	Builder.RouteEvent(RouteId_LogMessage, "Logging", "LogMessage");
}

bool FLogTraceAnalyzer::OnEvent(uint16 RouteId, const FOnEventContext& Context)
{
	Trace::FAnalysisSessionEditScope _(Session);

	const auto& EventData = Context.EventData;
	switch (RouteId)
	{
	case RouteId_LogCategory:
	{
		uint64 CategoryPointer = EventData.GetValue<uint64>("CategoryPointer");
		Trace::FLogCategory& Category = LogProvider.GetCategory(CategoryPointer);
		Category.Name = Session.StoreString(reinterpret_cast<const TCHAR*>(EventData.GetAttachment()));
		Category.DefaultVerbosity = static_cast<ELogVerbosity::Type>(EventData.GetValue<uint8>("DefaultVerbosity"));
		break;
	}
	case RouteId_LogMessageSpec:
	{
		uint64 LogPoint = EventData.GetValue<uint64>("LogPoint");
		Trace::FLogMessageSpec& Spec = LogProvider.GetMessageSpec(LogPoint);
		uint64 CategoryPointer = EventData.GetValue<uint64>("CategoryPointer");
		Trace::FLogCategory& Category = LogProvider.GetCategory(CategoryPointer);
		Spec.Category = &Category;
		Spec.Line = EventData.GetValue<int32>("Line");
		Spec.Verbosity = static_cast<ELogVerbosity::Type>(EventData.GetValue<uint8>("Verbosity"));
		const ANSICHAR* File = reinterpret_cast<const ANSICHAR*>(EventData.GetAttachment());
		Spec.File = Session.StoreString(ANSI_TO_TCHAR(File));
		Spec.FormatString = Session.StoreString(reinterpret_cast<const TCHAR*>(EventData.GetAttachment() + strlen(File) + 1));
		break;
	}
	case RouteId_LogMessage:
	{
		uint64 LogPoint = EventData.GetValue<uint64>("LogPoint");
		uint64 Cycle = EventData.GetValue<uint64>("Cycle");
		LogProvider.AppendMessage(LogPoint, Context.SessionContext.TimestampFromCycle(Cycle), EventData.GetAttachment());
		break;
	}
	}

	return true;
}
