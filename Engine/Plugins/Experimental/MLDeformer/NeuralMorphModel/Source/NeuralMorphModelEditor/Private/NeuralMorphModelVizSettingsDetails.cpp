// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralMorphModelVizSettingsDetails.h"
#include "NeuralMorphModel.h"
#include "NeuralMorphEditorModel.h"
#include "NeuralMorphModelVizSettings.h"
#include "MLDeformerModule.h"
#include "MLDeformerModel.h"
#include "MLDeformerEditorModule.h"
#include "MLDeformerGeomCacheHelpers.h"
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

#define LOCTEXT_NAMESPACE "NeuralMorphModelVizSettingsDetails"

namespace UE::NeuralMorphModel
{
	using namespace UE::MLDeformer;

	TSharedRef<IDetailCustomization> FNeuralMorphModelVizSettingsDetails::MakeInstance()
	{
		return MakeShareable(new FNeuralMorphModelVizSettingsDetails());
	}

	bool FNeuralMorphModelVizSettingsDetails::UpdateMemberPointers(const TArray<TWeakObjectPtr<UObject>>& Objects)
	{
		if (!FMLDeformerVizSettingsDetails::UpdateMemberPointers(Objects))
		{
			return false;
		}

		NeuralMorphModel = Cast<UNeuralMorphModel>(Model);
		NeuralMorphVizSettings = Cast<UNeuralMorphModelVizSettings>(VizSettings);
		return (NeuralMorphModel != nullptr && NeuralMorphVizSettings != nullptr);
	}

	void FNeuralMorphModelVizSettingsDetails::AddGroundTruth()
	{
		TestAssetsCategory->AddProperty(GET_MEMBER_NAME_CHECKED(UNeuralMorphModelVizSettings, GroundTruth));

		// Show an error when the test anim sequence duration doesn't match the one of the ground truth.
		const FText AnimErrorText = GetGeomCacheAnimSequenceErrorText(NeuralMorphVizSettings->GetTestGroundTruth(), VizSettings->GetTestAnimSequence());
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

		const FText GeomErrorText = GetGeomCacheErrorText(Model->GetSkeletalMesh(), NeuralMorphVizSettings->GetTestGroundTruth());
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

	bool FNeuralMorphModelVizSettingsDetails::IsMorphTargetsEnabled() const
	{
		return NeuralMorphVizSettings->bDrawMorphTargets && !NeuralMorphModel->MorphTargetDeltas.IsEmpty();
	}

	void FNeuralMorphModelVizSettingsDetails::AddAdditionalSettings()
	{
		IDetailGroup& MorphsGroup = LiveSettingsCategory->AddGroup("Morph Targets", LOCTEXT("MorphTargetsLabel", "Morph Targets"), false, true);
		MorphsGroup.AddPropertyRow(DetailLayoutBuilder->GetProperty(GET_MEMBER_NAME_CHECKED(UNeuralMorphModelVizSettings, bDrawMorphTargets)))
			.EditCondition(!NeuralMorphModel->MorphTargetDeltas.IsEmpty(), nullptr);

		MorphsGroup.AddPropertyRow(DetailLayoutBuilder->GetProperty(GET_MEMBER_NAME_CHECKED(UNeuralMorphModelVizSettings, MorphTargetNumber)))
			.EditCondition(TAttribute<bool>(this, &FNeuralMorphModelVizSettingsDetails::IsMorphTargetsEnabled), nullptr);

		MorphsGroup.AddPropertyRow(DetailLayoutBuilder->GetProperty(GET_MEMBER_NAME_CHECKED(UNeuralMorphModelVizSettings, MorphTargetDeltaThreshold)))
			.EditCondition(TAttribute<bool>(this, &FNeuralMorphModelVizSettingsDetails::IsMorphTargetsEnabled), nullptr);
	}

}	//namespace UE::NeuralMorphModel

#undef LOCTEXT_NAMESPACE
