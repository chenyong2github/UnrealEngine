// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Internationalization/Text.h"

#include "MetasoundFacade.h"


namespace Metasound
{
	class METASOUNDSTANDARDNODES_API FBufferAddFloatNode : public FNodeFacade
	{
		public:
			FBufferAddFloatNode(const FString& InInstanceName, const FGuid& InInstanceID);
			FBufferAddFloatNode(const FNodeInitData& InInitData);
	};
} // namespace Metasound
