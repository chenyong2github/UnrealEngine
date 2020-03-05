// Copyright Epic Games, Inc. All Rights Reserved.

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





FMeshVertexChangeBuilder::FMeshVertexChangeBuilder(bool bSaveOverlayNormalsIn)
{
	Change = MakeUnique<FMeshVertexChange>();

	bSaveOverlayNormals = bSaveOverlayNormalsIn;
	if (bSaveOverlayNormals)
	{
		Change->bHaveOverlayNormals = true;
	}
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







void FMeshVertexChangeBuilder::UpdateOverlayNormal(int ElementID, const FVector3f& OldNormal, const FVector3f& NewNormal)
{
	const int* FoundIndex = SavedNormalElements.Find(ElementID);
	if (FoundIndex == nullptr)
	{
		int NewIndex = Change->Normals.Num();
		SavedNormalElements.Add(ElementID, NewIndex);
		Change->Normals.Add(ElementID);
		Change->OldNormals.Add(OldNormal);
		Change->NewNormals.Add(NewNormal);
	}
	else
	{
		Change->NewNormals[*FoundIndex] = NewNormal;
	}
}

void FMeshVertexChangeBuilder::UpdateOverlayNormalFinal(int ElementID, const FVector3f& NewNormal)
{
	check(SavedNormalElements.Contains(ElementID));
	const int* Index = SavedNormalElements.Find(ElementID);
	if (Index != nullptr)
	{
		Change->NewNormals[*Index] = NewNormal;
	}
}



void FMeshVertexChangeBuilder::SaveOverlayNormals(const FDynamicMesh3* Mesh, const TArray<int>& ElementIDs, bool bInitial)
{
	if (Mesh->HasAttributes() == false || Mesh->Attributes()->PrimaryNormals() == nullptr)
	{
		return;
	}
	const FDynamicMeshNormalOverlay* Overlay = Mesh->Attributes()->PrimaryNormals();

	int Num = ElementIDs.Num();
	if (bInitial)
	{
		for (int k = 0; k < Num; ++k)
		{
			FVector3f Normal = Overlay->GetElement(ElementIDs[k]);
			UpdateOverlayNormal(ElementIDs[k], Normal, Normal);
		}
	}
	else
	{
		for (int k = 0; k < Num; ++k)
		{
			FVector3f Normal = Overlay->GetElement(ElementIDs[k]);
			UpdateOverlayNormalFinal(ElementIDs[k], Normal);
		}
	}
}


void FMeshVertexChangeBuilder::SaveOverlayNormals(const FDynamicMesh3* Mesh, const TSet<int>& ElementIDs, bool bInitial)
{
	if (Mesh->HasAttributes() == false || Mesh->Attributes()->PrimaryNormals() == nullptr)
	{
		return;
	}
	const FDynamicMeshNormalOverlay* Overlay = Mesh->Attributes()->PrimaryNormals();

	if (bInitial)
	{
		for (int ElementID : ElementIDs)
		{
			FVector3f Normal = Overlay->GetElement(ElementID);
			UpdateOverlayNormal(ElementID, Normal, Normal);
		}
	}
	else
	{
		for (int ElementID : ElementIDs)
		{
			FVector3f Normal = Overlay->GetElement(ElementID);
			UpdateOverlayNormalFinal(ElementID, Normal);
		}
	}
}