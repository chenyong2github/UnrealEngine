// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Internationalization/Text.h"

#include "MetasoundFacade.h"


namespace Metasound
{
	class METASOUNDSTANDARDNODES_API FGainMultiplyNode : public FNodeFacade
	{
		public:
			FGainMultiplyNode(const FString& InInstanceName);
			FGainMultiplyNode(const FNodeInitData& InInitData);
	};
} // namespace Metasound
