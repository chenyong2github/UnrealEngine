// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MLDeformerModelDetails.h"
#include "IDetailCustomization.h"
#include "AssetRegistry/AssetData.h"
#include "Input/Reply.h"

class IDetailLayoutBuilder;
class UNeuralMorphModel;

namespace UE::MLDeformer
{
	class FMLDeformerEditorModel;
}

namespace UE::NeuralMorphModel
{
	class FNeuralMorphEditorModel;

	class NEURALMORPHMODELEDITOR_API FNeuralMorphModelDetails final
		: public UE::MLDeformer::FMLDeformerModelDetails
	{
	public:
		/** Makes a new instance of this detail layout class for a specific detail view requesting it. */
		static TSharedRef<IDetailCustomization> MakeInstance();

		// ILayoutDetails overrides.
		void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
		// ~END ILayoutDetails overrides.

		// FMLDeformerModelDetails overrides.
		bool UpdateMemberPointers(const TArray<TWeakObjectPtr<UObject>>& Objects) override;
		void AddTargetMesh() override;
		void AddAnimSequenceErrors() override;
		// ~END FMLDeformerModelDetails overrides.

	protected:
		TObjectPtr<UNeuralMorphModel> NeuralMorphModel = nullptr;
		FNeuralMorphEditorModel* NeuralMorphEditorModel = nullptr;
	};
}	// namespace UE::NeuralMorphModel
