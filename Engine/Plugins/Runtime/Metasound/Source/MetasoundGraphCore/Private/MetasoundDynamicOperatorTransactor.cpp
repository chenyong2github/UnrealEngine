// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundDynamicOperatorTransactor.h"

#include "Containers/SpscQueue.h"
#include "Containers/UnrealString.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundDynamicOperator.h"
#include "MetasoundGraph.h"
#include "MetasoundGraphAlgo.h"
#include "MetasoundGraphAlgoPrivate.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundTrace.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"

namespace Metasound
{
	namespace DynamicGraph
	{
		namespace DynamicOperatorTransactorPrivate
		{
			// Literal nodes always have output vertex with this name. 
			static FLazyName LiteralNodeOutputVertexName("Value");

			TArray<FOperatorID> DetermineOperatorOrder(const IGraph& InGraph)
			{
				/* determine new operator order. */
				TArray<const INode*> NodeOrder;

				bool bSuccess = DirectedGraphAlgo::DepthFirstTopologicalSort(InGraph, NodeOrder);
				if (!bSuccess)
				{
					UE_LOG(LogMetaSound, Error, TEXT("Cycles found in dynamic graph"));
				}

				TArray<FOperatorID> OperatorOrder;
				Algo::Transform(NodeOrder, OperatorOrder, static_cast<FOperatorID(*)(const INode*)>(DirectedGraphAlgo::GetOperatorID)); //< Static cast to help deduce which overloaded version of GetOperatorID to call in Algo::Transform

				return OperatorOrder;
			}
		}

		bool operator<(const FDynamicOperatorTransactor::FLiteralNodeID& InLHS, const FDynamicOperatorTransactor::FLiteralNodeID& InRHS)
		{
			if (InLHS.ToNode < InRHS.ToNode)
			{
				return true;
			}
			else if (InRHS.ToNode < InLHS.ToNode)
			{
				return false;
			}
			else
			{
				return InLHS.ToVertex.FastLess(InRHS.ToVertex);
			}
		}

		FDynamicOperatorTransactor::FDynamicOperatorTransactor(const IGraph& InGraph)
		: Graph(InGraph)
		{
		}

		FDynamicOperatorTransactor::FDynamicOperatorTransactor()
		: Graph(TEXT(""), FGuid())
		{
		}

		TSharedRef<TSpscQueue<TUniquePtr<IDynamicOperatorTransform>>> FDynamicOperatorTransactor::CreateTransformQueue(const FOperatorSettings& InOperatorSettings, const FMetasoundEnvironment& InEnvironment)
		{
			TSharedRef<TSpscQueue<TUniquePtr<IDynamicOperatorTransform>>> Queue = MakeShared<TSpscQueue<TUniquePtr<IDynamicOperatorTransform>>>();
			OperatorInfos.Add(FDynamicOperatorInfo{InOperatorSettings, InEnvironment, Queue});

			return Queue;
		}

		void FDynamicOperatorTransactor::AddNode(const FGuid& InNodeID, TUniquePtr<INode> InNode)
		{
			using namespace DirectedGraphAlgo;

			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(Metasound::FDynamicOperatorTransactor::AddNode);

			if (!InNode)
			{
				return;
			}

			// Cache underlying pointer to node for later use. TUniquePtr<INode> will be moved to node
			// map thus invalidating `TUniquePtr<INode> InNode`. 
			INode* NodePtr = InNode.Get();

			NodeMap.Add(InNodeID, MoveTemp(InNode));

			auto CreateAddNodeTransform = [&](const FOperatorSettings& InOperatorSettings, const FMetasoundEnvironment& InEnvironment) -> TUniquePtr<IDynamicOperatorTransform>
			{
				return CreateAddOperatorTransform(*NodePtr, InOperatorSettings, InEnvironment);
			};

			EnqueueTransformOnOperatorQueues(CreateAddNodeTransform);
		}

