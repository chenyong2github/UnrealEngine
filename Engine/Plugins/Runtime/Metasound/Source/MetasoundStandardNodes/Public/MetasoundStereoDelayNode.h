// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundFacade.h"

namespace Metasound
{
	/** FStereoDelayNode
	 *
	 *  Delays an audio buffer by a specified amount.
	 */
	class METASOUNDSTANDARDNODES_API FStereoDelayNode : public FNodeFacade
	{
	public:
		/**
		 * Constructor used by the Metasound Frontend.
		 */
		FStereoDelayNode(const FNodeInitData& InitData);
	};
} // namespace Metasound

