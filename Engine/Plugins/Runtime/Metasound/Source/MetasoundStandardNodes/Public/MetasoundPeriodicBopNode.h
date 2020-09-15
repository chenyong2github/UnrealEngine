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
	class METASOUNDSTANDARDNODES_API FPeriodicBopNode : public FNodeFacade
	{
		public:
			FPeriodicBopNode(const FString& InName, float InDefaultPeriodInSeconds);

			FPeriodicBopNode(const FNodeInitData& InInitData);

			float GetDefaultPeriodInSeconds() const;

		private:

			float DefaultPeriod;
	};
}
