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
#include "MetasoundVertex.h"


namespace Metasound
{
	class METASOUNDSTANDARDNODES_API FTriggerOnThresholdNode : public FNodeFacade
	{
	public:
		FTriggerOnThresholdNode(const FVertexName& InName, const FGuid& InInstanceID, float InDefaultThreshold);
		FTriggerOnThresholdNode(const FNodeInitData& InInitData);

		float GetDefaultThreshold() const { return DefaultThreshold; }
	private:
		float DefaultThreshold;
	};
}
