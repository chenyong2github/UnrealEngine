// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Internationalization/Text.h"

#include "MetasoundFacade.h"


namespace Metasound
{
	class METASOUNDSTANDARDNODES_API FAudioMultiplyNode : public FNodeFacade
	{
		public:
			FAudioMultiplyNode(const FString& InInstanceName);
			FAudioMultiplyNode(const FNodeInitData& InInitData);
	};

} // namespace Metasound