		void FDynamicOperatorTransactor::RemoveNode(const FGuid& InNodeID)
		{
			using namespace DirectedGraphAlgo;

			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(Metasound::FDynamicOperatorTransactor::RemoveNode);

			if (const INode* Node = FindNode(InNodeID))
			{
				FOperatorID OperatorID = GetOperatorID(Node);
				auto CreateRemoveNodeTransform = [&](const FOperatorSettings& InOperatorSettings, const FMetasoundEnvironment& InEnvironment) -> TUniquePtr<IDynamicOperatorTransform>
				{
					return MakeUnique<FRemoveOperator>(OperatorID);
				};
				EnqueueTransformOnOperatorQueues(CreateRemoveNodeTransform);

				Graph.RemoveDataEdgesWithNode(*Node);
				NodeMap.Remove(InNodeID);
			}
			else
			{
				UE_LOG(LogMetaSound, Error, TEXT("No node found in dynamic transactor with ID %s"), *InNodeID.ToString());
			}
		}

		/** Add an edge to the graph. */
		void FDynamicOperatorTransactor::AddDataEdge(const FGuid& InFromNodeID, const FVertexName& InFromVertex, const FGuid& InToNodeID, const FVertexName& InToVertex)
		{
			using namespace DirectedGraphAlgo; 

			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(Metasound::FDynamicOperatorTransactor::AddDataEdge);

			const INode* FromNode = FindNode(InFromNodeID);
			const INode* ToNode = FindNode(InToNodeID);

			if ((nullptr == ToNode) || (nullptr == FromNode))
			{
				UE_LOG(LogMetaSound, Error, TEXT("Cannot add edge from %s:%s to %s:%s because of missing node"), *InFromNodeID.ToString(), *InFromVertex.ToString(), *InToNodeID.ToString(), *InToVertex.ToString());
				return;
			}

			AddDataEdgeInternal(*FromNode, InFromVertex, InToNodeID, *ToNode, InToVertex);
		}

