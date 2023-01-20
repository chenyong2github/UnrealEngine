// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformerMorphModelDetails.h"
#include "MLDeformerMorphModelEditorModel.h"
#include "MLDeformerMorphModel.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailGroup.h"

#define LOCTEXT_NAMESPACE "MLDeformerMorphModelDetails"

namespace UE::MLDeformer
{
	bool FMLDeformerMorphModelDetails::UpdateMemberPointers(const TArray<TWeakObjectPtr<UObject>>& Objects)
	{
		if (!FMLDeformerGeomCacheModelDetails::UpdateMemberPointers(Objects))
		{
			return false;
		}

		MorphModel = Cast<UMLDeformerMorphModel>(Model);
		check(MorphModel);
		MorphModelEditorModel = static_cast<FMLDeformerMorphModelEditorModel*>(EditorModel);

		return (MorphModel != nullptr && MorphModelEditorModel != nullptr);
	}

	void FMLDeformerMorphModelDetails::CreateCategories()
	{
		FMLDeformerGeomCacheModelDetails::CreateCategories();
		MorphTargetCategoryBuilder = &DetailLayoutBuilder->EditCategory("Morph Targets", FText::GetEmpty(), ECategoryPriority::Important);
	}

	void FMLDeformerMorphModelDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
	{
		// Create all the detail categories and add the properties of the base class.
		FMLDeformerGeomCacheModelDetails::CustomizeDetails(DetailBuilder);

		MorphTargetCategoryBuilder->AddProperty(DetailLayoutBuilder->GetProperty(UMLDeformerMorphModel::GetIncludeMorphTargetNormalsPropertyName(), UMLDeformerMorphModel::StaticClass()));

		IDetailGroup& CompressionGroup = MorphTargetCategoryBuilder->AddGroup("Compression", LOCTEXT("MorphCompressionGroupLabel", "Compression"), false, true);
		CompressionGroup.AddPropertyRow(DetailLayoutBuilder->GetProperty(UMLDeformerMorphModel::GetMorphDeltaZeroThresholdPropertyName(), UMLDeformerMorphModel::StaticClass()));
		CompressionGroup.AddPropertyRow(DetailLayoutBuilder->GetProperty(UMLDeformerMorphModel::GetMorphCompressionLevelPropertyName(), UMLDeformerMorphModel::StaticClass()));

		IDetailGroup& MaskGroup = MorphTargetCategoryBuilder->AddGroup("Mask", LOCTEXT("MorphMaskGroupLabel", "Masking"), false, false);
		MaskGroup.AddPropertyRow(DetailLayoutBuilder->GetProperty(UMLDeformerMorphModel::GetMaskChannelPropertyName(), UMLDeformerMorphModel::StaticClass()));
		MaskGroup.AddPropertyRow(DetailLayoutBuilder->GetProperty(UMLDeformerMorphModel::GetInvertMaskChannelPropertyName(), UMLDeformerMorphModel::StaticClass()));

		MorphTargetCategoryBuilder->AddProperty(DetailLayoutBuilder->GetProperty(UMLDeformerMorphModel::GetQualityLevelsPropertyName(), UMLDeformerMorphModel::StaticClass()));
	}
}	// namespace UE::MLDeformer

#undef LOCTEXT_NAMESPACE
