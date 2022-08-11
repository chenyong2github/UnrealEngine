// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MLDeformerModelDetails.h"
#include "IDetailCustomization.h"
#include "AssetRegistry/AssetData.h"
#include "Input/Reply.h"

class IDetailLayoutBuilder;
class ULegacyVertexDeltaModel;

namespace UE::MLDeformer
{
	class FMLDeformerEditorModel;
}

namespace UE::LegacyVertexDeltaModel
{
	class FLegacyVertexDeltaEditorModel;

	class LEGACYVERTEXDELTAMODELEDITOR_API FLegacyVertexDeltaModelDetails final
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
		TObjectPtr<ULegacyVertexDeltaModel> VertexModel = nullptr;
		FLegacyVertexDeltaEditorModel* VertexEditorModel = nullptr;
	};
}	// namespace UE::LegacyVertexDeltaModel
