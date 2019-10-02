// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Changes/MeshVertexChange.h"
#include "DynamicMesh3.h"

#include "BaseDynamicMeshComponent.h"


void FMeshVertexChange::Apply(UObject* Object)
{
	IMeshVertexCommandChangeTarget* ChangeTarget = CastChecked<IMeshVertexCommandChangeTarget>(Object);
	ChangeTarget->ApplyChange(this, false);
}

void FMeshVertexChange::Revert(UObject* Object)
{
	IMeshVertexCommandChangeTarget* ChangeTarget = CastChecked<IMeshVertexCommandChangeTarget>(Object);
	ChangeTarget->ApplyChange(this, true);
}


FString FMeshVertexChange::ToString() const
{
	return FString(TEXT("Mesh Vertex Change"));
}





FMeshVertexChangeBuilder::FMeshVertexChangeBuilder()
{
	Change = MakeUnique<FMeshVertexChange>();
}

void FMeshVertexChangeBuilder::UpdateVertex(int VertexID, const FVector3d& OldPosition, const FVector3d& NewPosition)
{
	const int* FoundIndex = SavedVertices.Find(VertexID);
	if (FoundIndex == nullptr)
	{
		int NewIndex = Change->Vertices.Num();
		SavedVertices.Add(VertexID, NewIndex);
		Change->Vertices.Add(VertexID);
		Change->OldPositions.Add(OldPosition);
		Change->NewPositions.Add(NewPosition);
	} 
	else
	{
		Change->NewPositions[*FoundIndex] = NewPosition;
	}
}

void FMeshVertexChangeBuilder::UpdateVertexFinal(int VertexID, const FVector3d& NewPosition)
{
	check(SavedVertices.Contains(VertexID));

	const int* Index = SavedVertices.Find(VertexID);
	if ( Index != nullptr )
	{
		Change->NewPositions[*Index] = NewPosition;
	}
}


void FMeshVertexChangeBuilder::SavePosition(const FDynamicMesh3* Mesh, int VertexID, bool bInitial)
{
	FVector3d Pos = Mesh->GetVertex(VertexID);
	if (bInitial)
	{
		UpdateVertex(VertexID, Pos, Pos);
	}
	else 
	{
		UpdateVertexFinal(VertexID, Pos);
	}
}

void FMeshVertexChangeBuilder::SavePositions(const FDynamicMesh3* Mesh, const TArray<int>& VertexIDs, bool bInitial)
{
	int Num = VertexIDs.Num();
	if (bInitial)
	{
		for (int k = 0; k < Num; ++k)
		{
			FVector3d Pos = Mesh->GetVertex(VertexIDs[k]);
			UpdateVertex(VertexIDs[k], Pos, Pos);
		}
	}
	else
	{
		for (int k = 0; k < Num; ++k)
		{
			FVector3d Pos = Mesh->GetVertex(VertexIDs[k]);
			UpdateVertexFinal(VertexIDs[k], Pos);
		}
	}
}


void FMeshVertexChangeBuilder::SavePositions(const FDynamicMesh3* Mesh, const TSet<int>& VertexIDs, bool bInitial)
{
	if (bInitial)
	{
		for (int VertexID : VertexIDs)
		{
			FVector3d Pos = Mesh->GetVertex(VertexID);
			UpdateVertex(VertexID, Pos, Pos);
		}
	}
	else
	{
		for (int VertexID : VertexIDs)
		{
			FVector3d Pos = Mesh->GetVertex(VertexID);
			UpdateVertexFinal(VertexID, Pos);
		}
	}
}