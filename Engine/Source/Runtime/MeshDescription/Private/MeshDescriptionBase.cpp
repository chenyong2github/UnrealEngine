// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshDescriptionBase.h"
#include "Algo/Copy.h"



void UMeshDescriptionBase::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
}

void UMeshDescriptionBase::RegisterAttributes()
{
	RequiredAttributes = MakeUnique<FMeshAttributes>(MeshDescription);
	RequiredAttributes->Register();
}

void UMeshDescriptionBase::Reset()
{
	MeshDescription = FMeshDescription();
	RegisterAttributes();
}

void UMeshDescriptionBase::Empty()
{
	MeshDescription.Empty();
}

bool UMeshDescriptionBase::IsEmpty() const
{
	return MeshDescription.IsEmpty();
}

void UMeshDescriptionBase::ReserveNewVertices(int32 NumberOfNewVertices)
{
	MeshDescription.ReserveNewVertices(NumberOfNewVertices);
}

FVertexID UMeshDescriptionBase::CreateVertex()
{
	return MeshDescription.CreateVertex();
}

void UMeshDescriptionBase::CreateVertexWithID(FVertexID VertexID)
{
	if (!MeshDescription.IsVertexValid(VertexID))
	{
		MeshDescription.CreateVertexWithID(VertexID);
	}
	else
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("CreateVertexWithID: VertexID %d already exists."), VertexID.GetValue());
	}
}

void UMeshDescriptionBase::DeleteVertex(FVertexID VertexID)
{
	if (MeshDescription.IsVertexValid(VertexID))
	{
		MeshDescription.DeleteVertex(VertexID);
	}
	else
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("DeleteVertex: VertexID %d doesn't exist."), VertexID.GetValue());
	}
}

bool UMeshDescriptionBase::IsVertexValid(FVertexID VertexID) const
{
	return MeshDescription.IsVertexValid(VertexID);
}

void UMeshDescriptionBase::ReserveNewVertexInstances(int32 NumberOfNewVertexInstances)
{
	MeshDescription.ReserveNewEdges(NumberOfNewVertexInstances);
}

FVertexInstanceID UMeshDescriptionBase::CreateVertexInstance(FVertexID VertexID)
{
	if (MeshDescription.IsVertexValid(VertexID))
	{
		return MeshDescription.CreateVertexInstance(VertexID);
	}
	else
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("CreateVertexInstance: VertexID %d doesn't exist."), VertexID.GetValue());
		return FVertexInstanceID::Invalid;
	}
}

void UMeshDescriptionBase::CreateVertexInstanceWithID(FVertexInstanceID VertexInstanceID, FVertexID VertexID)
{
	if (MeshDescription.IsVertexValid(VertexID))
	{
		if (!MeshDescription.IsVertexInstanceValid(VertexInstanceID))
		{
			MeshDescription.CreateVertexInstanceWithID(VertexInstanceID, VertexID);
		}
		else
		{
			UE_LOG(LogMeshDescription, Warning, TEXT("CreateVertexInstanceWithID: VertexInstanceID %d already exists."), VertexInstanceID.GetValue());
		}
	}
	else
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("CreateVertexInstanceWithID: VertexID %d doesn't exist."), VertexID.GetValue());
	}
}

void UMeshDescriptionBase::DeleteVertexInstance(FVertexInstanceID VertexInstanceID, TArray<FVertexID>& OrphanedVertices)
{
	if (MeshDescription.IsVertexInstanceValid(VertexInstanceID))
	{
		MeshDescription.DeleteVertexInstance(VertexInstanceID, &OrphanedVertices);
	}
	else
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("DeleteVertexInstance: VertexInstanceID %d doesn't exist."), VertexInstanceID.GetValue());
	}
}

bool UMeshDescriptionBase::IsVertexInstanceValid(FVertexInstanceID VertexInstanceID) const
{
	return MeshDescription.IsVertexInstanceValid(VertexInstanceID);
}

void UMeshDescriptionBase::ReserveNewEdges(int32 NumberOfNewEdges)
{
	MeshDescription.ReserveNewEdges(NumberOfNewEdges);
}

FEdgeID UMeshDescriptionBase::CreateEdge(FVertexID VertexID0, FVertexID VertexID1)
{
	if (!MeshDescription.IsVertexValid(VertexID0))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("CreateEdge: VertexID %d doesn't exist."), VertexID0.GetValue());
		return FEdgeID::Invalid;
	}

	if (!MeshDescription.IsVertexValid(VertexID1))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("CreateEdge: VertexID %d doesn't exist."), VertexID1.GetValue());
		return FEdgeID::Invalid;
	}

	return MeshDescription.CreateEdge(VertexID0, VertexID1);
}

