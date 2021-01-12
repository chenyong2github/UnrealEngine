// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundNode.h"
#include "MetasoundBop.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundFacade.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundDataReferenceCollection.h"
#include "MetasoundTime.h"

namespace Metasound
{
	class METASOUNDSTANDARDNODES_API FTriggerDelayNode : public FNodeFacade
	{
		public:
			FTriggerDelayNode(const FString& InName, float InDefaultDelayInSeconds);
			FTriggerDelayNode(const FNodeInitData& InInitData);

			float GetDefaultDelayInSeconds() const;

		private:
			float DefaultDelay;
	};
}