		void FDynamicOperatorTransactor::RemoveDataEdge(const FGuid& InFromNodeID, const FVertexName& InFromVertex, const FGuid& InToNodeID, const FVertexName& InToVertex, TUniquePtr<INode> InReplacementLiteralNode)
		{
			using namespace DirectedGraphAlgo; 

			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(Metasound::FDynamicOperatorTransactor::RemoveDataEdge);

			const INode* FromNode = FindNode(InFromNodeID);
			const INode* ToNode = FindNode(InToNodeID);
			const INode* ReplacementLiteralNode = InReplacementLiteralNode.Get(); // Cache pointer because TUniquePtr<INode> will get moved

			if ((nullptr == ToNode) || (nullptr == FromNode))
			{
				UE_LOG(LogMetaSound, Error, TEXT("Cannot remove edge from %s:%s to %s:%s because of missing node"), *InFromNodeID.ToString(), *InFromVertex.ToString(), *InToNodeID.ToString(), *InToVertex.ToString());
				return;
			}

			if (!ToNode->GetVertexInterface().ContainsInputVertex(InToVertex))
			{
				UE_LOG(LogMetaSound, Error, TEXT("Cannot remove edge from %s:%s to %s:%s because of destination node does not contain vertex %s."), *InFromNodeID.ToString(), *InFromVertex.ToString(), *InToNodeID.ToString(), *InToVertex.ToString(), *InToVertex.ToString());
				return;
			}

			if (!InReplacementLiteralNode.IsValid())
			{
				UE_LOG(LogMetaSound, Error, TEXT("Cannot remove edge from %s:%s to %s:%s because of invalid pointer to replacement literal node."), *InFromNodeID.ToString(), *InFromVertex.ToString(), *InToNodeID.ToString(), *InToVertex.ToString());
				return;
			}

			// Store literal node associated with the target of the literal value.
			LiteralNodeMap.Add(FLiteralNodeID{InToNodeID, InToVertex}, MoveTemp(InReplacementLiteralNode));
			/* remove edge from internal graph. */
			bool bSuccess = Graph.RemoveDataEdge(*FromNode, InFromVertex, *ToNode, InToVertex);
			if (!bSuccess)
			{
				UE_LOG(LogMetaSound, Warning, TEXT("Failed to remove edge from %s:%s to %s:%s on internal graph."), *InFromNodeID.ToString(), *InFromVertex.ToString(), *InToNodeID.ToString(), *InToVertex.ToString());
				// No need to early exit here because edge likely did not exist.
			}

			bSuccess = Graph.AddDataEdge(*ReplacementLiteralNode, DynamicOperatorTransactorPrivate::LiteralNodeOutputVertexName, *ToNode, InToVertex);
			if (!bSuccess)
			{
				UE_LOG(LogMetaSound, Error, TEXT("Failed to add literal for %s:%s on internal graph."), *InToNodeID.ToString(), *InToVertex.ToString());
				return;
			}

			/* enqueue an update. */
			const FOperatorID FromOperatorID = GetOperatorID(FromNode);
			const FOperatorID LiteralOperatorID = GetOperatorID(ReplacementLiteralNode);
			const FOperatorID ToOperatorID = GetOperatorID(ToNode);
			TArray<FOperatorID> OperatorOrder = DynamicOperatorTransactorPrivate::DetermineOperatorOrder(Graph);

			auto CreateRemoveEdgeTransform = [&](const FOperatorSettings& InOperatorSettings, const FMetasoundEnvironment& InEnvironment) -> TUniquePtr<IDynamicOperatorTransform>
			{

				// Add the literal node.
				TUniquePtr<IDynamicOperatorTransform> AddNodeTransform = CreateAddOperatorTransform(*ReplacementLiteralNode, InOperatorSettings, InEnvironment);

				// Connect literal node to target node.
				TUniquePtr<IDynamicOperatorTransform> ConnectOperatorsTransform = MakeUnique<FConnectOperators>(LiteralOperatorID, DynamicOperatorTransactorPrivate::LiteralNodeOutputVertexName, ToOperatorID, InToVertex);

				// Reorder the node graph.
				TUniquePtr<IDynamicOperatorTransform> SetOrderTransform = MakeUnique<FSetOperatorOrder>(OperatorOrder);

				if (AddNodeTransform.IsValid() && ConnectOperatorsTransform.IsValid() && SetOrderTransform.IsValid())
				{
					// Create an atomic transform so all sub-transforms happen before next execution.
					TArray<TUniquePtr<IDynamicOperatorTransform>> AtomicTransforms;

					AtomicTransforms.Emplace(MoveTemp(AddNodeTransform));
					AtomicTransforms.Emplace(MoveTemp(ConnectOperatorsTransform));
					AtomicTransforms.Emplace(MoveTemp(SetOrderTransform));

					return MakeUnique<FAtomicTransform>(MoveTemp(AtomicTransforms));
				}
				else
				{
					UE_LOG(LogMetaSound, Error, TEXT("Cannot remove edge from %s:%s to %s:%s because of failure to create all transforms needed to perform operatorn."), *InFromNodeID.ToString(), *InFromVertex.ToString(), *InToNodeID.ToString(), *InToVertex.ToString());
					return TUniquePtr<IDynamicOperatorTransform>(nullptr);
				}
			};

			EnqueueTransformOnOperatorQueues(CreateRemoveEdgeTransform);
		}

