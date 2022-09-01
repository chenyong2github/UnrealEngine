// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MLDeformerGeomCacheModelDetails.h"
#include "IDetailCustomization.h"

class IDetailLayoutBuilder;
class UMLDeformerMorphModel;

namespace UE::MLDeformer
{
	class FMLDeformerMorphModelEditorModel;

	class MLDEFORMERFRAMEWORKEDITOR_API FMLDeformerMorphModelDetails
		: public FMLDeformerGeomCacheModelDetails
	{
	public:
		// ILayoutDetails overrides.
		virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
		// ~END ILayoutDetails overrides.

		// FMLDeformerModelDetails overrides.
		virtual void CreateCategories() override;
		virtual bool UpdateMemberPointers(const TArray<TWeakObjectPtr<UObject>>& Objects) override;
		// ~END FMLDeformerModelDetails overrides.

	protected:
		TObjectPtr<UMLDeformerMorphModel> MorphModel = nullptr;
		FMLDeformerMorphModelEditorModel* MorphModelEditorModel = nullptr;
		IDetailCategoryBuilder* MorphTargetCategoryBuilder = nullptr;
	};
}	// namespace UE::MLDeformer
