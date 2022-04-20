// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartitionRuntimeSpatialHashDetailsCustomization.h"
#include "WorldPartition/WorldPartitionRuntimeSpatialHash.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SCheckBox.h"

#define LOCTEXT_NAMESPACE "FWorldPartitionRuntimeSpatialHashDetails"

TSharedRef<IDetailCustomization> FWorldPartitionRuntimeSpatialHashDetails::MakeInstance()
{
	return MakeShareable(new FWorldPartitionRuntimeSpatialHashDetails);
}

void FWorldPartitionRuntimeSpatialHashDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
	DetailBuilder.GetObjectsBeingCustomized(ObjectsBeingCustomized);
	check(ObjectsBeingCustomized.Num() == 1);

	WorldPartitionRuntimeSpatialHash = CastChecked<UWorldPartitionRuntimeSpatialHash>(ObjectsBeingCustomized[0].Get());

	IDetailCategoryBuilder& RuntimeSettingsCategory = DetailBuilder.EditCategory("RuntimeSettings");

	RuntimeSettingsCategory.AddCustomRow(LOCTEXT("PreviewGrids", "Preview Grids"), false)
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("WorldPartitionRuntimeSpatialHashPreviewGrids", "Preview Grids"))
			.ToolTipText(LOCTEXT("WorldPartitionRuntimeSpatialHashPreviewGrids_ToolTip", "Toggle preview grids"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		[
			SNew(SCheckBox)
			.IsChecked(MakeAttributeLambda([this]() { return WorldPartitionRuntimeSpatialHash->bPreviewGrids ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }))
			.OnCheckStateChanged_Lambda([this](ECheckBoxState InState) { WorldPartitionRuntimeSpatialHash->bPreviewGrids = (InState == ECheckBoxState::Checked); })
		];
}

#undef LOCTEXT_NAMESPACE