		void FDynamicOperatorTransactor::SetValue(const FGuid& InNodeID, const FVertexName& InVertex, TUniquePtr<INode> InLiteralNode)
		{
			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(Metasound::FDynamicOperatorTransactor::SetValue);

			const INode* Node = FindNode(InNodeID);
			const INode* LiteralNode = InLiteralNode.Get();

			if (nullptr == Node)
			{
				UE_LOG(LogMetaSound, Error, TEXT("Cannot set node value of %s:%s because of missing node"), *InNodeID.ToString(), *InVertex.ToString());
				return;
			}

			if (!InLiteralNode.IsValid())
			{
				UE_LOG(LogMetaSound, Error, TEXT("Cannot set value on %s:%s because of invalid pointer to literal node."), *InNodeID.ToString(), *InVertex.ToString());
				return;
			}

			auto CreateAddNodeTransform = [&](const FOperatorSettings& InOperatorSettings, const FMetasoundEnvironment& InEnvironment) -> TUniquePtr<IDynamicOperatorTransform>
			{
				return CreateAddOperatorTransform(*LiteralNode, InOperatorSettings, InEnvironment);
			};

			EnqueueTransformOnOperatorQueues(CreateAddNodeTransform);

			AddDataEdgeInternal(*LiteralNode, DynamicOperatorTransactorPrivate::LiteralNodeOutputVertexName, InNodeID, *Node, InVertex);

			// Add literal node before calling "AddDataEdgeInternal" so that AddDataEdgeInternal can check if there is an existing literal node.
			LiteralNodeMap.Add(FLiteralNodeID{InNodeID, InVertex}, MoveTemp(InLiteralNode));
		}

		/** Add an input data destination to describe how data provided 
		 * outside this graph should be routed internally.
		 *
		 * @param InNode - Node which receives the data.
		 * @param InVertexName - Key for input vertex on InNode.
		 *
		 */
		void FDynamicOperatorTransactor::AddInputDataDestination(const FGuid& InNodeID, const FVertexName& InVertexName, const FLiteral& InDefaultLiteral, FReferenceCreationFunction InFunc)
		{
			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(Metasound::FDynamicOperatorTransactor::AddInputDataDestination);

			const INode* Node = FindNode(InNodeID);
			if (nullptr == Node)
			{
				UE_LOG(LogMetaSound, Error, TEXT("Cannot add Input Data Destination %s:%s because of missing node"), *InNodeID.ToString(), *InVertexName.ToString());
				return;
			}

			if (!Node->GetVertexInterface().ContainsInputVertex(InVertexName))
			{
				UE_LOG(LogMetaSound, Error, TEXT("Cannot add Input Data Destination %s:%s because of node does not contain input vertex with name %s."), *InNodeID.ToString(), *InVertexName.ToString(), *InVertexName.ToString());
				return;
			}

			const FInputDataVertex& InputVertex = Node->GetVertexInterface().GetInputVertex(InVertexName);
			EDataReferenceAccessType ReferenceAccessType = InputVertex.AccessType == EVertexAccessType::Value ? EDataReferenceAccessType::Value : EDataReferenceAccessType::Write;
			FOperatorID OperatorID = DirectedGraphAlgo::GetOperatorID(Node);

			Graph.AddInputDataDestination(*Node, InVertexName);

			auto CreateAddInputTransform = [&](const FOperatorSettings& InOperatorSettings, const FMetasoundEnvironment& InEnvironment) -> TUniquePtr<IDynamicOperatorTransform>
			{
				TOptional<FAnyDataReference> NewDataReference = InFunc(InOperatorSettings, InDefaultLiteral, ReferenceAccessType);
				if (NewDataReference.IsSet())
				{
					return MakeUnique<FAddInput>(OperatorID, InVertexName, *NewDataReference);
				}
				else
				{
					UE_LOG(LogMetaSound, Error, TEXT("Cannot add Input Data Destination %s:%s because of failure to create data reference."), *InNodeID.ToString(), *InVertexName.ToString());
					return TUniquePtr<IDynamicOperatorTransform>(nullptr);
				}
			};

			EnqueueTransformOnOperatorQueues(CreateAddInputTransform);
		}

		void FDynamicOperatorTransactor::RemoveInputDataDestination(const FVertexName& InVertexName)
		{
			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(Metasound::FDynamicOperatorTransactor::RemoveInputDataDestination);

			Graph.RemoveInputDataDestination(InVertexName);

			auto CreateRemoveInputTransform = [&](const FOperatorSettings& InOperatorSettings, const FMetasoundEnvironment& InEnvironment) -> TUniquePtr<IDynamicOperatorTransform>
			{
				return MakeUnique<FRemoveInput>(InVertexName);
			};

			EnqueueTransformOnOperatorQueues(CreateRemoveInputTransform);
		}

