// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartitionDetailsCustomization.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
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

	if (WorldPartition->RuntimeHash)
	{
		FAddPropertyParams Params;
		Params.HideRootObjectNode(true);

		if (IDetailPropertyRow* RuntimeHashRow = WorldPartitionCategory.AddExternalObjects({ WorldPartition->RuntimeHash }, EPropertyLocation::Default, Params))
		{
			RuntimeHashRow->ShouldAutoExpand(true)
				.DisplayName(LOCTEXT("RuntimeHash", "Runtime Hash"))
				.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateLambda([this]() { return WorldPartition->bEnableStreaming ? EVisibility::Visible : EVisibility::Hidden; })));
		}
	}

	WorldPartitionCategory.AddCustomRow(LOCTEXT("WorldPartitionEditorCellSizeRow", "WorldPartitionEditorCellSize"), true)
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("WorldPartitionCellSize", "World Partition Editor Cell Size"))
			.ToolTipText(LOCTEXT("WorldPartitionEditorCellSize_ToolTip", "Set the world partition editor cell size, will take effect on the next world reload."))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
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

void FWorldPartitionDetails::HandleWorldPartitionEditorCellSizeChanged(uint32 NewValue)
{
	WorldPartition->SetEditorWantedCellSize(NewValue);
}

TOptional<uint32> FWorldPartitionDetails::HandleWorldPartitionEditorCellSizeValue() const
{
	return WorldPartition->GetWantedEditorCellSize();
}

#undef LOCTEXT_NAMESPACE
