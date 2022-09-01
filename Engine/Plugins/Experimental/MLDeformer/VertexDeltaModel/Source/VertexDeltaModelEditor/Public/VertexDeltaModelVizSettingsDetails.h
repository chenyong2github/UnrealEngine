// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MLDeformerGeomCacheVizSettingsDetails.h"

namespace UE::VertexDeltaModel
{
	class VERTEXDELTAMODELEDITOR_API FVertexDeltaModelVizSettingsDetails
		: public UE::MLDeformer::FMLDeformerGeomCacheVizSettingsDetails
	{
	public:
		/** Makes a new instance of this detail layout class for a specific detail view requesting it. */
		static TSharedRef<IDetailCustomization> MakeInstance()
		{
			return MakeShareable(new FVertexDeltaModelVizSettingsDetails());
		}
	};
}	// namespace UE::VertexDeltaModel
