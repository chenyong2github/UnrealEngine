// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundFacade.h"

namespace Metasound
{
	class METASOUNDSTANDARDNODES_API FNoiseNode : public FNodeFacade
	{
	public:
		FNoiseNode(const FString& InName, const FGuid& InInstanceID, int32 InDefaultSeed);
		FNoiseNode(const FNodeInitData& InInitData);

		int32 GetDefaultSeed() const { return DefaultSeed; }
	private:
		int32 DefaultSeed;
	};
}
