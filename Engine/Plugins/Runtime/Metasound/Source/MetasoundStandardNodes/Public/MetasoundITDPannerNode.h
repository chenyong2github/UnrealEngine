// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundFacade.h"

namespace Metasound
{
	/** FITDPannerNode
	 *
	 *  Pans an audio buffer using an ITD method (Interaural time delay)
	 */
	class METASOUNDSTANDARDNODES_API FITDPannerNode  : public FNodeFacade
	{
	public:
		/**
		 * Constructor used by the Metasound Frontend.
		 */
		FITDPannerNode(const FNodeInitData& InitData);
	};
} // namespace Metasound

