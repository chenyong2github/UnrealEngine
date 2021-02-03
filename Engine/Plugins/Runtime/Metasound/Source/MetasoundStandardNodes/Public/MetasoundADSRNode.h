// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "MetasoundFacade.h"

namespace Metasound
{
	/** FADSRNode
	 *
	 *  Creates an Attack, Decay Sustain, Release audio processor node. 
	 */
	class METASOUNDSTANDARDNODES_API FADSRNode : public FNodeFacade
	{
		public:
			/**
			 * Constructor used by the Metasound Frontend.
			 */
			FADSRNode(const FNodeInitData& InitData);
	};
} // namespace Metasound
