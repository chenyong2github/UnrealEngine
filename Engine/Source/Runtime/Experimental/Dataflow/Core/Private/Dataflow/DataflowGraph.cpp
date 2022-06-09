// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowGraph.h"

#include "Dataflow/DataflowInputOutput.h"
#include "Dataflow/DataflowNodeParameters.h"
#include "Dataflow/DataflowNodeFactory.h"
#include "Logging/LogMacros.h"
#include "Dataflow/DataflowArchive.h"

DEFINE_LOG_CATEGORY_STATIC(DATAFLOW_LOG, Error, All);

namespace Dataflow
{

	FGraph::FGraph(FGuid InGuid)
		: Guid(InGuid)
	{
	}


	void FGraph::RemoveNode(TSharedPtr<FDataflowNode> Node)
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
			for (TSharedPtr<FDataflowNode> Node : Nodes)
			{
				ArGuid = Node->GetGuid();
				ArType = Node->GetType();
				ArName = Node->GetName();
				Ar << ArGuid << ArType << ArName;

				DATAFLOW_OPTIONAL_BLOCK_WRITE_BEGIN()
				{
					TArray< FConnection* > IO = Node->GetOutputs();
					IO.Append(Node->GetInputs());
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
				DATAFLOW_OPTIONAL_BLOCK_WRITE_END();
			}

			Ar << Connections;

		}
		else if( Ar.IsLoading())
		{
			FGuid ArGuid;
			FName ArType, ArName;
			int32 ArNum = 0;

			TMap<FGuid, TSharedPtr<FDataflowNode> > NodeGuidMap;
			TMap<FGuid, FConnection* > ConnectionGuidMap;

			Ar << ArNum;
			for (int32 Ndx = ArNum; Ndx > 0; Ndx--)
			{
				Ar << ArGuid << ArType << ArName;

				TSharedPtr<FDataflowNode> Node = FNodeFactory::GetInstance()->NewNodeFromRegisteredType(*this, { ArGuid,ArType,ArName });
				DATAFLOW_OPTIONAL_BLOCK_READ_BEGIN(Node != nullptr)
				{
					ensure(!NodeGuidMap.Contains(ArGuid));
					NodeGuidMap.Add(ArGuid, Node);

					int ArNumInner;
					Ar << ArNumInner;
					TArray< FConnection* > IO = Node->GetOutputs();  IO.Append(Node->GetInputs());
					for (int Cdx = 0; Cdx < ArNumInner; Cdx++)
					{
						Ar << ArGuid << ArType << ArName;
						if (Cdx < IO.Num())
						{
							IO[Cdx]->SetGuid(ArGuid);
							ensure(!ConnectionGuidMap.Contains(ArGuid));
							ConnectionGuidMap.Add(ArGuid, IO[Cdx]);
						}
					}

					Node->SerializeInternal(Ar);
				}
				DATAFLOW_OPTIONAL_BLOCK_READ_ELSE()
				{
					DisabledNodes.Add(ArName);
					ensureMsgf(false,
						TEXT("Error: Missing registered node type (%s) will be removed from graph on load. Graph will fail to evaluate due to missing node (%s).")
						, *ArType.ToString(), *ArName.ToString());
				}
				DATAFLOW_OPTIONAL_BLOCK_READ_END();
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

