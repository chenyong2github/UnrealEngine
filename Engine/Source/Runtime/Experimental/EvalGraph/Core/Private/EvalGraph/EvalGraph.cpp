// Copyright Epic Games, Inc. All Rights Reserved.

#include "EvalGraph/EvalGraph.h"

#include "EvalGraph/EvalGraphInputOutput.h"
#include "EvalGraph/EvalGraphNodeParameters.h"
#include "EvalGraph/EvalGraphNodeFactory.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(EGGRAPH_LOG, Error, All);

namespace Eg
{

	FGraph::FGraph(FGuid InGuid)
		: Guid(InGuid)
	{
	}


	void FGraph::RemoveNode(TSharedPtr<FNode> Node)
	{
		for (FConnectionTypeBase* Output : Node->GetOutputs())
		{
			if (Output)
			{
				for (FConnectionTypeBase* Input : Output->GetBaseInputs())
				{
					if (Input)
					{
						Disconnect(Output, Input);
					}
				}
			}
		}
		for (FConnectionTypeBase* Input : Node->GetInputs())
		{
			if (Input)
			{
				TArray<FConnectionTypeBase*> Outputs = Input->GetBaseOutputs();
				for (FConnectionTypeBase* Out : Outputs)
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

	void FGraph::Connect(FConnectionTypeBase* Input, FConnectionTypeBase* Output)
	{
		if (ensure(Input && Output))
		{
			Input->AddConnection(Output);
			Output->AddConnection(Input);
			Connections.Add(FConnection(Input->GetGuid(), Output->GetGuid()));
		}
	}

	void FGraph::Disconnect(FConnectionTypeBase* Input, FConnectionTypeBase* Output)
	{
		Input->RemoveConnection(Output);
		Output->RemoveConnection(Input);
		Connections.RemoveSwap(FConnection(Input->GetGuid(), Output->GetGuid()));
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


				TArray< FConnectionTypeBase* > IO = Node->GetOutputs();  IO.Append(Node->GetInputs());
				ArNum = IO.Num();
				Ar << ArNum;
				for (FConnectionTypeBase* Conn : IO)
				{
					ArGuid = Conn->GetGuid();
					ArType = Conn->GetType();
					ArName = Conn->GetName();
					Ar << ArGuid << ArType << ArName;
				}

				Node->SerializeInternal(Ar);
			}

			Ar << Connections;

		}
		else if( Ar.IsLoading())
		{
			FGuid ArGuid;
			FName ArType, ArName;
			int32 ArNum = 0;

			TMap<FGuid, FConnectionTypeBase* > ConnectionGuidMap;

			Ar << ArNum;
			for (int32 Ndx = ArNum; Ndx > 0; Ndx--)
			{
				Ar << ArGuid << ArType << ArName;
				if (TSharedPtr<FNode> Node = FNodeFactory::GetInstance()->NewNodeFromRegisteredType(*this, { ArGuid,ArType,ArName }))
				{
					int ArNumInner;
					Ar << ArNumInner;
					TArray< FConnectionTypeBase* > IO = Node->GetOutputs();  IO.Append(Node->GetInputs());
					for (int Cdx=0;Cdx< ArNumInner; Cdx++)
					{
						Ar << ArGuid << ArType << ArName;
						if (Cdx < IO.Num())
						{
							//if ((EGraphConnectionType)ArIntType!=IO[Cdx]->GetType())
							//{
							//	FName TypeName = GraphConnectionTypeName((EGraphConnectionType)ArIntType);
							//	FName FromType = GraphConnectionTypeName(IO[Cdx]->GetType());
							//	UE_LOG(EGGRAPH_LOG, Fatal, TEXT("Type mismatch in input file for Ed::FGraphNode Input/Output (%s,%s) to type (%s)"),
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
					ensureMsgf(false, TEXT("Warning: Missing registered node type on load. Graph will fail to evaluate (%s %s)"), *ArType.ToString(), *ArName.ToString());
				}
				
			}

			TArray< FConnection > LocalConnections;
			Ar << LocalConnections;
			for (const FConnection& Con : LocalConnections)
			{
				ensure(ConnectionGuidMap.Contains(Con.Key));
				ensure(ConnectionGuidMap.Contains(Con.Value));
				ensure(ConnectionGuidMap[Con.Key]->GetType() == ConnectionGuidMap[Con.Value]->GetType());
				Connect(ConnectionGuidMap[Con.Key], ConnectionGuidMap[Con.Value]);
			}

		}
	}

}

