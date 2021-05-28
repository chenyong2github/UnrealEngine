// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/TVariant.h"
#include "MetasoundAccessPtr.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendRegistries.h"

namespace Metasound
{
	namespace Frontend
	{
		// This is called by FMetasoundFrontendModule, and flushes any node or datatype registration that was done prior to boot.
		void RegisterPendingNodes();

		// Convenience functions to create an INode corresponding to a specific input or output for a metasound graph.
		// @returns nullptr if the type given wasn't found.
		METASOUNDFRONTEND_API TUniquePtr<INode> ConstructInputNode(const FName& InInputType, FInputNodeConstructorParams&& InParams);
		METASOUNDFRONTEND_API TUniquePtr<INode> ConstructVariableNode(const FName& InVariableType, FVariableNodeConstructorParams&& InParams);
		METASOUNDFRONTEND_API TUniquePtr<INode> ConstructOutputNode(const FName& InOutputType, FOutputNodeConstructorParams&& InParams);
	}
}
