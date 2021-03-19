// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundFacade.h"

namespace Metasound
{
	/** FStereoPannerNode
	 *
	 *  Pans an input audio signal to stereo output
	 */
	class METASOUNDSTANDARDNODES_API FStereoPannerNode : public FNodeFacade
	{
	public:
		/**
		 * Constructor used by the Metasound Frontend.
		 */
		FStereoPannerNode(const FNodeInitData& InitData);
	};
} // namespace Metasound

