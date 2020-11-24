// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundNodeInterface.h"

namespace Metasound
{
	class METASOUNDGRAPHCORE_API FNode : public INode
	{
		public:
			FNode(const FString& InInstanceName, const FNodeInfo& InInfo);

			virtual ~FNode() = default;

			/** Return the name of this specific instance of the node class. */
			const FString& GetInstanceName() const override;

			/** Return metadata associated with this node. */
			const FNodeInfo& GetMetadata() const override;

		private:

			FString InstanceName;
			FNodeInfo Info;
	};
}
