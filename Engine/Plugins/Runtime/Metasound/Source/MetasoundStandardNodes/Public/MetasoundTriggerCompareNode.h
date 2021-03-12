// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundDataReferenceCollection.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundFacade.h"
#include "MetasoundNodeInterface.h"

namespace Metasound
{
	class METASOUNDSTANDARDNODES_API FTriggerCompareNode : public FNodeFacade
	{
		public:
			FTriggerCompareNode(const FNodeInitData& InInitData);

			virtual ~FTriggerCompareNode() = default;
	};
}
