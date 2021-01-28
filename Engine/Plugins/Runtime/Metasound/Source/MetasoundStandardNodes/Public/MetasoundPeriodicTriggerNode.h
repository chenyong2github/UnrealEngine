// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundDataReferenceCollection.h"
#include "MetasoundNode.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundFacade.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundTime.h"
#include "MetasoundTrigger.h"


namespace Metasound
{
	class METASOUNDSTANDARDNODES_API FPeriodicTriggerNode : public FNodeFacade
	{
		public:
			FPeriodicTriggerNode(const FString& InInstanceName, const FGuid& InInstanceID, float InDefaultPeriodInSeconds);
			FPeriodicTriggerNode(const FNodeInitData& InInitData);

			float GetDefaultPeriodInSeconds() const;

		private:
			float DefaultPeriod = 1.0f;
	};
}
