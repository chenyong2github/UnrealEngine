// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LiveLinkWindowsMixedRealityHandTrackingSourceFactory.h"
#include "LiveLinkWindowsMixedRealityHandTrackingSourceEditor.h"
#include "IWindowsMixedRealityHandTrackingPlugin.h"
#include "WindowsMixedRealityHandTracking.h"

#define LOCTEXT_NAMESPACE "WindowsMixedRealityHandTracking"

FText ULiveLinkWindowsMixedRealityHandTrackingSourceFactory::GetSourceDisplayName() const
{
	return LOCTEXT("HandTrackingLiveLinkSourceName", "Windows Mixed Reality Hand Tracking Source");
}

FText ULiveLinkWindowsMixedRealityHandTrackingSourceFactory::GetSourceTooltip() const
{
	return LOCTEXT("HandTrackingLiveLinkSourceTooltip", "Windows Mixed Reality Hand Tracking Key Points Source");
}

TSharedPtr<SWidget> ULiveLinkWindowsMixedRealityHandTrackingSourceFactory::CreateSourceCreationPanel()
{
	if (!ActiveSourceEditor.IsValid())
	{
		SAssignNew(ActiveSourceEditor, SLiveLinkWindowsMixedRealityHandTrackingSourceEditor);
	}
	return ActiveSourceEditor;
}

TSharedPtr<ILiveLinkSource> ULiveLinkWindowsMixedRealityHandTrackingSourceFactory::OnSourceCreationPanelClosed(bool bCreateSource)
{
	TSharedPtr<ILiveLinkSource> NewSource = nullptr;

	if (bCreateSource && ActiveSourceEditor.IsValid())
	{
		TSharedPtr<FWindowsMixedRealityHandTracking> HandTracking = StaticCastSharedPtr<FWindowsMixedRealityHandTracking>(IWindowsMixedRealityHandTrackingModule::Get().GetLiveLinkSource());

		// Here we could apply settings from SLiveLinkWindowsMixedRealityHandTrackingSourceEditor

		NewSource = HandTracking;
	}
	ActiveSourceEditor = nullptr;
	return NewSource;
}

#undef LOCTEXT_NAMESPACE