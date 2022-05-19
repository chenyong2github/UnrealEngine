// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartitionDetailsCustomization.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "Misc/MessageDialog.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"

#define LOCTEXT_NAMESPACE "FWorldPartitionDetails"

TSharedRef<IDetailCustomization> FWorldPartitionDetails::MakeInstance()
{
	return MakeShareable(new FWorldPartitionDetails);
}

void FWorldPartitionDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
	DetailBuilder.GetObjectsBeingCustomized(ObjectsBeingCustomized);
	check(ObjectsBeingCustomized.Num() == 1);

	WorldPartition = CastChecked<UWorldPartition>(ObjectsBeingCustomized[0].Get());

	IDetailCategoryBuilder& WorldPartitionCategory = DetailBuilder.EditCategory("WorldPartition");

	WorldPartitionCategory.AddCustomRow(LOCTEXT("EnableStreaming", "Enable Streaming"), false)
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("WorldPartitionEnableStreaming", "Enable Streaming"))
			.ToolTipText(LOCTEXT("WorldPartitionEnableStreaming_ToolTip", "Set the world partition enable streaming state."))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		[
			SNew(SCheckBox)
			.IsChecked(MakeAttributeLambda([this]() { return WorldPartition.IsValid() && WorldPartition->IsStreamingEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }))
			.OnCheckStateChanged(this, &FWorldPartitionDetails::HandleWorldPartitionEnableStreamingChanged)
		]
		.Visibility(TAttribute<EVisibility>::CreateLambda([this]() { return WorldPartition.IsValid() && WorldPartition->SupportsStreaming() ? EVisibility::Visible : EVisibility::Hidden; }));

	if (WorldPartition->RuntimeHash)
	{
		FAddPropertyParams Params;
		Params.HideRootObjectNode(true);
		Params.UniqueId(TEXT("RuntimeHash"));

		if (IDetailPropertyRow* RuntimeHashRow = WorldPartitionCategory.AddExternalObjects({ WorldPartition->RuntimeHash }, EPropertyLocation::Default, Params))
		{
			(*RuntimeHashRow)
				.ShouldAutoExpand(true)
				.DisplayName(LOCTEXT("RuntimeHash", "Runtime Hash"))
				.Visibility(TAttribute<EVisibility>::CreateLambda([this]() { return WorldPartition.IsValid() && WorldPartition->IsStreamingEnabled() ? EVisibility::Visible : EVisibility::Hidden; }));
		}
	}

	WorldPartitionCategory.AddCustomRow(LOCTEXT("EditorCellSizeRow", "Editor Cell Size"), true)
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("WorldPartitionCellSize", "Editor Cell Size"))
			.ToolTipText(LOCTEXT("WorldPartitionEditorCellSize_ToolTip", "Set the world partition editor cell size, will take effect on the next world reload."))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SNumericEntryBox<uint32>)
				.AllowSpin(false)
				.MinSliderValue(100)
				.MaxSliderValue(100000)
				.OnValueChanged(this, &FWorldPartitionDetails::HandleWorldPartitionEditorCellSizeChanged)
				.Value(this, &FWorldPartitionDetails::HandleWorldPartitionEditorCellSizeValue)
			]
		];		
}

void FWorldPartitionDetails::HandleWorldPartitionEnableStreamingChanged(ECheckBoxState CheckState)
{
	if (CheckState == ECheckBoxState::Checked)
	{
		if (!WorldPartition->bStreamingWasEnabled)
		{
			if (FMessageDialog::Open(EAppMsgType::YesNo, LOCTEXT("WorldPartitionConfirmEnableStreaming", "You are about to enable streaming for the first time, the world will be setup to stream. Continue?")) == EAppReturnType::No)
			{
				return;
			}
			
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("WorldPartitionEnableStreamingDialolg", "Please refer to https://docs.unrealengine.com/5.0/en-US/world-partition-in-unreal-engine/ for how to set up streaming."));
			WorldPartition->bStreamingWasEnabled = true;
		}

		WorldPartition->SetEnableStreaming(true);
	}
	else
	{
		if (FMessageDialog::Open(EAppMsgType::YesNo, LOCTEXT("WorldPartitionConfirmDisableStreaming", "You are about to disable streaming, all actors in the world will be always loaded. Continue?")) == EAppReturnType::Yes)
		{
			WorldPartition->SetEnableStreaming(false);
		}
	}
}

void FWorldPartitionDetails::HandleWorldPartitionEditorCellSizeChanged(uint32 NewValue)
{
	WorldPartition->SetEditorWantedCellSize(NewValue);
}

TOptional<uint32> FWorldPartitionDetails::HandleWorldPartitionEditorCellSizeValue() const
{
	return WorldPartition->GetWantedEditorCellSize();
}

#undef LOCTEXT_NAMESPACE
