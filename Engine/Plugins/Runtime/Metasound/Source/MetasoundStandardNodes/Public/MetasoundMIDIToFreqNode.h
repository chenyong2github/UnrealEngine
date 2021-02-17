// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundFacade.h"

namespace Metasound
{
	/** FMIDIToFreqNode
	 *
	 *  Outputs the frequency value for a given MIDI note.
	 */
	class METASOUNDSTANDARDNODES_API FMIDIToFreqNode : public FNodeFacade
	{
	public:
		/**
		 * Constructor used by the Metasound Frontend.
		 */
		FMIDIToFreqNode(const FNodeInitData& InitData);
	};
} // namespace Metasound

