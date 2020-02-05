// Copyright Epic Games, Inc. All Rights Reserved.

#include "DiagnosticsSessionAnalyzer.h"

namespace Insights
{

void FDiagnosticsSessionAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
	Context.InterfaceBuilder.RouteEvent(0, "Diagnostics", "Session");
}

bool FDiagnosticsSessionAnalyzer::OnEvent(uint16, const FOnEventContext& Context)
{
	const FEventData& EventData = Context.EventData;

	const uint8* Attachment = EventData.GetAttachment();
	if (Attachment == nullptr)
	{
		return false;
	}

	uint8 AppNameOffset = EventData.GetValue<uint8>("AppNameOffset");
	uint8 CommandLineOffset = EventData.GetValue<uint8>("CommandLineOffset");

	Platform = FString(AppNameOffset, (const ANSICHAR*)Attachment);

	Attachment += AppNameOffset;
	int32 AppNameLength = CommandLineOffset - AppNameOffset;
	AppName = FString(AppNameLength, (const ANSICHAR*)Attachment);

	Attachment += AppNameLength;
	int32 CommandLineLength = EventData.GetAttachmentSize() - CommandLineOffset;
	CommandLine = FString(CommandLineLength, (const ANSICHAR*)Attachment);

	ConfigurationType = EventData.GetValue<int8>("ConfigurationType");
	TargetType = EventData.GetValue<int8>("TargetType");

	return false;
}

} // namespace Insights
