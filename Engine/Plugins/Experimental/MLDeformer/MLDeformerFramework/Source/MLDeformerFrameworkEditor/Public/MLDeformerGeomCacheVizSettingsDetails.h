// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MLDeformerVizSettingsDetails.h"
#include "IDetailCustomization.h"

class IDetailLayoutBuilder;
class UMLDeformerGeomCacheModel;
class UMLDeformerGeomCacheVizSettings;

namespace UE::MLDeformer
{
	class MLDEFORMERFRAMEWORKEDITOR_API FMLDeformerGeomCacheVizSettingsDetails
		: public FMLDeformerVizSettingsDetails
	{
	public:
		// FMLDeformerVizSettingsDetails overrides.
		virtual bool UpdateMemberPointers(const TArray<TWeakObjectPtr<UObject>>& Objects) override;
		virtual void AddGroundTruth() override;
		// ~END FMLDeformerVizSettingsDetails overrides.

	protected:
		TObjectPtr<UMLDeformerGeomCacheModel> GeomCacheModel = nullptr;
		TObjectPtr<UMLDeformerGeomCacheVizSettings> GeomCacheVizSettings = nullptr;
	};
}	// namespace UE::MLDeformer
