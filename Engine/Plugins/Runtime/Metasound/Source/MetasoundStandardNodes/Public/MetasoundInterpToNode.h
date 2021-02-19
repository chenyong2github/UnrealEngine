// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundFacade.h"

namespace Metasound
{
	/** FInterpToNode
	 *
	 *  Interpolates to a target value over a given time.
	 */
	class METASOUNDSTANDARDNODES_API FInterpToNode : public FNodeFacade
	{
	public:
		/**
		 * Constructor used by the Metasound Frontend.
		 */
		FInterpToNode(const FNodeInitData& InitData);
	};
} // namespace Metasound

