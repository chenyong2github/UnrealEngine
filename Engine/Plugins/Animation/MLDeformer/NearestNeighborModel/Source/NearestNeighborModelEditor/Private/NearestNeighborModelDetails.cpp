// Copyright Epic Games, Inc. All Rights Reserved.

#include "NearestNeighborModelDetails.h"
#include "NearestNeighborEditorModel.h"
#include "NearestNeighborModel.h"
#include "MLDeformerEditorToolkit.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailsView.h"
#include "Widgets/Input/SButton.h"

#define LOCTEXT_NAMESPACE "NearestNeighborModelDetails"

namespace UE::NearestNeighborModel
{
	using namespace UE::MLDeformer;

	TSharedRef<IDetailCustomization> FNearestNeighborModelDetails::MakeInstance()
	{
		return MakeShareable(new FNearestNeighborModelDetails());
	}

	bool FNearestNeighborModelDetails::UpdateMemberPointers(const TArray<TWeakObjectPtr<UObject>>& Objects)
	{
		if (!FMLDeformerGeomCacheModelDetails::UpdateMemberPointers(Objects))
		{
			return false;
		}

		NearestNeighborModel = static_cast<UNearestNeighborModel*>(Model);
		check(NearestNeighborModel);
		NearestNeighborEditorModel = static_cast<FNearestNeighborEditorModel*>(EditorModel);

		return (NearestNeighborModel != nullptr && NearestNeighborEditorModel != nullptr);
	}

	void FNearestNeighborModelDetails::CreateCategories()
	{
		FMLDeformerMorphModelDetails::CreateCategories();
		ClothPartCategoryBuilder = &DetailLayoutBuilder->EditCategory("Cloth Parts", FText::GetEmpty(), ECategoryPriority::Important);
		NearestNeighborCategoryBuilder = &DetailLayoutBuilder->EditCategory("Nearest Neighbors", FText::GetEmpty(), ECategoryPriority::Important);
		KMeansCategoryBuilder = &DetailLayoutBuilder->EditCategory("KMeans Pose Generator", FText::GetEmpty(), ECategoryPriority::Important);
	}

	void FNearestNeighborModelDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
	{
		// Create all the detail categories and add the properties of the base class.
		FMLDeformerMorphModelDetails::CustomizeDetails(DetailBuilder);

		// Training settings.
		TrainingSettingsCategoryBuilder->AddProperty(UNearestNeighborModel::GetInputDimPropertyName());
		TrainingSettingsCategoryBuilder->AddProperty(UNearestNeighborModel::GetHiddenLayerDimsPropertyName());
		TrainingSettingsCategoryBuilder->AddProperty(UNearestNeighborModel::GetOutputDimPropertyName());
		TrainingSettingsCategoryBuilder->AddProperty(UNearestNeighborModel::GetNumEpochsPropertyName());
		TrainingSettingsCategoryBuilder->AddProperty(UNearestNeighborModel::GetBatchSizePropertyName());
		TrainingSettingsCategoryBuilder->AddProperty(UNearestNeighborModel::GetLearningRatePropertyName());

		// Cloth part settings
		ClothPartCategoryBuilder->AddProperty(UNearestNeighborModel::GetClothPartEditorDataPropertyName());
		FText ButtonText = (NearestNeighborModel && NearestNeighborModel->IsClothPartDataValid()) ? LOCTEXT("Update", "Update") : LOCTEXT("Update *", "Update *");
		ClothPartCategoryBuilder->AddCustomRow(FText::FromString(""))
			.WholeRowContent()
			[
				SNew(SButton)
				.Text(ButtonText)
				.HAlign(HAlign_Center)
				.OnClicked_Lambda([this]
				{
					if (NearestNeighborModel != nullptr)
					{
						NearestNeighborModel->UpdateClothPartData();
						if (NearestNeighborEditorModel != nullptr)
						{
							NearestNeighborEditorModel->UpdateNearestNeighborActors();
						}
						EditorModel->GetEditor()->GetModelDetailsView()->ForceRefresh();
					}
					return FReply::Handled();
				})
			];

		// Nearest Neighbor settings
		NearestNeighborCategoryBuilder->AddProperty(GET_MEMBER_NAME_STRING_CHECKED(UNearestNeighborModel, DecayFactor));
		NearestNeighborCategoryBuilder->AddProperty(UNearestNeighborModel::GetNearestNeighborDataPropertyName());
		ButtonText = (NearestNeighborModel && NearestNeighborModel->IsNearestNeighborDataValid()) ? LOCTEXT("Update", "Update") : LOCTEXT("Update *", "Update *");

		NearestNeighborCategoryBuilder->AddCustomRow(FText::FromString(""))
			.WholeRowContent()
			[
				SNew(SButton)
				.Text(ButtonText)
				.HAlign(HAlign_Center)
				.OnClicked_Lambda([this]
				{
					if (NearestNeighborEditorModel != nullptr)
					{
						NearestNeighborEditorModel->UpdateNearestNeighborData();
						EditorModel->GetEditor()->GetModelDetailsView()->ForceRefresh();
					}
					return FReply::Handled();
				})
			];

		KMeansCategoryBuilder->AddProperty(GET_MEMBER_NAME_STRING_CHECKED(UNearestNeighborModel, SourceSkeletons));
		KMeansCategoryBuilder->AddProperty(GET_MEMBER_NAME_STRING_CHECKED(UNearestNeighborModel, NumClusters));
		KMeansCategoryBuilder->AddCustomRow(FText::FromString(""))
			.WholeRowContent()
			[
				SNew(SButton)
				.Text(FText::FromString("Cluster"))
				.HAlign(HAlign_Center)
				.OnClicked_Lambda([this]
				{
					NearestNeighborEditorModel->KMeansClusterPoses();
					return FReply::Handled();
				})
			];

		MorphTargetCategoryBuilder->AddCustomRow(FText::FromString(""))
			.WholeRowContent()
			[
				SNew(SButton)
				.Text(FText::FromString("Update"))
				.HAlign(HAlign_Center)
				.OnClicked_Lambda([this]
				{
					if (NearestNeighborEditorModel != nullptr)
					{
						NearestNeighborEditorModel->InitMorphTargets();
						EditorModel->GetEditor()->GetModelDetailsView()->ForceRefresh();
					}
					return FReply::Handled();
				})
			];
	}
}	// namespace UE::NearestNeighborModel

#undef LOCTEXT_NAMESPACE
