// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformerAssetDetails.h"
#include "MLDeformer.h"
#include "MLDeformerAsset.h"
#include "NeuralNetwork.h"

#include "GeometryCache.h"
#include "GeometryCacheTrack.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "PropertyCustomizationHelpers.h"
#include "AssetRegistry/AssetData.h"

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailsView.h"
#include "IDetailGroup.h"

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Layout/SBox.h"
#include "DetailLayoutBuilder.h"

#include "SWarningOrErrorBox.h"
#include "Widgets/Input/SButton.h"

#define LOCTEXT_NAMESPACE "MLDeformerAssetDetails"


TSharedRef<IDetailCustomization> FMLDeformerAssetDetails::MakeInstance()
{
	return MakeShareable(new FMLDeformerAssetDetails());
}

void FMLDeformerAssetDetails::CustomizeDetails(class IDetailLayoutBuilder& DetailBuilder)
{
	DetailLayoutBuilder = &DetailBuilder;

	// Get the deformer asset.
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	UMLDeformerAsset* DeformerAsset = nullptr;
	if (Objects.Num() == 1)
	{
		DeformerAsset = Cast<UMLDeformerAsset>(Objects[0]);
	}

	// Base mesh details.
	IDetailCategoryBuilder& BaseMeshCategoryBuilder = DetailBuilder.EditCategory("Base Mesh", FText::GetEmpty(), ECategoryPriority::Important);
	BaseMeshCategoryBuilder.AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerAsset, SkeletalMesh));
	if (DeformerAsset)
	{
		// Check if the base mesh matches the target mesh vertex count.
		const FText ErrorText = DeformerAsset->GetVertexErrorText(DeformerAsset->SkeletalMesh, DeformerAsset->GeometryCache, FText::FromString("Base Mesh"), FText::FromString("Target Mesh"));
		FDetailWidgetRow& ErrorRow = BaseMeshCategoryBuilder.AddCustomRow(FText::FromString("BaseMeshError"))
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

		// Check if the vertex counts of our asset have changed.
		const FText ChangedErrorText = DeformerAsset->GetBaseAssetChangedErrorText();
		FDetailWidgetRow& ChangedErrorRow = BaseMeshCategoryBuilder.AddCustomRow(FText::FromString("BaseMeshChangedError"))
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

		// Check if our skeletal mesh's imported model contains a list of mesh infos. If not, we need to reimport it as it is an older asset.
		const FText NeedsReimportErrorText = DeformerAsset->GetSkeletalMeshNeedsReimportErrorText();
		FDetailWidgetRow& NeedsReimportErrorRow = BaseMeshCategoryBuilder.AddCustomRow(FText::FromString("BaseMeshNeedsReimportError"))
			.Visibility(!NeedsReimportErrorText.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed)
			.WholeRowContent()
			[
				SNew(SBox)
				.Padding(FMargin(0.0f, 4.0f))
				[
					SNew(SWarningOrErrorBox)
					.MessageStyle(EMessageStyle::Error)
					.Message(NeedsReimportErrorText)
				]
			];
		
		// Show meshes that have no matching geometry track list.
		const FText MeshMappingErrorList = DeformerAsset->GetMeshMappingErrorText();
		FString GeomTrackNameList;
		if (!MeshMappingErrorList.IsEmpty())
		{
			UGeometryCache* GeomCache = DeformerAsset->GetGeometryCache();
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
		FDetailWidgetRow& MeshMappingErrorRow = BaseMeshCategoryBuilder.AddCustomRow(FText::FromString("MeshMappingError"))
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

	// Animation sequence.
	IDetailPropertyRow& AnimRow = BaseMeshCategoryBuilder.AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerAsset, AnimSequence));
	AnimRow.CustomWidget()
	.NameContent()
	[
		AnimRow.GetPropertyHandle()->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		SNew(SObjectPropertyEntryBox)
		.PropertyHandle(AnimRow.GetPropertyHandle())
		.AllowedClass(UAnimSequence::StaticClass())
		.ObjectPath(DeformerAsset ? DeformerAsset->GetAnimSequence()->GetPathName() : FString())
		.ThumbnailPool(DetailBuilder.GetThumbnailPool())
		.OnShouldFilterAsset(
			this, 
			&FMLDeformerAssetDetails::FilterAnimSequences, 
			DeformerAsset->GetSkeletalMesh() ? DeformerAsset->GetSkeletalMesh()->GetSkeleton() : nullptr
		)
	];

	if (DeformerAsset)
	{
		const FText WarningText = DeformerAsset->GetAnimSequenceErrorText(DeformerAsset->GetGeometryCache(), DeformerAsset->GetAnimSequence());
		FDetailWidgetRow& WarningRow = BaseMeshCategoryBuilder.AddCustomRow(FText::FromString("AnimSeqWarning"))
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

		const FText AnimErrorText = DeformerAsset->GetIncompatibleSkeletonErrorText(DeformerAsset->GetSkeletalMesh(), DeformerAsset->AnimSequence);
		FDetailWidgetRow& AnimErrorRow = BaseMeshCategoryBuilder.AddCustomRow(FText::FromString("AnimSkeletonMisMatchError"))
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

	// Target mesh details.
	IDetailCategoryBuilder& TargetMeshCategoryBuilder = DetailBuilder.EditCategory("Target Mesh", FText::GetEmpty(), ECategoryPriority::Important);
	TargetMeshCategoryBuilder.AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerAsset, GeometryCache));
	if (DeformerAsset)
	{
		const FText ErrorText = DeformerAsset->GetGeomCacheErrorText(DeformerAsset->GetGeometryCache());
		FDetailWidgetRow& ErrorRow = TargetMeshCategoryBuilder.AddCustomRow(FText::FromString("TargetMeshError"))
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

		const FText ChangedErrorText = DeformerAsset->GetTargetAssetChangedErrorText();
		FDetailWidgetRow& ChangedErrorRow = BaseMeshCategoryBuilder.AddCustomRow(FText::FromString("TargetMeshChangedError"))
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
	}
	TargetMeshCategoryBuilder.AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerAsset, AlignmentTransform));

	// Input and output.
	IDetailCategoryBuilder& InputOutputCategoryBuilder = DetailBuilder.EditCategory("Inputs and Output", FText::GetEmpty(), ECategoryPriority::Important);
	InputOutputCategoryBuilder.AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerAsset, TrainingInputs));
	if (DeformerAsset)
	{
		const FText ErrorText = DeformerAsset->GetInputsErrorText();
		FDetailWidgetRow& ErrorRow = InputOutputCategoryBuilder.AddCustomRow(FText::FromString("InputsError"))
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

	InputOutputCategoryBuilder.AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerAsset, DeltaCutoffLength));

	// Bone include list group.
	IDetailGroup& BoneIncludeGroup = InputOutputCategoryBuilder.AddGroup("BoneIncludeGroup", LOCTEXT("BoneIncludeGroup", "Bones"), false, false);
	BoneIncludeGroup.AddWidgetRow()
		.ValueContent()
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Text(LOCTEXT("AnimatedBonesButton", "Animated Bones Only"))
			.OnClicked(FOnClicked::CreateSP(this, &FMLDeformerAssetDetails::OnFilterAnimatedBonesOnly, DeformerAsset))
			.IsEnabled_Lambda([DeformerAsset](){ return (DeformerAsset->GetTrainingInputs() == ETrainingInputs::BonesAndCurves) || (DeformerAsset->GetTrainingInputs() == ETrainingInputs::BonesOnly); })
		];
	BoneIncludeGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerAsset, BoneIncludeList)));

	// Curve include list group.
	IDetailGroup& CurveIncludeGroup = InputOutputCategoryBuilder.AddGroup("CurveIncludeGroup", LOCTEXT("CurveIncludeGroup", "Curves"), false, false);
	CurveIncludeGroup.AddWidgetRow()
		.ValueContent()
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Text(LOCTEXT("AnimatedCurvesButton", "Animated Curves Only"))
			.OnClicked(FOnClicked::CreateSP(this, &FMLDeformerAssetDetails::OnFilterAnimatedCurvesOnly, DeformerAsset))
			.IsEnabled_Lambda([DeformerAsset](){ return (DeformerAsset->GetTrainingInputs() == ETrainingInputs::BonesAndCurves) || (DeformerAsset->GetTrainingInputs() == ETrainingInputs::CurvesOnly); })
		];
	CurveIncludeGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerAsset, CurveIncludeList)));


	// Training settings.
	IDetailCategoryBuilder& SettingsCategoryBuilder = DetailBuilder.EditCategory("Training Settings", FText::GetEmpty(), ECategoryPriority::Important);
	SettingsCategoryBuilder.AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerAsset, NumHiddenLayers));
	SettingsCategoryBuilder.AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerAsset, NumNeuronsPerLayer));
	SettingsCategoryBuilder.AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerAsset, BatchSize));
	SettingsCategoryBuilder.AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerAsset, Epochs));
	SettingsCategoryBuilder.AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerAsset, EpochsWithDecay));
	SettingsCategoryBuilder.AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerAsset, MaxTrainingFrames));
	SettingsCategoryBuilder.AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerAsset, CacheSizeInMegabytes));

	// Show a warning when no neural network has been set.
	if (DeformerAsset)
	{		
		UNeuralNetwork* NeuralNetwork = DeformerAsset->GetInferenceNeuralNetwork();
		FDetailWidgetRow& NeuralNetErrorRow = SettingsCategoryBuilder.AddCustomRow(FText::FromString("NeuralNetError"))
			.Visibility((NeuralNetwork == nullptr) ? EVisibility::Visible : EVisibility::Collapsed)
			.WholeRowContent()
			[
				SNew(SBox)
				.Padding(FMargin(0.0f, 4.0f))
				[
					SNew(SWarningOrErrorBox)
					.MessageStyle(EMessageStyle::Warning)
					.Message(FText::FromString("Model still needs to be trained."))
				]
			];

		// Check if our network is compatible with the skeletal mesh.
		if (DeformerAsset->GetSkeletalMesh() && NeuralNetwork)
		{
			FDetailWidgetRow& NeuralNetIncompatibleErrorRow = SettingsCategoryBuilder.AddCustomRow(FText::FromString("NeuralNetIncompatibleError"))
				.Visibility(!DeformerAsset->IsCompatibleWithNeuralNet() ? EVisibility::Visible : EVisibility::Collapsed)
				.WholeRowContent()
				[
					SNew(SBox)
					.Padding(FMargin(0.0f, 4.0f))
					[
						SNew(SWarningOrErrorBox)
						.MessageStyle(EMessageStyle::Error)
						.Message(FText::FromString("Trained neural network is incompatible with selected SkeletalMesh."))
					]
				];
		}
	}

	SettingsCategoryBuilder.AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerAsset, DecayFunction));
	SettingsCategoryBuilder.AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerAsset, DecayRate));
	SettingsCategoryBuilder.AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerAsset, LearningRate));
	SettingsCategoryBuilder.AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerAsset, ActivationFunction));
	SettingsCategoryBuilder.AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerAsset, LossFunction));
	SettingsCategoryBuilder.AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerAsset, ShrinkageSpeed));
	SettingsCategoryBuilder.AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerAsset, ShrinkageThreshold));
	SettingsCategoryBuilder.AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerAsset, NoiseAmount));
	if (DeformerAsset)
	{
		// Check if the noise amount is greater than 0, if so, show a warning about slow training times.
		const FText WarningText = LOCTEXT("NoiseWarning", "Adding noise will disable caching, which will slow down training a lot.");
		FDetailWidgetRow& WarningRow = SettingsCategoryBuilder.AddCustomRow(FText::FromString("NoiseWarning"), true)
			.Visibility((DeformerAsset->GetNoiseAmount() > 0.0f) ? EVisibility::Visible : EVisibility::Collapsed)
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

	SettingsCategoryBuilder.AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerAsset, DeviceType));
}

bool FMLDeformerAssetDetails::FilterAnimSequences(const FAssetData& AssetData, USkeleton* Skeleton)
{
	if (Skeleton && Skeleton->IsCompatibleSkeletonByAssetData(AssetData))
	{
		return false;
	}

	return true;
}

FReply FMLDeformerAssetDetails::OnFilterAnimatedBonesOnly(UMLDeformerAsset* DeformerAsset) const
{
	DeformerAsset->InitBoneIncludeListToAnimatedBonesOnly();
	DetailLayoutBuilder->ForceRefreshDetails();
	return FReply::Handled();
}

FReply FMLDeformerAssetDetails::OnFilterAnimatedCurvesOnly(UMLDeformerAsset* DeformerAsset) const
{
	DeformerAsset->InitCurveIncludeListToAnimatedCurvesOnly();
	DetailLayoutBuilder->ForceRefreshDetails();
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
