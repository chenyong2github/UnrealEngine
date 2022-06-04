// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowGraph.h"

#include "Dataflow/DataflowInputOutput.h"
#include "Dataflow/DataflowNodeParameters.h"
#include "Dataflow/DataflowNodeFactory.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(DATAFLOW_LOG, Error, All);

namespace Dataflow
{

	FGraph::FGraph(FGuid InGuid)
		: Guid(InGuid)
	{
	}


	void FGraph::RemoveNode(TSharedPtr<FNode> Node)
	{
		for (FConnection* Output : Node->GetOutputs())
		{
			if (Output)
			{
				for (FConnection* Input : Output->GetConnectedInputs())
				{
					if (Input)
					{
						Disconnect(Output, Input);
					}
				}
			}
		}
		for (FConnection* Input : Node->GetInputs())
		{
			if (Input)
			{
				TArray<FConnection*> Outputs = Input->GetConnectedOutputs();
				for (FConnection* Out : Outputs)
				{
					if (Out)
					{
						Disconnect(Out, Input);
					}
				}
			}
		}
		Nodes.Remove(Node);
	}

	void FGraph::ClearConnections(FConnection* Connection)
	{
		// Todo(dataflow) : do this without triggering a invalidation. 
		//            or implement a better sync for the EdGraph and DataflowGraph
		if (Connection->GetDirection() == FPin::EDirection::INPUT)
		{
			TArray<FConnection*> BaseOutputs = Connection->GetConnectedOutputs();
			for (FConnection* Output : BaseOutputs)
			{
				Disconnect(Connection, Output);
			}
		}
		else if (Connection->GetDirection() == FPin::EDirection::OUTPUT)
		{
			TArray<FConnection*> BaseInputs = Connection->GetConnectedInputs();
			for (FConnection* Input : BaseInputs)
			{
				Disconnect(Input, Connection);
			}
		}
	}


	void FGraph::Connect(FConnection* Input, FConnection* Output)
	{
		if (ensure(Input && Output))
		{
			Input->AddConnection(Output);
			Output->AddConnection(Input);
			Connections.Add(FLink(
				Input->GetOwningNode()->GetGuid(), Input->GetGuid(), 
				Output->GetOwningNode()->GetGuid(), Output->GetGuid()));
		}
	}

	void FGraph::Disconnect(FConnection* Input, FConnection* Output)
	{
		Input->RemoveConnection(Output);
		Output->RemoveConnection(Input);
		Connections.RemoveSwap(FLink(
			Input->GetOwningNode()->GetGuid(), Input->GetGuid(),
			Output->GetOwningNode()->GetGuid(), Output->GetGuid()));

	}

	void FGraph::Serialize(FArchive& Ar)
	{
		Ar << Guid;
		if (Ar.IsSaving())
		{
			FGuid ArGuid;
			FName ArType, ArName;
			int32 ArNum = Nodes.Num();

			Ar << ArNum;
			for (TSharedPtr<FNode> Node : Nodes)
			{
				ArGuid = Node->GetGuid();
				ArType = Node->GetType();
				ArName = Node->GetName();
				Ar << ArGuid << ArType << ArName;

				// Data Jump Serialization (on Node and connections)
				{
					const int64 NodeDataSizePos = Ar.Tell();
					int64 NodeDataSize = 0;
					Ar << NodeDataSize;

					const int64 NodeBegin = Ar.Tell();
					{ // Data we can jump past on load
						TArray< FConnection* > IO = Node->GetOutputs();  IO.Append(Node->GetInputs());
						ArNum = IO.Num();
						Ar << ArNum;
						for (FConnection* Conn : IO)
						{
							ArGuid = Conn->GetGuid();
							ArType = Conn->GetType();
							ArName = Conn->GetName();
							Ar << ArGuid << ArType << ArName;
						}

						Node->SerializeInternal(Ar);
					}

					if (NodeDataSizePos != INDEX_NONE)
					{
						const int64 NodeEnd = Ar.Tell();
						NodeDataSize = (NodeEnd - NodeBegin);
						Ar.Seek(NodeDataSizePos);
						Ar << NodeDataSize;
						Ar.Seek(NodeEnd);
					}
				}
			}

			Ar << Connections;

		}
		else if( Ar.IsLoading())
		{
			FGuid ArGuid;
			FName ArType, ArName;
			int32 ArNum = 0;
			int64 NodeDataSize = 0;

			TMap<FGuid, TSharedPtr<FNode> > NodeGuidMap;
			TMap<FGuid, FConnection* > ConnectionGuidMap;

			Ar << ArNum;
			for (int32 Ndx = ArNum; Ndx > 0; Ndx--)
			{
				Ar << ArGuid << ArType << ArName << NodeDataSize;

				if (TSharedPtr<FNode> Node = FNodeFactory::GetInstance()->NewNodeFromRegisteredType(*this, { ArGuid,ArType,ArName }))
				{
					ensure(!NodeGuidMap.Contains(ArGuid));
					NodeGuidMap.Add(ArGuid,Node);


					int ArNumInner;
					Ar << ArNumInner;
					TArray< FConnection* > IO = Node->GetOutputs();  IO.Append(Node->GetInputs());
					for (int Cdx=0;Cdx< ArNumInner; Cdx++)
					{
						Ar << ArGuid << ArType << ArName;
						if (Cdx < IO.Num())
						{
							//if ((EGraphConnectionType)ArIntType!=IO[Cdx]->GetType())
							//{
							//	FName TypeName = GraphConnectionTypeName((EGraphConnectionType)ArIntType);
							//	FName FromType = GraphConnectionTypeName(IO[Cdx]->GetType());
							//	UE_LOG(DATAFLOW_LOG, Fatal, TEXT("Type mismatch in input file for Ed::FGraphNode Input/Output (%s,%s) to type (%s)"),
							//		*TypeName.ToString(), *ArName.ToString(), *FromType.ToString());
							//}

							IO[Cdx]->SetGuid(ArGuid);
							ensure(!ConnectionGuidMap.Contains(ArGuid));
							ConnectionGuidMap.Add(ArGuid, IO[Cdx]);
						}
					}

					Node->SerializeInternal(Ar);
				}
				else
				{
					Ar.Seek(Ar.Tell() + NodeDataSize); // Data Jump Serialization (on mising Node)
					DisabledNodes.Add(ArName);
					ensureMsgf(false, 
						TEXT("Error: Missing registered node type (%s) will be removed from graph on load. Graph will fail to evaluate due to missing node (%s).")
						, *ArType.ToString(), *ArName.ToString());
				}
				
			}

			TArray< FLink > LocalConnections;
			Ar << LocalConnections;
			for (const FLink& Con : LocalConnections)
			{
				if (ensure(NodeGuidMap.Contains(Con.InputNode) && NodeGuidMap.Contains(Con.OutputNode)))
				{
					if (ensure(ConnectionGuidMap.Contains(Con.Input) && ConnectionGuidMap.Contains(Con.Output)))
					{
						if (ensure(ConnectionGuidMap[Con.Input]->GetType() == ConnectionGuidMap[Con.Output]->GetType()))
						{
							Connect(ConnectionGuidMap[Con.Input], ConnectionGuidMap[Con.Output]);
						}
					}
				}
			}

		}
	}
}

