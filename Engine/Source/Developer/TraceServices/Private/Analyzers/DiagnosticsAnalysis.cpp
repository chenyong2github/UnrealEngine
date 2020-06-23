// Copyright Epic Games, Inc. All Rights Reserved.
#include "DiagnosticsAnalysis.h"
#include "AnalysisServicePrivate.h"
#include "Common/Utils.h"

FDiagnosticsAnalyzer::FDiagnosticsAnalyzer(Trace::IAnalysisSession& InSession)
	: Session(InSession)
{
	Provider = Session.EditProvider<Trace::FDiagnosticsProvider>(Trace::FDiagnosticsProvider::ProviderName);
}

FDiagnosticsAnalyzer::~FDiagnosticsAnalyzer()
{
}

void FDiagnosticsAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
	FInterfaceBuilder& Builder = Context.InterfaceBuilder;

	Builder.RouteEvent(RouteId_Session, "Diagnostics", "Session");
	Builder.RouteEvent(RouteId_Session2, "Diagnostics", "Session2");
}

bool FDiagnosticsAnalyzer::OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context)
{
	if (!Provider)
	{
		return false;
	}

	Trace::FAnalysisSessionEditScope _(Session);

	const auto& EventData = Context.EventData;
	switch (RouteId)
	{
	case RouteId_Session:
	{
		const uint8* Attachment = EventData.GetAttachment();
		if (Attachment == nullptr)
		{
			return false;
		}

		Trace::FSessionInfo SessionInfo;
		uint8 AppNameOffset = EventData.GetValue<uint8>("AppNameOffset");
		uint8 CommandLineOffset = EventData.GetValue<uint8>("CommandLineOffset");

		SessionInfo.Platform = FString(AppNameOffset, (const ANSICHAR*)Attachment);

		Attachment += AppNameOffset;
		int32 AppNameLength = CommandLineOffset - AppNameOffset;
		SessionInfo.AppName = FString(AppNameLength, (const ANSICHAR*)Attachment);

		Attachment += AppNameLength;
		int32 CommandLineLength = EventData.GetAttachmentSize() - CommandLineOffset;
		SessionInfo.CommandLine = FString(CommandLineLength, (const ANSICHAR*)Attachment);

		SessionInfo.ConfigurationType = (EBuildConfiguration) EventData.GetValue<uint8>("ConfigurationType");
		SessionInfo.TargetType = (EBuildTargetType) EventData.GetValue<uint8>("TargetType");

		Provider->SetSessionInfo(SessionInfo);

		return false;
	}
	break;
	case RouteId_Session2:
	{
		Trace::FSessionInfo SessionInfo;

		EventData.GetString("Platform", SessionInfo.Platform);
		EventData.GetString("AppName", SessionInfo.AppName);
		EventData.GetString("CommandLine", SessionInfo.CommandLine);

		SessionInfo.ConfigurationType = (EBuildConfiguration) EventData.GetValue<uint8>("ConfigurationType");
		SessionInfo.TargetType = (EBuildTargetType) EventData.GetValue<uint8>("TargetType");

		Provider->SetSessionInfo(SessionInfo);

		return false;
	};
	break;
	}

	return true;
}
