// Copyright Epic Games, Inc. All Rights Reserved.

#include "CADKernel/Topo/TopologicalVertex.h"

#include "CADKernel/Mesh/Structure/ModelMesh.h"
#include "CADKernel/Mesh/Structure/VertexMesh.h"
#include "CADKernel/Topo/TopologicalEdge.h"

#ifdef CADKERNEL_DEV
CADKernel::FInfoEntity& CADKernel::FTopologicalVertex::GetInfo(FInfoEntity& Info) const
{
	return FTopologicalEntity::GetInfo(Info)
		.Add(TEXT("Link"), TopologicalLink)
		.Add(TEXT("Position"), Coordinates)
		.Add(TEXT("ConnectedEdges"), ConnectedEdges)
		.Add(TEXT("mesh"), Mesh);
}
#endif

void CADKernel::FTopologicalVertex::AddConnectedEdge(FTopologicalEdge& Edge)
{
	ConnectedEdges.Add(&Edge);
}

void CADKernel::FTopologicalVertex::RemoveConnectedEdge(FTopologicalEdge& Edge)
{
	for (int32 EdgeIndex = 0; EdgeIndex < ConnectedEdges.Num(); EdgeIndex++)
	{
		if (ConnectedEdges[EdgeIndex] == &Edge)
		{
			ConnectedEdges.RemoveAt(EdgeIndex);
			return;
		}
	}
	ensureCADKernel(false);
}

bool CADKernel::FTopologicalVertex::IsBorderVertex()
{
	for (FTopologicalVertex* Vertex : GetTwinsEntities())
	{
		for (const FTopologicalEdge* Edge : Vertex->GetDirectConnectedEdges())
		{
			if (Edge->GetTwinsEntityCount() == 1)
			{
				return true;
			}
		}
	}
	return false;
}

TSharedPtr<CADKernel::FEntityGeom> CADKernel::FTopologicalVertex::ApplyMatrix(const FMatrixH& InMatrix) const
{
	FPoint transformedPoint = InMatrix.Multiply(Coordinates);
	return FEntity::MakeShared<FTopologicalVertex>(transformedPoint);
}

void CADKernel::FTopologicalVertex::GetConnectedEdges(const FTopologicalVertex& OtherVertex, TArray<FTopologicalEdge*>& OutEdges) const
{
	OutEdges.Reserve(GetTwinsEntityCount());

	TSharedPtr<TTopologicalLink<FTopologicalVertex>> OtherVertexLink = OtherVertex.GetLink();
	for (const FTopologicalVertex* Vertex : GetTwinsEntities())
	{
		ensureCADKernel(Vertex);
		for (FTopologicalEdge* Edge : Vertex->GetDirectConnectedEdges())
		{
			ensureCADKernel(Edge);
			if (Edge->GetOtherVertex(*Vertex)->GetLink() == OtherVertexLink)
			{
				OutEdges.Add(Edge);
			}
		}
	}
}

void CADKernel::FTopologicalVertex::Link(FTopologicalVertex& Twin)
{
	// The active vertex is always the closest of the Barycenter
	if (TopologicalLink.IsValid() && Twin.TopologicalLink.IsValid())
	{
		if (TopologicalLink == Twin.TopologicalLink)
		{
			return;
		}
	}

	FPoint Barycenter = GetBarycenter() * (double) GetTwinsEntityCount()
		+ Twin.GetBarycenter() * (double)Twin.GetTwinsEntityCount();

	MakeLink(Twin);

	Barycenter /= (double) GetTwinsEntityCount();
	GetLink()->SetBarycenter(Barycenter);

	// Find the closest vertex of the Barycenter
	GetLink()->DefineActiveEntity();
}

void CADKernel::FTopologicalVertex::UnlinkTo(FTopologicalVertex& OtherVertex)
{
	TSharedPtr<FVertexLink> OldLink = GetLink();
	ResetTopologicalLink();
	OtherVertex.ResetTopologicalLink();

	for (FTopologicalVertex* Vertex : OldLink->GetTwinsEntities())
	{
		if (!Vertex|| Vertex == this || Vertex == &OtherVertex)
		{
			continue;
		}

		Vertex->ResetTopologicalLink();
		double Distance1 = Distance(*Vertex);
		double Distance2 = OtherVertex.Distance(*Vertex);
		if (Distance1 < Distance2)
		{
			Link(*Vertex);
		}
		else
		{
			OtherVertex.Link(*Vertex);
		}
	}
}

void CADKernel::FVertexLink::ComputeBarycenter()
{
	Barycenter = FPoint::ZeroPoint;
	for (const FTopologicalVertex* Vertex : TwinsEntities)
	{
		Barycenter += Vertex->Coordinates;
	}
	Barycenter /= TwinsEntities.Num();
}

void CADKernel::FVertexLink::DefineActiveEntity()
{
	if (TwinsEntities.Num() == 0)
	{
		ActiveEntity = nullptr;
		return;
	}

	double DistanceSquare = HUGE_VALUE;
	FTopologicalVertex* ClosedVertex = TwinsEntities.HeapTop();
	for (FTopologicalVertex* Vertex : TwinsEntities)
	{
		double Square = Vertex->SquareDistance(Barycenter);
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

TSharedRef<CADKernel::FVertexMesh> CADKernel::FTopologicalVertex::GetOrCreateMesh(TSharedRef<FModelMesh>& MeshModel)
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

void CADKernel::FTopologicalVertex::SpawnIdent(FDatabase& Database)
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
CADKernel::FInfoEntity& TTopologicalLink<FTopologicalVertex>::GetInfo(FInfoEntity& Info) const
{
	return FEntity::GetInfo(Info)
		.Add(TEXT("active Entity"), ActiveEntity)
		.Add(TEXT("twin Entities"), TwinsEntities);
}

CADKernel::FInfoEntity& CADKernel::FVertexLink::GetInfo(FInfoEntity& Info) const
{
	return TTopologicalLink<FTopologicalVertex>::GetInfo(Info)
		.Add(TEXT("barycenter"), Barycenter);
}
#endif
