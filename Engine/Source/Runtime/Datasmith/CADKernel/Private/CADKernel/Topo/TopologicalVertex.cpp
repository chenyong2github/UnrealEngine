// Copyright Epic Games, Inc. All Rights Reserved.

#include "CADKernel/Topo/TopologicalVertex.h"

#include "CADKernel/Mesh/Structure/ModelMesh.h"
#include "CADKernel/Mesh/Structure/VertexMesh.h"
#include "CADKernel/Topo/TopologicalEdge.h"

using namespace CADKernel;


#ifdef CADKERNEL_DEV
FInfoEntity& FTopologicalVertex::GetInfo(FInfoEntity& Info) const
{
	return FTopologicalEntity::GetInfo(Info)
		.Add(TEXT("Link"), TopologicalLink)
		.Add(TEXT("Position"), Coordinates)
		.Add(TEXT("ConnectedEdges"), ConnectedEdges)
		.Add(TEXT("mesh"), Mesh);
}
#endif

void FTopologicalVertex::AddConnectedEdge(TSharedRef<FTopologicalEdge> Edge)
{
	ConnectedEdges.Add(Edge);
}

void FTopologicalVertex::RemoveConnectedEdge(TSharedRef<FTopologicalEdge> Edge)
{
	for (int32 EdgeIndex = 0; EdgeIndex < ConnectedEdges.Num(); EdgeIndex++)
	{
		if(ConnectedEdges[EdgeIndex]==Edge) 
		{
			ConnectedEdges.RemoveAt(EdgeIndex);
			return;
		}
	}
	ensureCADKernel(false);
}

bool FTopologicalVertex::IsBorderVertex()
{
	for (TWeakPtr<FTopologicalEdge> Edge : GetConnectedEdges())
	{
		if (Edge.Pin()->GetTwinsEntityCount() == 1)
		{
			return true;
		}
	}
	return false;
}

TSharedPtr<FEntityGeom> FTopologicalVertex::ApplyMatrix(const FMatrixH& InMatrix) const
{
	FPoint transformedPoint = InMatrix.Multiply(Coordinates);
	return FEntity::MakeShared<FTopologicalVertex>(transformedPoint);
}

void FTopologicalVertex::GetConnectedEdges(TSharedPtr<FTopologicalVertex> OtherVertex, TArray<TSharedPtr<FTopologicalEdge>>& Edges) const
{
	Edges.Reserve(GetTwinsEntityCount());

	TSharedPtr<TTopologicalLink<FTopologicalVertex>> OtherVertexLink = OtherVertex->GetLink();
	for (TWeakPtr<FTopologicalVertex> Vertex : GetTwinsEntities())
	{
		ensureCADKernel(Vertex.IsValid());
		for (TWeakPtr<FTopologicalEdge> Edge : Vertex.Pin()->GetConnectedEdges())
		{
			ensureCADKernel(Edge.IsValid());
			if (Edge.Pin()->GetOtherVertex(Vertex.Pin().ToSharedRef())->GetLink() == OtherVertexLink)
			{
				Edges.Add(Edge.Pin());
			}
		}
	}
}

void FTopologicalVertex::Link(TSharedRef<FTopologicalVertex> Twin)
{
	// The active vertex is always the closest of the Barycenter
	if (TopologicalLink.IsValid() && Twin->TopologicalLink.IsValid())
	{
		if (TopologicalLink == Twin->TopologicalLink)
		{
			return;
		}
	}

	FPoint Barycenter = GetBarycenter() * (double) GetTwinsEntityCount()
		+ Twin->GetBarycenter() * (double)Twin->GetTwinsEntityCount();

	MakeLink(Twin);

	Barycenter /= (double) GetTwinsEntityCount();
	GetLink()->SetBarycenter(Barycenter);

	// Find the closest vertex of the Barycenter
	GetLink()->DefineActiveEntity();
}

void FVertexLink::ComputeBarycenter()
{
	Barycenter = FPoint::ZeroPoint;
	for (TWeakPtr<FTopologicalVertex> Vertex : TwinsEntities)
	{
		Barycenter += Vertex.Pin()->Coordinates;
	}
	Barycenter /= TwinsEntities.Num();
}

void FVertexLink::DefineActiveEntity()
{
	double DistanceSquare = HUGE_VALUE;
	TWeakPtr<FTopologicalVertex> ClosedVertex = TwinsEntities.HeapTop();
	for (const TWeakPtr<FTopologicalVertex>& Vertex : TwinsEntities)
	{
		double Square = Vertex.Pin()->SquareDistance(Barycenter);
		if (Square < DistanceSquare)
		{
			DistanceSquare = Square;
			ClosedVertex = Vertex;
			if (FMath::IsNearlyZero(Square))
			{
				break;
			}
		}
	}
	ActiveEntity = ClosedVertex;
}

TSharedRef<FVertexMesh> FTopologicalVertex::GetOrCreateMesh(TSharedRef<FModelMesh>& MeshModel)
{
	if (!IsActiveEntity())
	{
		return GetLinkActiveEntity()->GetOrCreateMesh(MeshModel);
	}

	if (!Mesh.IsValid())
	{
		Mesh = FEntity::MakeShared<FVertexMesh>(MeshModel, StaticCastSharedRef<FTopologicalVertex>(AsShared()));

		Mesh->GetNodeCoordinates().Emplace(GetBarycenter());
		Mesh->RegisterCoordinates();
		MeshModel->AddMesh(Mesh.ToSharedRef());
		SetMeshed();
	}
	return Mesh.ToSharedRef();
}

void FTopologicalVertex::SpawnIdent(FDatabase& Database)
{
	if (!FEntity::SetId(Database))
	{
		return;
	}

	if (TopologicalLink.IsValid())
	{
		TopologicalLink->SpawnIdent(Database);
	}

	if(Mesh.IsValid())
	{
		Mesh->SpawnIdent(Database);
	}
}


#ifdef CADKERNEL_DEV
FInfoEntity& TTopologicalLink<FTopologicalVertex>::GetInfo(FInfoEntity& Info) const
{
	return FEntity::GetInfo(Info)
		.Add(TEXT("active Entity"), ActiveEntity)
		.Add(TEXT("twin Entities"), TwinsEntities);
}

FInfoEntity& FVertexLink::GetInfo(FInfoEntity& Info) const
{
	return TTopologicalLink<FTopologicalVertex>::GetInfo(Info)
		.Add(TEXT("barycenter"), Barycenter);
}
#endif
