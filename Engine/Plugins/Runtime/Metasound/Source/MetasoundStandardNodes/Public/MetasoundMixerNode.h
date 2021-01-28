// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "MetasoundAudioBuffer.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundDataReferenceCollection.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundFacade.h"
#include "MetasoundNode.h"
#include "MetasoundOperatorInterface.h"

namespace Metasound
{
	class METASOUNDSTANDARDNODES_API FMixerNode : public FNodeFacade
	{
	public:
		FMixerNode(const FString& InName, const FGuid& InInstanceID, float InDefaultMixGainCents);
		FMixerNode(const FNodeInitData& InInitData);

		float GetDefaultMixGainLinear() const;
	private:
		float DefaultMixGainLinear = 1.0f;
	};
}
