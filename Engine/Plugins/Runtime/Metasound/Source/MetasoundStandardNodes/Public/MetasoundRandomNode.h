// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundFacade.h"

namespace Metasound
{
	/** FRandomIntNode
	 *
	 *  Generates a random integer value when triggered.
	 */
	class METASOUNDSTANDARDNODES_API FRandomIntNode : public FNodeFacade
	{
	public:
		/**
		 * Constructor used by the Metasound Frontend.
		 */
		FRandomIntNode(const FNodeInitData& InInitData);
	};

	/** FRandomFloatNode
	 *
	 *  Generates a random float value when triggered.
	 */
	class METASOUNDSTANDARDNODES_API FRandomFloatNode : public FNodeFacade
	{
	public:
		/**
		 * Constructor used by the Metasound Frontend.
		 */
		FRandomFloatNode(const FNodeInitData& InInitData);
	};


} // namespace Metasound

