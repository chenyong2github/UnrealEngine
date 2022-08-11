// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MLDeformerModelDetails.h"
#include "IDetailCustomization.h"
#include "AssetRegistry/AssetData.h"
#include "Input/Reply.h"

class IDetailLayoutBuilder;
class UVertexDeltaModel;

namespace UE::MLDeformer
{
	class FMLDeformerEditorModel;
}

namespace UE::VertexDeltaModel
{
	class FVertexDeltaEditorModel;

	class VERTEXDELTAMODELEDITOR_API FVertexDeltaModelDetails final
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
		TObjectPtr<UVertexDeltaModel> VertexModel = nullptr;
		FVertexDeltaEditorModel* VertexEditorModel = nullptr;
	};
}	// namespace UE::VertexDeltaModel
