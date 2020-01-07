// Copyright Epic Games, Inc. All Rights Reserved.

#include "Insights/ViewModels/BaseTimingTrack.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#define LOCTEXT_NAMESPACE "BaseTimingTrack"

INSIGHTS_IMPLEMENT_RTTI(FBaseTimingTrack)

void FBaseTimingTrack::BuildContextMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.BeginSection(TEXT("Empty"), FText::FromString(GetName()));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ContextMenu_NA", "N/A"),
			LOCTEXT("ContextMenu_NA_Desc", "No actions available."),
			FSlateIcon(),
			FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([](){ return false; })),
			NAME_None,
			EUserInterfaceActionType::Button
		);
	}
	MenuBuilder.EndSection();
}

#undef LOCTEXT_NAMESPACE