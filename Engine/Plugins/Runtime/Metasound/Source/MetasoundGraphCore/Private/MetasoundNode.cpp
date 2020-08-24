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
	const FName& FNode::GetClassName() const
	{
		return Info.ClassName;
	}

	/** Return a longer text description describing how this node is used. */
	const FText& FNode::GetDescription() const
	{
		return Info.Description;
	}

	/** Return the original author of this node class. */
	const FText& FNode::GetAuthorName() const
	{
		return Info.AuthorName;
	}

	/** 
	 *  Return an optional prompt on how users can get the plugin this node is in,
	 *  if they have found a metasound that uses this node but don't have this plugin downloaded or enabled.
	 */
	const FText& FNode::GetPromptIfMissing() const
	{
		return Info.PromptIfMissing;
	}
}
