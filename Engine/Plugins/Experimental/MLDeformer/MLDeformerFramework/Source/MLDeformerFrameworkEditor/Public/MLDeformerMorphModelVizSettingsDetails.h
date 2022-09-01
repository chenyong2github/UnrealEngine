// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MLDeformerGeomCacheVizSettingsDetails.h"
#include "IDetailCustomization.h"

class IDetailLayoutBuilder;
class UMLDeformerMorphModel;
class UMLDeformerMorphModelVizSettings;

namespace UE::MLDeformer
{
	class MLDEFORMERFRAMEWORKEDITOR_API FMLDeformerMorphModelVizSettingsDetails
		: public FMLDeformerGeomCacheVizSettingsDetails
	{
	public:
		// FMLDeformerVizSettingsDetails overrides.
		virtual bool UpdateMemberPointers(const TArray<TWeakObjectPtr<UObject>>& Objects) override;
		virtual void AddAdditionalSettings() override;
		// ~END FMLDeformerVizSettingsDetails overrides.

	protected:
		bool IsMorphTargetsEnabled() const;

		TObjectPtr<UMLDeformerMorphModel> MorphModel = nullptr;
		TObjectPtr<UMLDeformerMorphModelVizSettings> MorphModelVizSettings = nullptr;
	};
}	// namespace UE::MLDeformer
