// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformerVizSettingsDetails.h"
#include "MLDeformerVizSettings.h"
#include "MLDeformer.h"
#include "MLDeformerAsset.h"

#include "MLDeformerEditorData.h"
#include "ComputeFramework/ComputeGraph.h"

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailsView.h"

#include "Widgets/SWidget.h"
#include "Layout/Margin.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SBoxPanel.h"
#include "EditorStyleSet.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailGroup.h"
#include "Framework/Application/SlateApplication.h"
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

#define LOCTEXT_NAMESPACE "MLDeformerVizSettingsDetails"

TSharedRef<IDetailCustomization> FMLDeformerVizSettingsDetails::MakeInstance()
{
	return MakeShareable(new FMLDeformerVizSettingsDetails());
}

UMLDeformerAsset* FMLDeformerVizSettingsDetails::GetMLDeformerAsset() const
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailLayoutBuilder->GetObjectsBeingCustomized(Objects);
	UMLDeformerVizSettings* VizSettings = nullptr;
	UMLDeformerAsset* DeformerAsset = nullptr;
	if (Objects.Num() == 1)
	{
		VizSettings = Cast<UMLDeformerVizSettings>(Objects[0]);
		check(VizSettings);
		DeformerAsset = Cast<UMLDeformerAsset>(VizSettings->GetOuter());
		check(DeformerAsset);
		return DeformerAsset;
	}

	return nullptr;
}

void FMLDeformerVizSettingsDetails::CustomizeDetails(class IDetailLayoutBuilder& DetailBuilder)
{
	DetailLayoutBuilder = &DetailBuilder;

	UMLDeformerAsset* DeformerAsset = GetMLDeformerAsset();
	UMLDeformerVizSettings* VizSettings = DeformerAsset->GetVizSettings();

	const bool bShowTrainingData = VizSettings ? (VizSettings->GetVisualizationMode() == EMLDeformerVizMode::TrainingData) : true;
	const bool bShowTestData = VizSettings ? (VizSettings->GetVisualizationMode() == EMLDeformerVizMode::TestData) : true;

	IDetailCategoryBuilder& DataCategoryBuilder = DetailBuilder.EditCategory("Data Selection", FText::GetEmpty(), ECategoryPriority::Important);
	DataCategoryBuilder.AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, VisualizationMode));

	// Shared settings.
	IDetailCategoryBuilder& SharedCategoryBuilder = DetailBuilder.EditCategory("Shared Settings", FText::GetEmpty(), ECategoryPriority::Important);
	SharedCategoryBuilder.AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, bDrawLabels));
	SharedCategoryBuilder.AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, LabelHeight));
	SharedCategoryBuilder.AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, LabelScale));
	SharedCategoryBuilder.AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, MeshSpacing));

	// Test Assets.
	IDetailCategoryBuilder& TestAssetsCategory = DetailBuilder.EditCategory("Test Assets", FText::GetEmpty(), ECategoryPriority::Important);
	TestAssetsCategory.SetCategoryVisibility(bShowTestData);
	
	IDetailPropertyRow& TestAnimRow = TestAssetsCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, TestAnimSequence));
	TestAnimRow.CustomWidget()
	.NameContent()
	[
		TestAnimRow.GetPropertyHandle()->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		SNew(SObjectPropertyEntryBox)
		.PropertyHandle(TestAnimRow.GetPropertyHandle())
		.AllowedClass(UAnimSequence::StaticClass())
		.ObjectPath(VizSettings ? VizSettings->GetTestAnimSequence()->GetPathName() : FString())
		.ThumbnailPool(DetailBuilder.GetThumbnailPool())
		.OnShouldFilterAsset(
			this, 
			&FMLDeformerVizSettingsDetails::FilterAnimSequences, 
			DeformerAsset->GetSkeletalMesh() ? DeformerAsset->GetSkeletalMesh()->GetSkeleton() : nullptr
		)
	];

	if (VizSettings)
	{
		const FText AnimErrorText = DeformerAsset->GetIncompatibleSkeletonErrorText(DeformerAsset->GetSkeletalMesh(), VizSettings->GetTestAnimSequence());
		FDetailWidgetRow& AnimErrorRow = TestAssetsCategory.AddCustomRow(FText::FromString("AnimSkeletonMisMatchError"))
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
	}

	FIsResetToDefaultVisible IsResetVisible = FIsResetToDefaultVisible::CreateSP(this, &FMLDeformerVizSettingsDetails::IsResetToDefaultDeformerGraphVisible);
	FResetToDefaultHandler ResetHandler = FResetToDefaultHandler::CreateSP(this, &FMLDeformerVizSettingsDetails::OnResetToDefaultDeformerGraph);
	FResetToDefaultOverride ResetOverride = FResetToDefaultOverride::Create(IsResetVisible, ResetHandler);
	TestAssetsCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, DeformerGraph)).OverrideResetToDefault(ResetOverride);

	// Show a warning when no deformer graph has been selected.
	UObject* Graph = nullptr;
	TSharedRef<IPropertyHandle> DeformerGraphProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, DeformerGraph));
	if (DeformerGraphProperty->GetValue(Graph) == FPropertyAccess::Result::Success)
	{
		FDetailWidgetRow& GraphErrorRow = TestAssetsCategory.AddCustomRow(FText::FromString("GraphError"))
			.Visibility((Graph == nullptr) ? EVisibility::Visible : EVisibility::Collapsed)
			.WholeRowContent()
			[
				SNew(SBox)
				.Padding(FMargin(0.0f, 4.0f))
				[
					SNew(SWarningOrErrorBox)
					.MessageStyle(EMessageStyle::Warning)
					.Message(FText::FromString("Please select a deformer graph.\nOtherwise only linear skinning is used."))
				]
			];
	}

	if (DeformerAsset)
	{
		FDetailWidgetRow& ErrorRow = TestAssetsCategory.AddCustomRow(FText::FromString("NoNeuralNetError"))
			.Visibility((DeformerAsset->GetInferenceNeuralNetwork() == nullptr && Graph != nullptr) ? EVisibility::Visible : EVisibility::Collapsed)
			.WholeRowContent()
			[
				SNew(SBox)
				.Padding(FMargin(0.0f, 4.0f))
				[
					SNew(SWarningOrErrorBox)
					.MessageStyle(EMessageStyle::Warning)
					.Message(FText::FromString("The selected deformer graph isn't used, because you didn't train the neural network yet.\n\nLinear skinning is used until then."))
				]
			];
	}

	TestAssetsCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, GroundTruth));

	// Show an error when the test anim sequence duration doesn't match the one of the ground truth.
	if (VizSettings)
	{
		const FText AnimErrorText = DeformerAsset->GetAnimSequenceErrorText(VizSettings->GroundTruth, VizSettings->GetTestAnimSequence());
		FDetailWidgetRow& GroundTruthAnimErrorRow = TestAssetsCategory.AddCustomRow(FText::FromString("GroundTruthAnimMismatchError"))
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

		const FText GeomErrorText = DeformerAsset->GetGeomCacheErrorText(VizSettings->GetGroundTruth());
		FDetailWidgetRow& GroundTruthGeomErrorRow = TestAssetsCategory.AddCustomRow(FText::FromString("GroundTruthGeomMismatchError"))
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

		const FText VertexErrorText = DeformerAsset->GetVertexErrorText(DeformerAsset->SkeletalMesh, VizSettings->GetGroundTruth(), FText::FromString("Base Mesh"), FText::FromString("Ground Truth Mesh"));
		FDetailWidgetRow& GroundTruthVertexErrorRow = TestAssetsCategory.AddCustomRow(FText::FromString("GroundTruthVertexMismatchError"))
			.Visibility(!VertexErrorText.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed)
			.WholeRowContent()
			[
				SNew(SBox)
				.Padding(FMargin(0.0f, 4.0f))
				[
					SNew(SWarningOrErrorBox)
					.MessageStyle(EMessageStyle::Warning)
					.Message(VertexErrorText)
				]
			];
	}

	IDetailCategoryBuilder& LiveSettingsCategory = DetailBuilder.EditCategory("Live Settings", FText::GetEmpty(), ECategoryPriority::Important);
	LiveSettingsCategory.SetCategoryVisibility(bShowTestData);

	LiveSettingsCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, VertexDeltaMultiplier));
	LiveSettingsCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, AnimPlaySpeed));

	IDetailGroup& HeatMapGroup = LiveSettingsCategory.AddGroup("HeatMap", LOCTEXT("HeatMap", "Heat Map"), false, true);
	HeatMapGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, bShowHeatMap)));
	HeatMapGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, HeatMapMode)));
	HeatMapGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, HeatMapScale)));
	HeatMapGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, GroundTruthLerp)));

	IDetailGroup& VisGroup = LiveSettingsCategory.AddGroup("Visibility", LOCTEXT("Visibility", "Visibility"), false, true);
	VisGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, bDrawLinearSkinnedActor)));
	VisGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, bDrawMLDeformedActor)));
	VisGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, bDrawGroundTruthActor)));

	// Training data.
	IDetailCategoryBuilder& TrainingMeshesCategoryBuilder = DetailBuilder.EditCategory("Training Meshes", FText::GetEmpty(), ECategoryPriority::Important);
	TrainingMeshesCategoryBuilder.SetCategoryVisibility(bShowTrainingData);
	TrainingMeshesCategoryBuilder.AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, FrameNumber));
	TrainingMeshesCategoryBuilder.AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, bDrawDeltas));
	TrainingMeshesCategoryBuilder.AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, bXRayDeltas));
}

