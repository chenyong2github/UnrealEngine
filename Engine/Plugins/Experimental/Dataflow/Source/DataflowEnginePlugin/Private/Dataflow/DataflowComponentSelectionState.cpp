// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowComponentSelectionState.h"

#include "Dataflow/DataflowComponent.h"
#include "GeometryCollection/Facades/CollectionRenderingFacade.h"


void FDataflowSelectionState::UpdateSelection(UDataflowComponent* DataflowComponent)
{
	using namespace GeometryCollection::Facades;
	FRenderingFacade Facade(DataflowComponent->ModifyRenderingCollection());
	if (Facade.IsValid())
	{
		TManagedArray<int32>& SelectionArray = Facade.ModifySelectionState();
		if (Nodes.Num())
		{
			const TManagedArray<FString>& GeomNameArray = Facade.GetGeometryNameAttribute();

			TArray<bool> bVisited;
			bVisited.Init(false, Nodes.Num());

			for (int i = 0; i < SelectionArray.Num(); i++)
			{
				ObjectID ID(GeomNameArray[i], i);
				SelectionArray[i] = false;
				int32 IndexOf = Nodes.IndexOfByKey(ID);
				if (IndexOf != INDEX_NONE)
				{
					SelectionArray[i] = true;
					bVisited[IndexOf] = true;
				}
			}

			// remove unknown selections
			for (int32 Ndx = bVisited.Num() - 1; 0 <= Ndx; Ndx--)
			{
				if (!bVisited[Ndx])
				{
					Nodes.RemoveAt(Ndx);
				}
			}
		}
		else
		{
			SelectionArray.Fill(0);
		}
	}
}