void UMeshDescriptionBase::CreateEdgeWithID(FEdgeID EdgeID, FVertexID VertexID0, FVertexID VertexID1)
{
	if (MeshDescription.IsEdgeValid(EdgeID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("CreateEdgeWithID: EdgeID %d already exists."), EdgeID.GetValue());
		return;
	}

	if (!MeshDescription.IsVertexValid(VertexID0))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("CreateEdgeWithID: VertexID %d doesn't exist."), VertexID0.GetValue());
		return;
	}

	if (!MeshDescription.IsVertexValid(VertexID1))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("CreateEdgeWithID: VertexID %d doesn't exist."), VertexID1.GetValue());
		return;
	}

	MeshDescription.CreateEdgeWithID(EdgeID, VertexID0, VertexID1);
}

void UMeshDescriptionBase::DeleteEdge(FEdgeID EdgeID, TArray<FVertexID>& OrphanedVertices)
{
	if (MeshDescription.IsEdgeValid(EdgeID))
	{
		MeshDescription.DeleteEdge(EdgeID, &OrphanedVertices);
	}
	else
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("DeleteEdge: EdgeID %d doesn't exist."), EdgeID.GetValue());
	}
}

bool UMeshDescriptionBase::IsEdgeValid(FEdgeID EdgeID) const
{
	return MeshDescription.IsEdgeValid(EdgeID);
}

void UMeshDescriptionBase::ReserveNewTriangles(int32 NumberOfNewTriangles)
{
	MeshDescription.ReserveNewTriangles(NumberOfNewTriangles);
}

FTriangleID UMeshDescriptionBase::CreateTriangle(FPolygonGroupID PolygonGroupID, const TArray<FVertexInstanceID>& VertexInstanceIDs, TArray<FEdgeID>& NewEdgeIDs)
{
	if (!MeshDescription.IsPolygonGroupValid(PolygonGroupID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("CreateTriangle: PolygonGroupID %d doesn't exist."), PolygonGroupID.GetValue());
		return FTriangleID::Invalid;
	}

	return MeshDescription.CreateTriangle(PolygonGroupID, VertexInstanceIDs, &NewEdgeIDs);
}

void UMeshDescriptionBase::CreateTriangleWithID(FTriangleID TriangleID, FPolygonGroupID PolygonGroupID, const TArray<FVertexInstanceID>& VertexInstanceIDs, TArray<FEdgeID>& NewEdgeIDs)
{
	if (MeshDescription.IsTriangleValid(TriangleID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("CreateTriangleWithID: TriangleID %d already exists."), TriangleID.GetValue());
		return;
	}

	if (!MeshDescription.IsPolygonGroupValid(PolygonGroupID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("CreateTriangleWithID: PolygonGroupID %d doesn't exist."), PolygonGroupID.GetValue());
		return;
	}

	MeshDescription.CreateTriangleWithID(TriangleID, PolygonGroupID, VertexInstanceIDs, &NewEdgeIDs);
}

void UMeshDescriptionBase::DeleteTriangle(FTriangleID TriangleID, TArray<FEdgeID>& OrphanedEdges, TArray<FVertexInstanceID>& OrphanedVertexInstances, TArray<FPolygonGroupID>& OrphanedPolygonGroups)
{
	if (!MeshDescription.IsTriangleValid(TriangleID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("DeleteTriangle: TriangleID %d doesn't exist."), TriangleID.GetValue());
		return;
	}

	MeshDescription.DeleteTriangle(TriangleID, &OrphanedEdges, &OrphanedVertexInstances, &OrphanedPolygonGroups);
}

bool UMeshDescriptionBase::IsTriangleValid(const FTriangleID TriangleID) const
{
	return MeshDescription.IsTriangleValid(TriangleID);
}

void UMeshDescriptionBase::ReserveNewPolygons(const int32 NumberOfNewPolygons)
{
	MeshDescription.ReserveNewPolygons(NumberOfNewPolygons);
}

FPolygonID UMeshDescriptionBase::CreatePolygon(FPolygonGroupID PolygonGroupID, TArray<FVertexInstanceID>& VertexInstanceIDs, TArray<FEdgeID>& NewEdgeIDs)
{
	if (!MeshDescription.IsPolygonGroupValid(PolygonGroupID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("CreatePolygon: PolygonGroupID %d doesn't exist."), PolygonGroupID.GetValue());
		return FPolygonID::Invalid;
	}

	return MeshDescription.CreatePolygon(PolygonGroupID, VertexInstanceIDs, &NewEdgeIDs);
}

