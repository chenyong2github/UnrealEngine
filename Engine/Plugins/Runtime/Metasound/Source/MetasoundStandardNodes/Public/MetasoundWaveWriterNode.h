// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundFacade.h"

namespace Metasound
{
	class METASOUNDSTANDARDNODES_API FWaveWriterNode : public FNodeFacade
	{
	public:
		FWaveWriterNode(const FString& InName, const FGuid& InInstanceID);
		FWaveWriterNode(const FNodeInitData& InInitData);
	private:
	};
} // namespace Metasound

