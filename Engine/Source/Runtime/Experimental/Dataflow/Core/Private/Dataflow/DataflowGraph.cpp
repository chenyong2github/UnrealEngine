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
		for (FDataflowConnection* Output : Node->GetOutputs())
		{
			if (Output)
			{
				for (FDataflowConnection* Input : Output->GetConnectedInputs())
				{
					if (Input)
					{
						Disconnect(Output, Input);
					}
				}
			}
		}
		for (FDataflowConnection* Input : Node->GetInputs())
		{
			if (Input)
			{
				TArray<FDataflowConnection*> Outputs = Input->GetConnectedOutputs();
				for (FDataflowConnection* Out : Outputs)
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

	void FGraph::ClearConnections(FDataflowConnection* Connection)
	{
		// Todo(dataflow) : do this without triggering a invalidation. 
		//            or implement a better sync for the EdGraph and DataflowGraph
		if (Connection->GetDirection() == FPin::EDirection::INPUT)
		{
			TArray<FDataflowConnection*> BaseOutputs = Connection->GetConnectedOutputs();
			for (FDataflowConnection* Output : BaseOutputs)
			{
				Disconnect(Connection, Output);
			}
		}
		else if (Connection->GetDirection() == FPin::EDirection::OUTPUT)
		{
			TArray<FDataflowConnection*> BaseInputs = Connection->GetConnectedInputs();
			for (FDataflowConnection* Input : BaseInputs)
			{
				Disconnect(Input, Connection);
			}
		}
	}


	void FGraph::Connect(FDataflowConnection* Input, FDataflowConnection* Output)
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

	void FGraph::Disconnect(FDataflowConnection* Input, FDataflowConnection* Output)
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
					TArray< FDataflowConnection* > IO = Node->GetOutputs();
					IO.Append(Node->GetInputs());
					ArNum = IO.Num();
					Ar << ArNum;
					for (FDataflowConnection* Conn : IO)
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
			TMap<FGuid, FDataflowConnection* > ConnectionGuidMap;

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
					TArray< FDataflowConnection* > IO = Node->GetOutputs();  IO.Append(Node->GetInputs());
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
				if (NodeGuidMap.Contains(Con.InputNode) && NodeGuidMap.Contains(Con.OutputNode))
				{
					if (ConnectionGuidMap.Contains(Con.Input) && ConnectionGuidMap.Contains(Con.Output))
					{
						if (ConnectionGuidMap[Con.Input]->GetType() == ConnectionGuidMap[Con.Output]->GetType())
						{
							Connect(ConnectionGuidMap[Con.Input], ConnectionGuidMap[Con.Output]);
						}
					}
				}
			}

		}
	}
}

