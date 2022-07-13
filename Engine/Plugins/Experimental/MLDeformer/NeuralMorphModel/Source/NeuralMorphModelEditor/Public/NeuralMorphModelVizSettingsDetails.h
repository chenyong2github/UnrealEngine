// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MLDeformerVizSettingsDetails.h"
#include "IDetailCustomization.h"
#include "AssetRegistry/AssetData.h"
#include "PropertyHandle.h"

class IDetailLayoutBuilder;
class USkeleton;
class UNeuralMorphModel;
class UNeuralMorphModelVizSettings;

namespace UE::NeuralMorphModel
{
	class NEURALMORPHMODELEDITOR_API FNeuralMorphModelVizSettingsDetails final
		: public UE::MLDeformer::FMLDeformerVizSettingsDetails
	{
	public:
		/** Makes a new instance of this detail layout class for a specific detail view requesting it. */
		static TSharedRef<IDetailCustomization> MakeInstance();

		// FMLDeformerVizSettingsDetails overrides.
		bool UpdateMemberPointers(const TArray<TWeakObjectPtr<UObject>>& Objects) override;
		void AddGroundTruth() override;
		void AddAdditionalSettings() override;
		// ~END FMLDeformerVizSettingsDetails overrides.

	protected:
		bool IsMorphTargetsEnabled() const;

		TObjectPtr<UNeuralMorphModel> NeuralMorphModel = nullptr;
		TObjectPtr<UNeuralMorphModelVizSettings> NeuralMorphVizSettings = nullptr;
	};
}	// namespace UE::NeuralMorphModel
