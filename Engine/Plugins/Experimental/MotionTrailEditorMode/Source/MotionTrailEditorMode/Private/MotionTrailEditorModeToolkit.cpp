// Copyright Epic Games, Inc. All Rights Reserved.

#include "MotionTrailEditorModeToolkit.h"
#include "MotionTrailEditorMode.h"
#include "Engine/Selection.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "EditorModeManager.h"

#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "IDetailRootObjectCustomization.h"

#define LOCTEXT_NAMESPACE "FMotionTrailEditorModeEdModeToolkit"


namespace UE
{
namespace MotionTrailEditor
{

FMotionTrailEditorModeToolkit::FMotionTrailEditorModeToolkit()
{
}

void FMotionTrailEditorModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost)
{
	SAssignNew(TimingStatsTextWidget, STextBlock);
	FModeToolkit::Init(InitToolkitHost);
}

FName FMotionTrailEditorModeToolkit::GetToolkitFName() const
{
	return FName("MotionTrailEditorMode");
}

FText FMotionTrailEditorModeToolkit::GetBaseToolkitName() const
{
	return NSLOCTEXT("MotionTrailEditorModeToolkit", "DisplayName", "Motion Trail Editor Tool");
}

class UEdMode* FMotionTrailEditorModeToolkit::GetScriptableEditorMode() const
{
	return GLevelEditorModeTools().GetActiveScriptableMode("MotionTrailEditorMode");
}

TSharedPtr<SWidget> FMotionTrailEditorModeToolkit::GetInlineContent() const
{
	// TODO: fix crash where TimingStatsTextWidget becomes null when re-activating ed mode
	TSharedPtr<SWidget> ModeWidget = FModeToolkit::GetInlineContent();
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			ModeWidget.ToSharedRef()
		]
	+ SVerticalBox::Slot()
		[
			TimingStatsTextWidget.ToSharedRef()
		];
}

void FMotionTrailEditorModeToolkit::SetTimingStats(const TArray<TMap<FString, FTimespan>>& HierarchyStats)
{
	FString StatsString = "";
	int32 HierarchyIndex = 1;
	for (const TMap<FString, FTimespan>& TimingStats : HierarchyStats)
	{
		StatsString += FText::Format(LOCTEXT("TimingStatsTitle", "Timing Statistics for Trail Hierarchy {0} \n"), HierarchyIndex).ToString();

		for (const TPair<FString, FTimespan>& TimingStat : TimingStats)
		{
			StatsString += FText::Format(LOCTEXT("TimingStat", "{0}: {1}\n"), FText::FromString(TimingStat.Key), FText::FromString(TimingStat.Value.ToString())).ToString();
		}

		HierarchyIndex++;
	}

	TimingStatsTextWidget->SetText(FText::FromString(StatsString));
}

} // namespace MovieScene
} // namespace UE

#undef LOCTEXT_NAMESPACE
