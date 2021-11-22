// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetasoundFrontendGraph.h"
#include "MetasoundVertex.h"

namespace Metasound
{
	namespace Frontend 
	{
		/** FReceiveNodeAddressFunction defines the function signature for callbacks
		 * to create send addresses. Different systems may rely on different data
		 * in the Metasound Environment to create a unique address.
		 *
		 * @param InEnv - The current environment of the graph hosting the receive node.
		 * @param InVertexName - The input vertex key of the graph input which will have a 
		 *                      receive node injected. 
		 * @param InTypeName - The data type of the receive node. 
		 *
		 * @return FSendAddress The resulting transmission address for the receive node. 
		 */
		using FReceiveNodeAddressFunction = TFunction<FSendAddress(const FMetasoundEnvironment& InEnv, const FVertexName& InVertexName, const FName& InTypeName)>;

		/** Injects a receive node between an graph input and the connected internal nodes.
		 *
		 * @param InGraph - The graph to manipulate.
		 * @parma InAddressPolicy - A function which creates unique transmission addresses for each input.
		 * @param InInputDestination - The input destination on the graph where a receive node should be injected.
		 *
		 * @return True on success, false on failure. 
		 */
		METASOUNDFRONTEND_API bool InjectReceiveNode(FFrontendGraph& InGraph, const FReceiveNodeAddressFunction& InAddressPolicy, const FInputDataDestination& InInputDestination);

		/** Injects receive nodes between an graph inputs and the connected internal nodes.
		 *
		 * @param InGraph - The graph to manipulate.
		 * @parma InAddressPolicy - Function which creates unique transmission addresses for each input.
		 * @param InInputVertexNames - Set of input vertices which should have receive nodes injected.
		 *
		 * @return True on success, false on failure.
		 */
		METASOUNDFRONTEND_API bool InjectReceiveNodes(FFrontendGraph& InGraph, const FReceiveNodeAddressFunction& InAddressPolicy, const TSet<FVertexName>& InInputVertexNames);
	}
}