		/** Add an output data source which describes routing of data which is 
		 * owned this graph and exposed externally.
		 *
		 * @param InNode - Node which produces the data.
		 * @param InVertexName - Key for output vertex on InNode.
		 */
		void FDynamicOperatorTransactor::AddOutputDataSource(const FGuid& InNodeID, const FVertexName& InVertexName)
		{
			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(Metasound::FDynamicOperatorTransactor::AddOutputDataSource);

			const INode* Node = FindNode(InNodeID);
			if (nullptr == Node)
			{
				UE_LOG(LogMetaSound, Error, TEXT("Cannot add Output Data Source %s:%s because of missing node"), *InNodeID.ToString(), *InVertexName.ToString());
				return;
			}

			Graph.AddOutputDataSource(*Node, InVertexName);
			const FOperatorID OperatorID = DirectedGraphAlgo::GetOperatorID(Node);

			auto CreateAddOutputTransform = [&](const FOperatorSettings& InOperatorSettings, const FMetasoundEnvironment& InEnvironment) -> TUniquePtr<IDynamicOperatorTransform>
			{
				return MakeUnique<FAddOutput>(OperatorID, InVertexName);
			};

			EnqueueTransformOnOperatorQueues(CreateAddOutputTransform);
		}

		void FDynamicOperatorTransactor::RemoveOutputDataSource(const FVertexName& InVertexName)
		{
			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(Metasound::FDynamicOperatorTransactor::RemoveOutputDataSource);

			Graph.RemoveOutputDataSource(InVertexName);

			auto CreateRemoveOutputTransform = [&](const FOperatorSettings& InOperatorSettings, const FMetasoundEnvironment& InEnvironment) -> TUniquePtr<IDynamicOperatorTransform>
			{
				return MakeUnique<FRemoveOutput>(InVertexName);
			};

			EnqueueTransformOnOperatorQueues(CreateRemoveOutputTransform);
		}

		const IGraph& FDynamicOperatorTransactor::GetGraph() const
		{
			return Graph;
		}

		void FDynamicOperatorTransactor::AddDataEdgeInternal(const INode& InFromNode, const FVertexName& InFromVertex, const FGuid& InToNodeID, const INode& InToNode, const FVertexName& InToVertex)
		{
			using namespace DirectedGraphAlgo;

			/* Determine if there is an existing literal node connected to the node. */
			const FLiteralNodeID LiteralNodeKey{InToNodeID, InToVertex};
			bool bIsAlreadyConnectedToALiteralNode = false;
			FOperatorID PriorLiteralOperatorID;
			if (const TUniquePtr<INode>* PriorLiteralNode = LiteralNodeMap.Find(LiteralNodeKey))
			{
				bIsAlreadyConnectedToALiteralNode = true;
				PriorLiteralOperatorID = GetOperatorID(PriorLiteralNode->Get());
				
				Graph.RemoveDataEdge(**PriorLiteralNode, DynamicOperatorTransactorPrivate::LiteralNodeOutputVertexName, InToNode, InToVertex);
				LiteralNodeMap.Remove(LiteralNodeKey);
			}

			/* add edge to internal graph. */
			Graph.AddDataEdge(InFromNode, InFromVertex, InToNode, InToVertex);

			// Find order of operators after adding edge. 
			TArray<FOperatorID> OperatorOrder = DynamicOperatorTransactorPrivate::DetermineOperatorOrder(Graph);

			/* enqueue an update. */
			FOperatorID FromOperatorID = GetOperatorID(InFromNode);
			FOperatorID ToOperatorID = GetOperatorID(InToNode);

			auto CreateAddEdgeTransform = [&](const FOperatorSettings& InOperatorSettings, const FMetasoundEnvironment& InEnvironment) -> TUniquePtr<IDynamicOperatorTransform>
			{
				TArray<TUniquePtr<IDynamicOperatorTransform>> AtomicTransforms;

				if (bIsAlreadyConnectedToALiteralNode)
				{
					AtomicTransforms.Add(MakeUnique<FRemoveOperator>(PriorLiteralOperatorID));
				}
				AtomicTransforms.Add(MakeUnique<FSetOperatorOrder>(OperatorOrder));
				AtomicTransforms.Add(MakeUnique<FConnectOperators>(FromOperatorID, InFromVertex, ToOperatorID, InToVertex));

				return MakeUnique<FAtomicTransform>(MoveTemp(AtomicTransforms));
			};

			EnqueueTransformOnOperatorQueues(CreateAddEdgeTransform);
		}

