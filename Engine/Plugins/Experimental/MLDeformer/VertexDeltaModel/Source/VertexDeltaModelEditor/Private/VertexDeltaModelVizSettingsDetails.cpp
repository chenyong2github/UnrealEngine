// Copyright Epic Games, Inc. All Rights Reserved.

#include "VertexDeltaModelVizSettingsDetails.h"
#include "MLDeformerModule.h"
#include "MLDeformerModel.h"
#include "MLDeformerEditorModule.h"
#include "MLDeformerGeomCacheHelpers.h"
#include "VertexDeltaModel.h"
#include "VertexDeltaEditorModel.h"
#include "VertexDeltaModelVizSettings.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailsView.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailGroup.h"
#include "Modules/ModuleManager.h"
#include "Layout/Margin.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "PropertyCustomizationHelpers.h"
#include "AssetRegistry/AssetData.h"
#include "PropertyHandle.h"
#include "DetailLayoutBuilder.h"
#include "SWarningOrErrorBox.h"
#include "GeometryCache.h"
#include "Animation/AnimSequence.h"
#include "Animation/MeshDeformer.h"
#include "Engine/SkeletalMesh.h"

#define LOCTEXT_NAMESPACE "VertexDeltaModelVizSettingsDetails"

namespace UE::VertexDeltaModel
{
	using namespace UE::MLDeformer;

	TSharedRef<IDetailCustomization> FVertexDeltaModelVizSettingsDetails::MakeInstance()
	{
		return MakeShareable(new FVertexDeltaModelVizSettingsDetails());
	}

	bool FVertexDeltaModelVizSettingsDetails::UpdateMemberPointers(const TArray<TWeakObjectPtr<UObject>>& Objects)
	{
		if (!FMLDeformerVizSettingsDetails::UpdateMemberPointers(Objects))
		{
			return false;
		}

		VertexDeltaModel = Cast<UVertexDeltaModel>(Model);
		VertexDeltaVizSettings = Cast<UVertexDeltaModelVizSettings>(VizSettings);
		return (VertexDeltaModel != nullptr && VertexDeltaVizSettings != nullptr);
	}

	void FVertexDeltaModelVizSettingsDetails::AddGroundTruth()
	{
		TestAssetsCategory->AddProperty(GET_MEMBER_NAME_CHECKED(UVertexDeltaModelVizSettings, GroundTruth));

		// Show an error when the test anim sequence duration doesn't match the one of the ground truth.
		const FText AnimErrorText = GetGeomCacheAnimSequenceErrorText(VertexDeltaVizSettings->GetTestGroundTruth(), VizSettings->GetTestAnimSequence());
		FDetailWidgetRow& GroundTruthAnimErrorRow = TestAssetsCategory->AddCustomRow(FText::FromString("GroundTruthAnimMismatchError"))
			.Visibility(!AnimErrorText.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed)
			.WholeRowContent()
			[
				SNew(SBox)
				.Padding(FMargin(0.0f, 4.0f))
				[
					SNew(SWarningOrErrorBox)
					.MessageStyle(EMessageStyle::Warning)
					.Message(AnimErrorText)
				]
			];

		const FText GeomErrorText = GetGeomCacheErrorText(Model->GetSkeletalMesh(), VertexDeltaVizSettings->GetTestGroundTruth());
		FDetailWidgetRow& GroundTruthGeomErrorRow = TestAssetsCategory->AddCustomRow(FText::FromString("GroundTruthGeomMismatchError"))
			.Visibility(!GeomErrorText.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed)
			.WholeRowContent()
			[
				SNew(SBox)
				.Padding(FMargin(0.0f, 4.0f))
				[
					SNew(SWarningOrErrorBox)
					.MessageStyle(EMessageStyle::Warning)
					.Message(GeomErrorText)
				]
			];
	}
}	//namespace UE::VertexDeltaModel

#undef LOCTEXT_NAMESPACE