void UMeshDescriptionBase::CreatePolygonWithID(FPolygonID PolygonID, FPolygonGroupID PolygonGroupID, TArray<FVertexInstanceID>& VertexInstanceIDs, TArray<FEdgeID>& NewEdgeIDs)
{
	if (MeshDescription.IsPolygonValid(PolygonID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("CreatePolygonWithID: PolygonID %d already exists."), PolygonID.GetValue());
		return;
	}

	if (!MeshDescription.IsPolygonGroupValid(PolygonGroupID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("CreatePolygonWithID: PolygonGroupID %d doesn't exist."), PolygonGroupID.GetValue());
		return;
	}

	MeshDescription.CreatePolygonWithID(PolygonID, PolygonGroupID, VertexInstanceIDs, &NewEdgeIDs);
}

void UMeshDescriptionBase::DeletePolygon(FPolygonID PolygonID, TArray<FEdgeID>& OrphanedEdges, TArray<FVertexInstanceID>& OrphanedVertexInstances, TArray<FPolygonGroupID>& OrphanedPolygonGroups)
{
	if (!MeshDescription.IsPolygonValid(PolygonID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("DeletePolygon: PolygonID %d doesn't exist."), PolygonID.GetValue());
		return;
	}

	MeshDescription.DeletePolygon(PolygonID, &OrphanedEdges, &OrphanedVertexInstances, &OrphanedPolygonGroups);
}

bool UMeshDescriptionBase::IsPolygonValid(FPolygonID PolygonID) const
{
	return MeshDescription.IsPolygonValid(PolygonID);
}

void UMeshDescriptionBase::ReserveNewPolygonGroups(int32 NumberOfNewPolygonGroups)
{
	MeshDescription.ReserveNewPolygonGroups(NumberOfNewPolygonGroups);
}

FPolygonGroupID UMeshDescriptionBase::CreatePolygonGroup()
{
	return MeshDescription.CreatePolygonGroup();
}

void UMeshDescriptionBase::CreatePolygonGroupWithID(FPolygonGroupID PolygonGroupID)
{
	if (MeshDescription.IsPolygonGroupValid(PolygonGroupID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("CreatePolygonGroupWithID: PolygonGroupID %d already exists."), PolygonGroupID.GetValue());
		return;
	}

	MeshDescription.CreatePolygonGroupWithID(PolygonGroupID);
}

void UMeshDescriptionBase::DeletePolygonGroup(FPolygonGroupID PolygonGroupID)
{
	if (!MeshDescription.IsPolygonGroupValid(PolygonGroupID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("DeletePolygonGroup: FPolygonGroupID %d doesn't exist."), PolygonGroupID.GetValue());
		return;
	}

	MeshDescription.DeletePolygonGroup(PolygonGroupID);
}

bool UMeshDescriptionBase::IsPolygonGroupValid(FPolygonGroupID PolygonGroupID) const
{
	return MeshDescription.IsPolygonGroupValid(PolygonGroupID);
}


//////////////////////////////////////////////////////////////////////
// Vertex operations

bool UMeshDescriptionBase::IsVertexOrphaned(FVertexID VertexID) const
{
	if (!MeshDescription.IsVertexValid(VertexID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("IsVertexOrphaned: VertexID %d doesn't exist."), VertexID.GetValue());
		return false;
	}

	return MeshDescription.IsVertexOrphaned(VertexID);
}

FEdgeID UMeshDescriptionBase::GetVertexPairEdge(FVertexID VertexID0, FVertexID VertexID1) const
{
	if (!MeshDescription.IsVertexValid(VertexID0))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetVertexPairEdge: VertexID %d doesn't exist."), VertexID0.GetValue());
		return FEdgeID::Invalid;
	}

	if (!MeshDescription.IsVertexValid(VertexID1))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetVertexPairEdge: VertexID %d doesn't exist."), VertexID1.GetValue());
		return FEdgeID::Invalid;
	}

	return MeshDescription.GetVertexPairEdge(VertexID0, VertexID1);
}

