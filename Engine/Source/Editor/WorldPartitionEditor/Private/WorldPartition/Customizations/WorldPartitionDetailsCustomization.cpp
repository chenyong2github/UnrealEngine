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
#include "IDocumentation.h"

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

			if (FMessageDialog::Open(EAppMsgType::YesNo, LOCTEXT("WorldPartitionEnableStreamingDialolg", "Please refer to our documentation for how to set up streaming.\n\nWould you like to open it now? ")) == EAppReturnType::Yes)
			{
				IDocumentation::Get()->Open(TEXT("world-partition-in-unreal-engine"), FDocumentationSourceInfo(TEXT("worldpartition")));
			}

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

#undef LOCTEXT_NAMESPACE
