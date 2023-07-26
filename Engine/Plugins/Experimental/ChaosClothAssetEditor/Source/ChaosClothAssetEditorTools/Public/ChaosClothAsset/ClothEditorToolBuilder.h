// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"

namespace UE::Chaos::ClothAsset
{
	enum class EClothPatternVertexType : uint8;

	// Interface for all UInteractiveToolBuilders for Cloth Editor tools
	class IChaosClothAssetEditorToolBuilder
	{
	public:
		virtual ~IChaosClothAssetEditorToolBuilder() {}

		/** Returns all Construction View modes that this tool can operate in. The first element should be the preferred mode to switch to if necessary. */
		virtual void GetSupportedViewModes(TArray<EClothPatternVertexType>& Modes) const = 0;
	};
}


