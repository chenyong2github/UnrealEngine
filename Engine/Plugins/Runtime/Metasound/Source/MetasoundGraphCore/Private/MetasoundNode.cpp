// Copyright Epic Games, Inc. All Rights Reserved.


#include "MetasoundNode.h"
#include "CoreMinimal.h"

namespace Metasound
{
	FNode::FNode(const FString& InInstanceName, const FNodeInfo& InInfo)
	:	InstanceName(InInstanceName)
	,	Info(InInfo)
	{
	}

	/** Return the name of this specific instance of the node class. */
	const FString& FNode::GetInstanceName() const
	{
		return InstanceName;
	}

	/** Return the type name of this node. */
	const FNodeInfo& FNode::GetMetadata() const
	{
		return Info;
	}
}
