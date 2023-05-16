// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/SortedMap.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundVertexData.h"
#include "Templates/UniquePtr.h"

namespace Metasound
{
	// Forward Declare
	class INode;

	namespace DirectedGraphAlgo
	{
		// ID used to lookup operators.
		using FOperatorID = uintptr_t;

		// Retrieve an operator ID from a node. 
		FOperatorID GetOperatorID(const INode& InNode);
		FOperatorID GetOperatorID(const INode* InNode);

		/** FGraphOoperatorData contains all the objects needed to implement a runtime instance of a MetaSound graph.  */
		struct FGraphOperatorData
		{
			// Represents an input of an operator
			struct FVertexDestination
			{
				FOperatorID OperatorID;
				FVertexName VertexName;
			};

			struct FOperatorInfo
			{
				TUniquePtr<IOperator> Operator;

				// Vertex Data bound to operator.
				FVertexInterfaceData VertexData;

				// Map where the operators output vertex is the key, and an array of output
				// connections is the value.
				TSortedVertexNameMap<TArray<FVertexDestination>> OutputConnections;
			};

			FGraphOperatorData(const FOperatorSettings& InOperatorSettings);

			FOperatorSettings OperatorSettings;

			// Vertex Data bound to the graph
			FVertexInterfaceData VertexData;

			// Sorted order of operators for execution.
			TArray<FOperatorID> OperatorOrder;

			TSortedMap<FOperatorID, FOperatorInfo> OperatorMap;

			// Map with input vertex name as key, and OperatorID of input node as value.
			TSortedVertexNameMap<FOperatorID> InputVertexMap;

			// Map with output vertex name as key, and OperatorID of output node as value.
			TSortedVertexNameMap<FOperatorID> OutputVertexMap;
		};

		// Arguments for calling Rebind
		struct FRebindGraphDataParams
		{
			FVertexInterfaceData& VertexData; 											//< Current vertex data of graph
			TSortedMap<FOperatorID, FGraphOperatorData::FOperatorInfo>& OperatorMap; 	//< Map of operator data in graph
			TSortedVertexNameMap<FOperatorID>& InputVertexMap; 							//< Map of graph input nodes
			TSortedVertexNameMap<FOperatorID>& OutputVertexMap; 						//< Map of graph output nodes
		};

		using FOnPostOperatorRebind = TFunctionRef<void(FOperatorID, IOperator&)>;

		// Rebind the graph inputs, updating internal operator bindings as needed
		//
		// When an internal node is rebound, the OnPostOperatorRebind will be called. 
		void RebindGraphInputs(FInputVertexInterfaceData& InOutVertexData, FRebindGraphDataParams& InOutParams, FOnPostOperatorRebind OnPostOperatorRebind);

		// Rebind the graph inputs, updating internal operator bindings as needed
		//
		// When an internal node is rebound, the OnPostOperatorRebind will be called. 
		void RebindGraphOutputs(FOutputVertexInterfaceData& InOutVertexData, FRebindGraphDataParams& InOutParams, FOnPostOperatorRebind OnPostOperatorRebind);
	}
}