void UMeshDescriptionBase::GetVertexConnectedEdges(FVertexID VertexID, TArray<FEdgeID>& OutEdgeIDs) const
{
	if (!MeshDescription.IsVertexValid(VertexID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetVertexConnectedEdges: VertexID %d doesn't exist."), VertexID.GetValue());
		return;
	}

	OutEdgeIDs = MeshDescription.GetVertexConnectedEdges(VertexID);
}

int32 UMeshDescriptionBase::GetNumVertexConnectedEdges(FVertexID VertexID) const
{
	if (!MeshDescription.IsVertexValid(VertexID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetNumVertexConnectedEdges: VertexID %d doesn't exist."), VertexID.GetValue());
		return 0;
	}

	return MeshDescription.GetNumVertexConnectedEdges(VertexID);
}

void UMeshDescriptionBase::GetVertexVertexInstances(FVertexID VertexID, TArray<FVertexInstanceID>& OutVertexInstanceIDs) const
{
	if (!MeshDescription.IsVertexValid(VertexID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetVertexVertexInstances: VertexID %d doesn't exist."), VertexID.GetValue());
		return;
	}

	OutVertexInstanceIDs = MeshDescription.GetVertexVertexInstances(VertexID);
}

int32 UMeshDescriptionBase::GetNumVertexVertexInstances(FVertexID VertexID) const
{
	if (!MeshDescription.IsVertexValid(VertexID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetNumVertexVertexInstances: VertexID %d doesn't exist."), VertexID.GetValue());
		return 0;
	}

	return MeshDescription.GetNumVertexVertexInstances(VertexID);
}

void UMeshDescriptionBase::GetVertexConnectedTriangles(FVertexID VertexID, TArray<FTriangleID>& OutConnectedTriangleIDs) const
{
	if (!MeshDescription.IsVertexValid(VertexID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetVertexConnectedTriangles: VertexID %d doesn't exist."), VertexID.GetValue());
		return;
	}

	MeshDescription.GetVertexConnectedTriangles(VertexID, OutConnectedTriangleIDs);
}

int32 UMeshDescriptionBase::GetNumVertexConnectedTriangles(FVertexID VertexID) const
{
	if (!MeshDescription.IsVertexValid(VertexID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetNumVertexConnectedTriangles: VertexID %d doesn't exist."), VertexID.GetValue());
		return 0;
	}

	return MeshDescription.GetNumVertexConnectedTriangles(VertexID);
}

void UMeshDescriptionBase::GetVertexConnectedPolygons(FVertexID VertexID, TArray<FPolygonID>& OutConnectedPolygonIDs) const
{
	if (!MeshDescription.IsVertexValid(VertexID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetVertexConnectedPolygons: VertexID %d doesn't exist."), VertexID.GetValue());
		return;
	}

	MeshDescription.GetVertexConnectedPolygons(VertexID, OutConnectedPolygonIDs);
}

int32 UMeshDescriptionBase::GetNumVertexConnectedPolygons(FVertexID VertexID) const
{
	if (!MeshDescription.IsVertexValid(VertexID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetNumVertexConnectedPolygons: VertexID %d doesn't exist."), VertexID.GetValue());
		return 0;
	}

	return MeshDescription.GetNumVertexConnectedPolygons(VertexID);
}

void UMeshDescriptionBase::GetVertexAdjacentVertices(FVertexID VertexID, TArray<FVertexID>& OutAdjacentVertexIDs) const
{
	if (!MeshDescription.IsVertexValid(VertexID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetVertexAdjacentVertices: VertexID %d doesn't exist."), VertexID.GetValue());
		return;
	}

	MeshDescription.GetVertexAdjacentVertices(VertexID, OutAdjacentVertexIDs);
}

FVector UMeshDescriptionBase::GetVertexPosition(FVertexID VertexID) const
{
	if (!MeshDescription.IsVertexValid(VertexID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetVertexAttribute: VertexID %d doesn't exist."), VertexID.GetValue());
		return FVector::ZeroVector;
	}

	if (!MeshDescription.VertexAttributes().HasAttribute(MeshAttribute::Vertex::Position))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetVertexAttribute: VertexAttribute Position doesn't exist."));
		return FVector::ZeroVector;
	}

	return MeshDescription.VertexAttributes().GetAttribute<FVector>(VertexID, MeshAttribute::Vertex::Position);
}

void UMeshDescriptionBase::SetVertexPosition(FVertexID VertexID, const FVector& Position)
{
	if (!MeshDescription.IsVertexValid(VertexID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("SetVertexAttribute: VertexID %d doesn't exist."), VertexID.GetValue());
		return;
	}

	if (!MeshDescription.VertexAttributes().HasAttribute(MeshAttribute::Vertex::Position))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("SetVertexAttribute: VertexAttribute Position doesn't exist."));
		return;
	}

	MeshDescription.VertexAttributes().SetAttribute(VertexID, MeshAttribute::Vertex::Position, 0, Position);
}


//////////////////////////////////////////////////////////////////////
// Vertex instance operations

FVertexID UMeshDescriptionBase::GetVertexInstanceVertex(FVertexInstanceID VertexInstanceID) const
{
	if (!MeshDescription.IsVertexInstanceValid(VertexInstanceID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetVertexInstanceVertex: VertexInstanceID %d doesn't exist."), VertexInstanceID.GetValue());
		return FVertexID::Invalid;
	}

	return MeshDescription.GetVertexInstanceVertex(VertexInstanceID);
}

FEdgeID UMeshDescriptionBase::GetVertexInstancePairEdge(FVertexInstanceID VertexInstanceID0, FVertexInstanceID VertexInstanceID1) const
{
	if (!MeshDescription.IsVertexInstanceValid(VertexInstanceID0))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetVertexInstancePairEdge: VertexInstanceID %d doesn't exist."), VertexInstanceID0.GetValue());
		return FEdgeID::Invalid;
	}

	if (!MeshDescription.IsVertexInstanceValid(VertexInstanceID1))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetVertexInstancePairEdge: VertexInstanceID %d doesn't exist."), VertexInstanceID1.GetValue());
		return FEdgeID::Invalid;
	}

	return MeshDescription.GetVertexInstancePairEdge(VertexInstanceID0, VertexInstanceID1);
}

void UMeshDescriptionBase::GetVertexInstanceConnectedTriangles(FVertexInstanceID VertexInstanceID, TArray<FTriangleID>& OutConnectedTriangleIDs) const
{
	if (!MeshDescription.IsVertexInstanceValid(VertexInstanceID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetVertexInstanceConnectedTriangles: VertexInstanceID %d doesn't exist."), VertexInstanceID.GetValue());
		return;
	}

	OutConnectedTriangleIDs = MeshDescription.GetVertexInstanceConnectedTriangles(VertexInstanceID);
}

int32 UMeshDescriptionBase::GetNumVertexInstanceConnectedTriangles(FVertexInstanceID VertexInstanceID) const
{
	if (!MeshDescription.IsVertexInstanceValid(VertexInstanceID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetNumVertexInstanceConnectedTriangles: VertexInstanceID %d doesn't exist."), VertexInstanceID.GetValue());
		return 0;
	}

	return MeshDescription.GetNumVertexInstanceConnectedTriangles(VertexInstanceID);
}

void UMeshDescriptionBase::GetVertexInstanceConnectedPolygons(FVertexInstanceID VertexInstanceID, TArray<FPolygonID>& OutConnectedPolygonIDs) const
{
	if (!MeshDescription.IsVertexInstanceValid(VertexInstanceID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetVertexInstanceConnectedPolygons: VertexInstanceID %d doesn't exist."), VertexInstanceID.GetValue());
		return;
	}

	MeshDescription.GetVertexInstanceConnectedPolygons(VertexInstanceID, OutConnectedPolygonIDs);
}

int32 UMeshDescriptionBase::GetNumVertexInstanceConnectedPolygons(FVertexInstanceID VertexInstanceID) const
{
	if (!MeshDescription.IsVertexInstanceValid(VertexInstanceID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetNumVertexInstanceConnectedPolygons: VertexInstanceID %d doesn't exist."), VertexInstanceID.GetValue());
		return 0;
	}

	return MeshDescription.GetNumVertexInstanceConnectedPolygons(VertexInstanceID);
}


//////////////////////////////////////////////////////////////////////
// Edge operations

bool UMeshDescriptionBase::IsEdgeInternal(FEdgeID EdgeID) const
{
	if (!MeshDescription.IsEdgeValid(EdgeID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("IsEdgeInternal: EdgeID %d doesn't exist."), EdgeID.GetValue());
		return false;
	}

	return MeshDescription.IsEdgeInternal(EdgeID);
}

bool UMeshDescriptionBase::IsEdgeInternalToPolygon(FEdgeID EdgeID, FPolygonID PolygonID) const
{
	if (!MeshDescription.IsEdgeValid(EdgeID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("IsEdgeInternalToPolygon: EdgeID %d doesn't exist."), EdgeID.GetValue());
		return false;
	}

	if (!MeshDescription.IsPolygonValid(PolygonID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("IsEdgeInternalToPolygon: PolygonID %d doesn't exist."), PolygonID.GetValue());
		return false;
	}

	return MeshDescription.IsEdgeInternalToPolygon(EdgeID, PolygonID);
}

void UMeshDescriptionBase::GetEdgeConnectedTriangles(FEdgeID EdgeID, TArray<FTriangleID>& OutConnectedTriangleIDs) const
{
	if (!MeshDescription.IsEdgeValid(EdgeID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetEdgeConnectedTriangles: EdgeID %d doesn't exist."), EdgeID.GetValue());
		return;
	}

	OutConnectedTriangleIDs = MeshDescription.GetEdgeConnectedTriangles(EdgeID);
}

int32 UMeshDescriptionBase::GetNumEdgeConnectedTriangles(FEdgeID EdgeID) const
{
	if (!MeshDescription.IsEdgeValid(EdgeID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetNumEdgeConnectedTriangles: EdgeID %d doesn't exist."), EdgeID.GetValue());
		return 0;
	}

	return MeshDescription.GetNumEdgeConnectedTriangles(EdgeID);
}

void UMeshDescriptionBase::GetEdgeConnectedPolygons(FEdgeID EdgeID, TArray<FPolygonID>& OutConnectedPolygonIDs) const
{
	if (!MeshDescription.IsEdgeValid(EdgeID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetEdgeConnectedPolygons: EdgeID %d doesn't exist."), EdgeID.GetValue());
		return;
	}

	MeshDescription.GetEdgeConnectedPolygons(EdgeID, OutConnectedPolygonIDs);
}

int32 UMeshDescriptionBase::GetNumEdgeConnectedPolygons(FEdgeID EdgeID) const
{
	if (!MeshDescription.IsEdgeValid(EdgeID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetNumEdgeConnectedPolygons: EdgeID %d doesn't exist."), EdgeID.GetValue());
		return 0;
	}

	return MeshDescription.GetNumEdgeConnectedPolygons(EdgeID);
}

FVertexID UMeshDescriptionBase::GetEdgeVertex(FEdgeID EdgeID, int32 VertexNumber) const
{
	if (!MeshDescription.IsEdgeValid(EdgeID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetEdgeVertex: EdgeID %d doesn't exist."), EdgeID.GetValue());
		return FVertexID::Invalid;
	}

	if (VertexNumber != 0 && VertexNumber != 1)
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetEdgeVertex: invalid vertex number %d."), VertexNumber);
		return FVertexID::Invalid;
	}

	return MeshDescription.GetEdgeVertex(EdgeID, VertexNumber);
}

void UMeshDescriptionBase::GetEdgeVertices(const FEdgeID EdgeID, TArray<FVertexID>& OutVertexIDs) const
{
	if (!MeshDescription.IsEdgeValid(EdgeID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetEdgeVertices: EdgeID %d doesn't exist."), EdgeID.GetValue());
		return;
	}

	Algo::Copy(MeshDescription.GetEdgeVertices(EdgeID), OutVertexIDs);
}


//////////////////////////////////////////////////////////////////////
// Triangle operations

FPolygonID UMeshDescriptionBase::GetTrianglePolygon(FTriangleID TriangleID) const
{
	if (!MeshDescription.IsTriangleValid(TriangleID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetTrianglePolygon: TriangleID %d doesn't exist."), TriangleID.GetValue());
		return FPolygonID::Invalid;
	}

	return MeshDescription.GetTrianglePolygon(TriangleID);
}

FPolygonGroupID UMeshDescriptionBase::GetTrianglePolygonGroup(FTriangleID TriangleID) const
{
	if (!MeshDescription.IsTriangleValid(TriangleID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetTrianglePolygonGroup: TriangleID %d doesn't exist."), TriangleID.GetValue());
		return FPolygonGroupID::Invalid;
	}

	return MeshDescription.GetTrianglePolygonGroup(TriangleID);
}

bool UMeshDescriptionBase::IsTrianglePartOfNgon(FTriangleID TriangleID) const
{
	if (!MeshDescription.IsTriangleValid(TriangleID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("IsTrianglePartOfNgon: TriangleID %d doesn't exist."), TriangleID.GetValue());
		return false;
	}

	return MeshDescription.IsTrianglePartOfNgon(TriangleID);
}

void UMeshDescriptionBase::GetTriangleVertexInstances(FTriangleID TriangleID, TArray<FVertexInstanceID>& OutVertexInstanceIDs) const
{
	if (!MeshDescription.IsTriangleValid(TriangleID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetTriangleVertexInstances: TriangleID %d doesn't exist."), TriangleID.GetValue());
		return;
	}

	Algo::Copy(MeshDescription.GetTriangleVertexInstances(TriangleID), OutVertexInstanceIDs);
}

FVertexInstanceID UMeshDescriptionBase::GetTriangleVertexInstance(FTriangleID TriangleID, int32 Index) const
{
	if (!MeshDescription.IsTriangleValid(TriangleID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetTriangleVertexInstance: TriangleID %d doesn't exist."), TriangleID.GetValue());
		return FVertexInstanceID::Invalid;
	}

	if (Index < 0 || Index > 2)
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetTriangleVertexInstance: invalid vertex index %d."), Index);
		return FVertexInstanceID::Invalid;
	}

	return MeshDescription.GetTriangleVertexInstance(TriangleID, Index);
}

void UMeshDescriptionBase::GetTriangleVertices(FTriangleID TriangleID, TArray<FVertexID>& OutVertexIDs) const
{
	if (!MeshDescription.IsTriangleValid(TriangleID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetTriangleVertices: TriangleID %d doesn't exist."), TriangleID.GetValue());
		return;
	}

	OutVertexIDs.SetNumUninitialized(3);
	MeshDescription.GetTriangleVertices(TriangleID, OutVertexIDs);
}

void UMeshDescriptionBase::GetTriangleEdges(FTriangleID TriangleID, TArray<FEdgeID>& OutEdgeIDs) const
{
	if (!MeshDescription.IsTriangleValid(TriangleID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetTriangleEdges: TriangleID %d doesn't exist."), TriangleID.GetValue());
		return;
	}

	OutEdgeIDs.SetNumUninitialized(3);
	MeshDescription.GetTriangleEdges(TriangleID, OutEdgeIDs);
}

void UMeshDescriptionBase::GetTriangleAdjacentTriangles(FTriangleID TriangleID, TArray<FTriangleID>& OutTriangleIDs) const
{
	if (!MeshDescription.IsTriangleValid(TriangleID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetTriangleAdjacentTriangles: TriangleID %d doesn't exist."), TriangleID.GetValue());
		return;
	}

	MeshDescription.GetTriangleAdjacentTriangles(TriangleID, OutTriangleIDs);
}

FVertexInstanceID UMeshDescriptionBase::GetVertexInstanceForTriangleVertex(FTriangleID TriangleID, FVertexID VertexID) const
{
	if (!MeshDescription.IsTriangleValid(TriangleID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetVertexInstanceForTriangleVertex: TriangleID %d doesn't exist."), TriangleID.GetValue());
		return FVertexInstanceID::Invalid;
	}

	return MeshDescription.GetVertexInstanceForTriangleVertex(TriangleID, VertexID);
}


//////////////////////////////////////////////////////////////////////
// Polygon operations

void UMeshDescriptionBase::GetPolygonTriangles(FPolygonID PolygonID, TArray<FTriangleID>& OutTriangleIDs) const
{
	if (!MeshDescription.IsPolygonValid(PolygonID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetPolygonTriangles: PolygonID %d doesn't exist."), PolygonID.GetValue());
		return;
	}

	OutTriangleIDs = MeshDescription.GetPolygonTriangleIDs(PolygonID);
}

int32 UMeshDescriptionBase::GetNumPolygonTriangles(FPolygonID PolygonID) const
{
	if (!MeshDescription.IsPolygonValid(PolygonID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetNumPolygonTriangles: PolygonID %d doesn't exist."), PolygonID.GetValue());
		return 0;
	}

	return MeshDescription.GetNumPolygonTriangles(PolygonID);
}

void UMeshDescriptionBase::GetPolygonVertexInstances(FPolygonID PolygonID, TArray<FVertexInstanceID>& OutVertexInstanceIDs) const
{
	if (!MeshDescription.IsPolygonValid(PolygonID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetPolygonVertexInstances: PolygonID %d doesn't exist."), PolygonID.GetValue());
		return;
	}

	OutVertexInstanceIDs = MeshDescription.GetPolygonVertexInstances(PolygonID);
}

int32 UMeshDescriptionBase::GetNumPolygonVertices(FPolygonID PolygonID) const
{
	if (!MeshDescription.IsPolygonValid(PolygonID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetNumPolygonVertices: PolygonID %d doesn't exist."), PolygonID.GetValue());
		return 0;
	}

	return MeshDescription.GetNumPolygonVertices(PolygonID);
}

void UMeshDescriptionBase::GetPolygonVertices(FPolygonID PolygonID, TArray<FVertexID>& OutVertexIDs) const
{
	if (!MeshDescription.IsPolygonValid(PolygonID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetPolygonVertices: PolygonID %d doesn't exist."), PolygonID.GetValue());
		return;
	}

	MeshDescription.GetPolygonVertices(PolygonID, OutVertexIDs);
}

void UMeshDescriptionBase::GetPolygonPerimeterEdges(FPolygonID PolygonID, TArray<FEdgeID>& OutEdgeIDs) const
{
	if (!MeshDescription.IsPolygonValid(PolygonID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetPolygonPerimeterEdges: PolygonID %d doesn't exist."), PolygonID.GetValue());
		return;
	}

	MeshDescription.GetPolygonPerimeterEdges(PolygonID, OutEdgeIDs);
}

void UMeshDescriptionBase::GetPolygonInternalEdges(FPolygonID PolygonID, TArray<FEdgeID>& OutEdgeIDs) const
{
	if (!MeshDescription.IsPolygonValid(PolygonID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetPolygonInternalEdges: PolygonID %d doesn't exist."), PolygonID.GetValue());
		return;
	}

	MeshDescription.GetPolygonInternalEdges(PolygonID, OutEdgeIDs);
}

int32 UMeshDescriptionBase::GetNumPolygonInternalEdges(FPolygonID PolygonID) const
{
	if (!MeshDescription.IsPolygonValid(PolygonID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetNumPolygonInternalEdges: PolygonID %d doesn't exist."), PolygonID.GetValue());
		return 0;
	}

	return MeshDescription.GetNumPolygonInternalEdges(PolygonID);
}

void UMeshDescriptionBase::GetPolygonAdjacentPolygons(FPolygonID PolygonID, TArray<FPolygonID>& OutPolygonIDs) const
{
	if (!MeshDescription.IsPolygonValid(PolygonID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetPolygonAdjacentPolygons: PolygonID %d doesn't exist."), PolygonID.GetValue());
		return;
	}

	MeshDescription.GetPolygonAdjacentPolygons(PolygonID, OutPolygonIDs);
}

FPolygonGroupID UMeshDescriptionBase::GetPolygonPolygonGroup(FPolygonID PolygonID) const
{
	if (!MeshDescription.IsPolygonValid(PolygonID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetPolygonPolygonGroup: PolygonID %d doesn't exist."), PolygonID.GetValue());
		return FPolygonGroupID::Invalid;
	}

	return MeshDescription.GetPolygonPolygonGroup(PolygonID);
}

FVertexInstanceID UMeshDescriptionBase::GetVertexInstanceForPolygonVertex(FPolygonID PolygonID, FVertexID VertexID) const
{
	if (!MeshDescription.IsPolygonValid(PolygonID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetVertexInstanceForPolygonVertex: PolygonID %d doesn't exist."), PolygonID.GetValue());
		return FVertexInstanceID::Invalid;
	}

	return MeshDescription.GetVertexInstanceForPolygonVertex(PolygonID, VertexID);
}

void UMeshDescriptionBase::SetPolygonVertexInstance(FPolygonID PolygonID, int32 PerimeterIndex, FVertexInstanceID VertexInstanceID)
{
	if (!MeshDescription.IsPolygonValid(PolygonID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("SetPolygonVertexInstance: PolygonID %d doesn't exist."), PolygonID.GetValue());
		return;
	}

	if (PerimeterIndex >= MeshDescription.GetNumPolygonVertices(PolygonID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("SetPolygonVertexInstance: Out of range vertex index %d."), PerimeterIndex);
		return;
	}

	if (!MeshDescription.IsVertexInstanceValid(VertexInstanceID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("SetPolygonVertexInstance: VertexInstanceID %d doesn't exist."), VertexInstanceID.GetValue());
		return;
	}

	MeshDescription.SetPolygonVertexInstance(PolygonID, PerimeterIndex, VertexInstanceID);
}

void UMeshDescriptionBase::SetPolygonPolygonGroup(FPolygonID PolygonID, FPolygonGroupID PolygonGroupID)
{
	if (!MeshDescription.IsPolygonValid(PolygonID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("SetPolygonPolygonGroup: PolygonID %d doesn't exist."), PolygonID.GetValue());
		return;
	}

	if (!MeshDescription.IsPolygonGroupValid(PolygonGroupID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("SetPolygonPolygonGroup: PolygonGroupID %d doesn't exist."), PolygonGroupID.GetValue());
		return;
	}

	MeshDescription.SetPolygonPolygonGroup(PolygonID, PolygonGroupID);
}

void UMeshDescriptionBase::ReversePolygonFacing(FPolygonID PolygonID)
{
	if (!MeshDescription.IsPolygonValid(PolygonID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("ReversePolygonFacing: PolygonID %d doesn't exist."), PolygonID.GetValue());
		return;
	}

	MeshDescription.ReversePolygonFacing(PolygonID);
}

void UMeshDescriptionBase::ComputePolygonTriangulation(FPolygonID PolygonID)
{
	if (!MeshDescription.IsPolygonValid(PolygonID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("ComputePolygonTriangulation: PolygonID %d doesn't exist."), PolygonID.GetValue());
		return;
	}

	MeshDescription.ComputePolygonTriangulation(PolygonID);
}


//////////////////////////////////////////////////////////////////////
// Polygon group operations

void UMeshDescriptionBase::GetPolygonGroupPolygons(FPolygonGroupID PolygonGroupID, TArray<FPolygonID>& OutPolygonIDs) const
{
	if (!MeshDescription.IsPolygonGroupValid(PolygonGroupID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetPolygonGroupPolygons: PolygonGroupID %d doesn't exist."), PolygonGroupID.GetValue());
		return;
	}

	OutPolygonIDs = MeshDescription.GetPolygonGroupPolygons(PolygonGroupID);
}

int32 UMeshDescriptionBase::GetNumPolygonGroupPolygons(FPolygonGroupID PolygonGroupID) const
{
	if (!MeshDescription.IsPolygonGroupValid(PolygonGroupID))
	{
		UE_LOG(LogMeshDescription, Warning, TEXT("GetNumPolygonGroupPolygons: PolygonGroupID %d doesn't exist."), PolygonGroupID.GetValue());
		return 0;
	}

	return MeshDescription.GetNumPolygonGroupPolygons(PolygonGroupID);
}
