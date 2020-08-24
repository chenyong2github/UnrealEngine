// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundNodeInterface.h"

namespace Metasound
{
	struct FNodeInfo
	{
		FName ClassName;
		FText Description;
		FText AuthorName;
		FText PromptIfMissing;
	};

	class METASOUNDGRAPHCORE_API FNode : public INode
	{
		public:
			FNode(const FString& InInstanceName, const FNodeInfo& InInfo);

			virtual ~FNode() = default;

			/** Return the name of this specific instance of the node class. */
			virtual const FString& GetInstanceName() const override;

			/** Return the type name of this node. */
			virtual const FName& GetClassName() const override;

			/** Return a longer text description describing how this node is used. */
			virtual const FText& GetDescription() const override;

			/** Return the original author of this node class. */
			virtual const FText& GetAuthorName() const override;

			/** 
			 *  Return an optional prompt on how users can get the plugin this node is in,
			 *  if they have found a metasound that uses this node but don't have this plugin downloaded or enabled.
			 */
			virtual const FText& GetPromptIfMissing() const override;

		private:

			FString InstanceName;
			FNodeInfo Info;

	};
}
