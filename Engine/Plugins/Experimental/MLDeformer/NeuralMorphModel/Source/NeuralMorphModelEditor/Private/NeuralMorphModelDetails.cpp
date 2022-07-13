// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralMorphModelDetails.h"
#include "NeuralMorphEditorModel.h"
#include "NeuralMorphModel.h"
#include "MLDeformerModule.h"
#include "MLDeformerAsset.h"
#include "MLDeformerModel.h"
#include "MLDeformerEditorModel.h"
#include "MLDeformerEditorModule.h"
#include "MLDeformerGeomCacheHelpers.h"
#include "NeuralNetwork.h"
#include "GeometryCache.h"
#include "GeometryCacheTrack.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "PropertyCustomizationHelpers.h"
#include "AssetRegistry/AssetData.h"
#include "Modules/ModuleManager.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailsView.h"
#include "IDetailGroup.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "DetailLayoutBuilder.h"
#include "SWarningOrErrorBox.h"

#define LOCTEXT_NAMESPACE "NeuralMorphModelDetails"

namespace UE::NeuralMorphModel
{
	using namespace UE::MLDeformer;

	TSharedRef<IDetailCustomization> FNeuralMorphModelDetails::MakeInstance()
	{
		return MakeShareable(new FNeuralMorphModelDetails());
	}

	bool FNeuralMorphModelDetails::UpdateMemberPointers(const TArray<TWeakObjectPtr<UObject>>& Objects)
	{
		if (!FMLDeformerModelDetails::UpdateMemberPointers(Objects))
		{
			return false;
		}

		NeuralMorphModel = Cast<UNeuralMorphModel>(Model);
		check(NeuralMorphModel);
		NeuralMorphEditorModel = static_cast<FNeuralMorphEditorModel*>(EditorModel);

		return (NeuralMorphModel != nullptr && NeuralMorphEditorModel != nullptr);
	}

	void FNeuralMorphModelDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
	{
		// Create all the detail categories and add the properties of the base class.
		FMLDeformerModelDetails::CustomizeDetails(DetailBuilder);

		// Training settings.
		SettingsCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(UNeuralMorphModel, NumMorphTargetsPerBone));
		SettingsCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(UNeuralMorphModel, NumHiddenLayers));
		SettingsCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(UNeuralMorphModel, NumNeuronsPerLayer));
		SettingsCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(UNeuralMorphModel, NumIterations));
		SettingsCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(UNeuralMorphModel, BatchSize));
		SettingsCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(UNeuralMorphModel, LearningRate));
		SettingsCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(UNeuralMorphModel, RegularizationFactor));
		SettingsCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(UNeuralMorphModel, MorphTargetErrorTolerance));
	}

	void FNeuralMorphModelDetails::AddBaseMeshErrors()
	{
		// Check if the base mesh matches the target mesh vertex count.
		FText ErrorText = GetGeomCacheVertexErrorText(Model->SkeletalMesh, NeuralMorphModel->GetGeometryCache(), FText::FromString("Base Mesh"), FText::FromString("Target Mesh"));
		BaseMeshCategoryBuilder->AddCustomRow(FText::FromString("BaseMeshError"))
			.Visibility(!ErrorText.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed)
			.WholeRowContent()
			[
				SNew(SBox)
				.Padding(FMargin(0.0f, 4.0f))
				[
					SNew(SWarningOrErrorBox)
					.MessageStyle(EMessageStyle::Error)
					.Message(ErrorText)
				]
			];
	}
	
	void FNeuralMorphModelDetails::AddAnimSequenceErrors()
	{
		const FText WarningText = GetGeomCacheAnimSequenceErrorText(NeuralMorphModel->GetGeometryCache(), Model->GetAnimSequence());
		BaseMeshCategoryBuilder->AddCustomRow(FText::FromString("AnimSeqWarning"))
			.Visibility(!WarningText.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed)
			.WholeRowContent()
			[
				SNew(SBox)
				.Padding(FMargin(0.0f, 4.0f))
				[
					SNew(SWarningOrErrorBox)
					.MessageStyle(EMessageStyle::Warning)
					.Message(WarningText)
				]
			];
	}

	void FNeuralMorphModelDetails::AddTargetMesh()
	{
		TargetMeshCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(UNeuralMorphModel, GeometryCache));

		const FText TargetMeshErrorText = GetGeomCacheErrorText(NeuralMorphModel->GetSkeletalMesh(), NeuralMorphModel->GetGeometryCache());
		TargetMeshCategoryBuilder->AddCustomRow(FText::FromString("TargetMeshError"))
			.Visibility(!TargetMeshErrorText.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed)
			.WholeRowContent()
			[
				SNew(SBox)
				.Padding(FMargin(0.0f, 4.0f))
				[
					SNew(SWarningOrErrorBox)
					.MessageStyle(EMessageStyle::Error)
					.Message(TargetMeshErrorText)
				]
			];

		const FText ChangedErrorText = EditorModel->GetTargetAssetChangedErrorText();
		TargetMeshCategoryBuilder->AddCustomRow(FText::FromString("TargetMeshChangedError"))
			.Visibility(!ChangedErrorText.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed)
			.WholeRowContent()
			[
				SNew(SBox)
				.Padding(FMargin(0.0f, 4.0f))
				[
					SNew(SWarningOrErrorBox)
					.MessageStyle(EMessageStyle::Error)
					.Message(ChangedErrorText)
				]
			];

		// Show meshes that have no matching geometry track list.
		const FText MeshMappingErrorList = GetGeomCacheMeshMappingErrorText(Model->GetSkeletalMesh(), NeuralMorphModel->GetGeometryCache());
		FString GeomTrackNameList;
		if (!MeshMappingErrorList.IsEmpty())
		{
			UGeometryCache* GeomCache = NeuralMorphModel->GetGeometryCache();
			for (int32 Index = 0; Index < GeomCache->Tracks.Num(); ++Index)
			{
				GeomTrackNameList += GeomCache->Tracks[Index]->GetName();
				if (Index < GeomCache->Tracks.Num() - 1)
				{
					GeomTrackNameList += TEXT("\n");
				}
			}
		}

		FText MeshMappingErrorFull = FText::Format(
			LOCTEXT("MeshMappingError", "No matching GeomCache Tracks names found for meshes:\n{0}\n\nGeomCache Track List:\n{1}"), 
			MeshMappingErrorList,
			FText::FromString(GeomTrackNameList));

		TargetMeshCategoryBuilder->AddCustomRow(FText::FromString("MeshMappingError"))
			.Visibility(!MeshMappingErrorList.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed)
			.WholeRowContent()
			[
				SNew(SBox)
				.Padding(FMargin(0.0f, 4.0f))
				[
					SNew(SWarningOrErrorBox)
					.MessageStyle(EMessageStyle::Error)
					.Message(MeshMappingErrorFull)
				]
			];
	}
}	// namespace UE::NeuralMorphModel

#undef LOCTEXT_NAMESPACE
