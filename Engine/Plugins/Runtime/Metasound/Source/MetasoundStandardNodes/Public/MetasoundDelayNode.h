// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundFacade.h"

namespace Metasound
{
	/** FDelayNode
	 *
	 *  Delays an audio buffer by a specified amount.
	 */
	class METASOUNDSTANDARDNODES_API FDelayNode : public FNodeFacade
	{
	public:
		/**
		 * Constructor used by the Metasound Frontend.
		 */
		FDelayNode(const FNodeInitData& InitData);
	};
} // namespace Metasound

