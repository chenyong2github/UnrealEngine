// Copyright Epic Games, Inc. All Rights Reserved.

#include "SessionEditorFilterService.h"
#include "TraceServices/ITraceServicesModule.h"
#include "Templates/SharedPointer.h"
#include "Misc/CoreDelegates.h"
#include "Trace/Trace.h"

FSessionEditorFilterService::FSessionEditorFilterService(Trace::FSessionHandle InHandle, TSharedPtr<const Trace::IAnalysisSession> InSession) : FBaseSessionFilterService(InHandle, InSession)
{	
}

void FSessionEditorFilterService::OnApplyChannelChanges()
{
	if (FrameEnabledChannels.Num())
	{
		for (const FString& ChannelName : FrameEnabledChannels)
		{
			Trace::ToggleChannel(*ChannelName, true);
		}
		FrameEnabledChannels.Empty();
	}

	if (FrameDisabledChannels.Num())
	{
		for (const FString& ChannelName : FrameDisabledChannels)
		{
			Trace::ToggleChannel(*ChannelName, false);
		}

		FrameDisabledChannels.Empty();
	}
}
