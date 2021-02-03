// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundNode.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundFacade.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundDataReferenceCollection.h"

namespace Metasound
{
	class METASOUNDSTANDARDNODES_API FTriggerPipeNode : public FNodeFacade
	{
		public:
			FTriggerPipeNode(const FString& InName, const FGuid& InInstanceID, float InDefaultDelayInSeconds);
			FTriggerPipeNode(const FNodeInitData& InInitData);
	};
}
