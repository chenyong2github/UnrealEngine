// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MLDeformerGeomCacheModelDetails.h"

namespace UE::VertexDeltaModel
{
	class VERTEXDELTAMODELEDITOR_API FVertexDeltaModelDetails
		: public UE::MLDeformer::FMLDeformerGeomCacheModelDetails
	{
	public:
		/** Makes a new instance of this detail layout class for a specific detail view requesting it. */
		static TSharedRef<IDetailCustomization> MakeInstance();

		// ILayoutDetails overrides.
		void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
		// ~END ILayoutDetails overrides.
	};
}	// namespace UE::VertexDeltaModel
