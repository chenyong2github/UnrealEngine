// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MLDeformerVizSettingsDetails.h"
#include "IDetailCustomization.h"
#include "AssetRegistry/AssetData.h"
#include "PropertyHandle.h"

class IDetailLayoutBuilder;
class USkeleton;
class ULegacyVertexDeltaModel;
class ULegacyVertexDeltaModelVizSettings;

namespace UE::LegacyVertexDeltaModel
{
	class LEGACYVERTEXDELTAMODELEDITOR_API FLegacyVertexDeltaModelVizSettingsDetails final
		: public UE::MLDeformer::FMLDeformerVizSettingsDetails
	{
	public:
		/** Makes a new instance of this detail layout class for a specific detail view requesting it. */
		static TSharedRef<IDetailCustomization> MakeInstance();

		// FMLDeformerVizSettingsDetails overrides.
		bool UpdateMemberPointers(const TArray<TWeakObjectPtr<UObject>>& Objects) override;
		void AddGroundTruth() override;
		// ~END FMLDeformerVizSettingsDetails overrides.

	protected:
		TObjectPtr<ULegacyVertexDeltaModel> VertexDeltaModel = nullptr;
		TObjectPtr<ULegacyVertexDeltaModelVizSettings> VertexDeltaVizSettings = nullptr;
	};
}	// namespace UE::LegacyVertexDeltaModel
