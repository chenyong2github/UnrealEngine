// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundFacade.h"

namespace Metasound
{
	/** FWaveShaperNode
	 *
	 *  Delays an audio buffer by a specified amount.
	 */
	class METASOUNDSTANDARDNODES_API FWaveShaperNode : public FNodeFacade
	{
	public:
		/**
		 * Constructor used by the Metasound Frontend.
		 */
		FWaveShaperNode(const FNodeInitData& InitData);
	};
} // namespace Metasound

