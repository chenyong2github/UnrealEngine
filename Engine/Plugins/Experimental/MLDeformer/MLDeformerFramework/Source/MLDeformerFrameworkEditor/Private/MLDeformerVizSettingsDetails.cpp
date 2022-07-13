// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformerVizSettingsDetails.h"
#include "MLDeformerModule.h"
#include "MLDeformerModel.h"
#include "MLDeformerEditorModel.h"
#include "MLDeformerEditorModule.h"
#include "MLDeformerVizSettings.h"
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

#define LOCTEXT_NAMESPACE "MLDeformerVizSettingsDetails"

namespace UE::MLDeformer
{
	TSharedRef<IDetailCustomization> FMLDeformerVizSettingsDetails::MakeInstance()
	{
		return MakeShareable(new FMLDeformerVizSettingsDetails());
	}

	bool FMLDeformerVizSettingsDetails::UpdateMemberPointers(const TArray<TWeakObjectPtr<UObject>>& Objects)
	{
		Model = nullptr;
		VizSettings = nullptr;
		EditorModel = nullptr;

		if (Objects.Num() == 1)
		{
			FMLDeformerEditorModule& EditorModule = FModuleManager::GetModuleChecked<FMLDeformerEditorModule>("MLDeformerFrameworkEditor");
			VizSettings = Cast<UMLDeformerVizSettings>(Objects[0]);	
			Model = VizSettings ? Cast<UMLDeformerModel>(VizSettings->GetOuter()) : nullptr;
			EditorModel = Model ? EditorModule.GetModelRegistry().GetEditorModel(Model) : nullptr;
		}

		return (Model != nullptr && VizSettings != nullptr && EditorModel != nullptr);
	}

	void FMLDeformerVizSettingsDetails::CreateCategories()
	{
		SharedCategoryBuilder = &DetailLayoutBuilder->EditCategory("Shared Settings", FText::GetEmpty(), ECategoryPriority::Important);
		TestAssetsCategory = &DetailLayoutBuilder->EditCategory("Test Assets", FText::GetEmpty(), ECategoryPriority::Important);
		LiveSettingsCategory = &DetailLayoutBuilder->EditCategory("Live Settings", FText::GetEmpty(), ECategoryPriority::Important);
		TrainingMeshesCategoryBuilder = &DetailLayoutBuilder->EditCategory("Training Meshes", FText::GetEmpty(), ECategoryPriority::Important);
	}

	void FMLDeformerVizSettingsDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
	{
		DetailLayoutBuilder = &DetailBuilder;

		// Try update the member model, editormodel and viz settings pointers.
		TArray<TWeakObjectPtr<UObject>> Objects;
		DetailLayoutBuilder->GetObjectsBeingCustomized(Objects);
		if (!UpdateMemberPointers(Objects))
		{
			return;
		}

		// Create the categories.
		CreateCategories();

		const bool bShowTrainingData = VizSettings ? (VizSettings->GetVisualizationMode() == EMLDeformerVizMode::TrainingData) : true;
		const bool bShowTestData = VizSettings ? (VizSettings->GetVisualizationMode() == EMLDeformerVizMode::TestData) : true;

		// Shared settings.
		SharedCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, bDrawLabels), UMLDeformerVizSettings::StaticClass());
		SharedCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, LabelHeight), UMLDeformerVizSettings::StaticClass());
		SharedCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, LabelScale), UMLDeformerVizSettings::StaticClass());
		SharedCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, MeshSpacing), UMLDeformerVizSettings::StaticClass());

		// Test Assets.
		TestAssetsCategory->SetCategoryVisibility(bShowTestData);
	
		IDetailPropertyRow& TestAnimRow = TestAssetsCategory->AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, TestAnimSequence), UMLDeformerVizSettings::StaticClass());
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
				Model->GetSkeletalMesh() ? Model->GetSkeletalMesh()->GetSkeleton() : nullptr
			)
		];

		AddTestSequenceErrors();

		const FText AnimErrorText = EditorModel->GetIncompatibleSkeletonErrorText(Model->GetSkeletalMesh(), VizSettings->GetTestAnimSequence());
		FDetailWidgetRow& AnimErrorRow = TestAssetsCategory->AddCustomRow(FText::FromString("AnimSkeletonMisMatchError"))
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

		FIsResetToDefaultVisible IsResetVisible = FIsResetToDefaultVisible::CreateSP(this, &FMLDeformerVizSettingsDetails::IsResetToDefaultDeformerGraphVisible);
		FResetToDefaultHandler ResetHandler = FResetToDefaultHandler::CreateSP(this, &FMLDeformerVizSettingsDetails::OnResetToDefaultDeformerGraph);
		FResetToDefaultOverride ResetOverride = FResetToDefaultOverride::Create(IsResetVisible, ResetHandler);
		TestAssetsCategory->AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, DeformerGraph), UMLDeformerVizSettings::StaticClass()).OverrideResetToDefault(ResetOverride);

		AddDeformerGraphErrors();

		// Show a warning when no deformer graph has been selected.
		UObject* Graph = nullptr;
		TSharedRef<IPropertyHandle> DeformerGraphProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, DeformerGraph), UMLDeformerVizSettings::StaticClass());
		if (DeformerGraphProperty->GetValue(Graph) == FPropertyAccess::Result::Success)
		{
			FDetailWidgetRow& GraphErrorRow = TestAssetsCategory->AddCustomRow(FText::FromString("GraphError"))
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

		AddGroundTruth();

		LiveSettingsCategory->SetCategoryVisibility(bShowTestData);
		LiveSettingsCategory->AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, Weight), UMLDeformerVizSettings::StaticClass());
		LiveSettingsCategory->AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, AnimPlaySpeed), UMLDeformerVizSettings::StaticClass());
		LiveSettingsCategory->AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, TestingFrameNumber), UMLDeformerVizSettings::StaticClass());

		IDetailGroup& HeatMapGroup = LiveSettingsCategory->AddGroup("HeatMap", LOCTEXT("HeatMap", "Heat Map"), false, true);
		HeatMapGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, bShowHeatMap), UMLDeformerVizSettings::StaticClass()));
		HeatMapGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, HeatMapMode), UMLDeformerVizSettings::StaticClass()));
		HeatMapGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, HeatMapMax), UMLDeformerVizSettings::StaticClass()));
		HeatMapGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, GroundTruthLerp), UMLDeformerVizSettings::StaticClass()));

		AddAdditionalSettings();

		IDetailGroup& VisGroup = LiveSettingsCategory->AddGroup("Visibility", LOCTEXT("VisibilityLabel", "Visibility"), false, true);
		VisGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, bDrawLinearSkinnedActor), UMLDeformerVizSettings::StaticClass()));
		VisGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, bDrawMLDeformedActor), UMLDeformerVizSettings::StaticClass()));
		VisGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, bDrawGroundTruthActor), UMLDeformerVizSettings::StaticClass()))
			.EditCondition(VizSettings->HasTestGroundTruth(), nullptr);

		// Training data.
		TrainingMeshesCategoryBuilder->SetCategoryVisibility(bShowTrainingData);
		TrainingMeshesCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, TrainingFrameNumber), UMLDeformerVizSettings::StaticClass());
		TrainingMeshesCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, bDrawDeltas), UMLDeformerVizSettings::StaticClass());
		TrainingMeshesCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, bXRayDeltas), UMLDeformerVizSettings::StaticClass());
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
		if (EditorModel)
		{
			UMeshDeformer* MeshDeformer = EditorModel->LoadDefaultDeformerGraph();
			PropertyHandle->SetValue(MeshDeformer);
		}
	}

	bool FMLDeformerVizSettingsDetails::IsResetToDefaultDeformerGraphVisible(TSharedPtr<IPropertyHandle> PropertyHandle)
	{
		UObject* CurrentGraph = nullptr;
		PropertyHandle->GetValue(CurrentGraph);
		if (CurrentGraph == nullptr)
		{
			return true;
		}

		if (EditorModel)
		{
			// Check if we already assigned the default asset.
			const FAssetData CurrentGraphAssetData(CurrentGraph);
			const FString CurrentPath = CurrentGraphAssetData.ObjectPath.ToString();
			const FString DefaultPath = EditorModel->GetDefaultDeformerGraphAssetPath();
			return (DefaultPath != CurrentPath);
		}

		return false;
	}
}	// namespace UE::MLDeformer

#undef LOCTEXT_NAMESPACE