		const INode* FDynamicOperatorTransactor::FindNode(const FGuid& InNodeID) const
		{
			if (const TUniquePtr<INode>* NodePtr = NodeMap.Find(InNodeID))
			{
				return NodePtr->Get();
			}
			else
			{
				UE_LOG(LogMetaSound, Error, TEXT("Graph does not contain node with ID %s"), *InNodeID.ToString());
				return nullptr;
			}
		}

		TUniquePtr<IDynamicOperatorTransform> FDynamicOperatorTransactor::CreateAddOperatorTransform(const INode& InNode, const FOperatorSettings& InOperatorSettings, const FMetasoundEnvironment& InEnvironment) const
		{
			const FOperatorID OperatorID = DirectedGraphAlgo::GetOperatorID(InNode);
			FVertexInterfaceData InterfaceData(InNode.GetVertexInterface());
			FBuildOperatorParams OperatorParams
			{
				InNode,
				InOperatorSettings,
				InterfaceData.GetInputs(),
				InEnvironment
			};

			FBuildResults Results;
			TUniquePtr<IOperator> Operator = InNode.GetDefaultOperatorFactory()->CreateOperator(OperatorParams, Results);

			for (const TUniquePtr<IOperatorBuildError>& Error : Results.Errors)
			{
				if (Error.IsValid())
				{
					UE_LOG(LogMetaSound, Warning, TEXT("Encountered error while building operator for node %s. %s:%s"), *InNode.GetMetadata().ClassName.GetFullName().ToString(), *Error->GetErrorType().ToString(), *Error->GetErrorDescription().ToString());
				}
			}

			if (Operator.IsValid())
			{
				Operator->BindInputs(InterfaceData.GetInputs());
				Operator->BindOutputs(InterfaceData.GetOutputs());

				FOperatorInfo OpInfo
				{
					MoveTemp(Operator),
					MoveTemp(InterfaceData)
				};

				return MakeUnique<FAddOperator>(OperatorID, MoveTemp(OpInfo));
			}
			else
			{
				return TUniquePtr<IDynamicOperatorTransform>(nullptr);
			}
		}


		void FDynamicOperatorTransactor::EnqueueTransformOnOperatorQueues(FCreateTransformFunctionRef InFunc)
		{
			TArray<FDynamicOperatorInfo>::TIterator OperatorInfoIterator = OperatorInfos.CreateIterator();
			while (OperatorInfoIterator)
			{
				TSharedPtr<TSpscQueue<TUniquePtr<IDynamicOperatorTransform>>> OperatorQueue = OperatorInfoIterator->Queue.Pin();
				if (OperatorQueue.IsValid())
				{
					TUniquePtr<IDynamicOperatorTransform> Transform = InFunc(OperatorInfoIterator->OperatorSettings, OperatorInfoIterator->Environment);
					if (Transform.IsValid())
					{
						OperatorQueue->Enqueue(MoveTemp(Transform));
					}
				}
				else
				{
					OperatorInfoIterator.RemoveCurrent();
				}
				OperatorInfoIterator++;
			}
		}
	}
}
