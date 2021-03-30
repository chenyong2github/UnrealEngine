// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontendBaseClasses.h"
#include "MetasoundDataReference.h"

#include "MetasoundFrontendRegistries.h"



namespace Metasound
{
	namespace Frontend
	{
		TUniquePtr<INode> ConstructInputNode(const FName& InInputType, FInputNodeConstructorParams&& InParams)
		{
			return FMetasoundFrontendRegistryContainer::Get()->ConstructInputNode(InInputType, MoveTemp(InParams));
		}

		TUniquePtr<INode> ConstructOutputNode(const FName& InOutputType, const FOutputNodeConstructorParams& InParams)
		{
			return FMetasoundFrontendRegistryContainer::Get()->ConstructOutputNode(InOutputType, InParams);
		}

		void RegisterPendingNodes()
		{
			FMetasoundFrontendRegistryContainer::Get()->RegisterPendingNodes();
		}
	}
}