bool FMLDeformerVizSettingsDetails::FilterAnimSequences(const FAssetData& AssetData, USkeleton* Skeleton)
{
	if (Skeleton && Skeleton->IsCompatibleSkeletonByAssetData(AssetData))
	{
		return false;
	}

	return true;
}

void FMLDeformerVizSettingsDetails::OnResetToDefaultDeformerGraph(TSharedPtr<IPropertyHandle> PropertyHandle)
{
	UMLDeformerAsset* DeformerAsset = GetMLDeformerAsset();
	UMLDeformerVizSettings* VizSettings = DeformerAsset->GetVizSettings();

	UMeshDeformer* MeshDeformer = FMLDeformerEditorData::LoadDefaultDeformerGraph();
	PropertyHandle->SetValue(MeshDeformer);
}

bool FMLDeformerVizSettingsDetails::IsResetToDefaultDeformerGraphVisible(TSharedPtr<IPropertyHandle> PropertyHandle)
{
	UObject* CurrentGraph = nullptr;
	PropertyHandle->GetValue(CurrentGraph);
	if (CurrentGraph == nullptr)
	{
		return true;
	}

	// Check if we already assigned the default asset.
	const FAssetData CurrentGraphAssetData(CurrentGraph);
	const FString CurrentPath = CurrentGraphAssetData.ObjectPath.ToString();
	const FString DefaultPath = FMLDeformerEditorData::GetDefaultDeformerGraphAssetPath();
	return (DefaultPath != CurrentPath);
}

#undef LOCTEXT_NAMESPACE
