// Copyright Epic Games, Inc. All Rights Reserved.

#include "CADKernel/Topo/TopologicalFace.h"

#include "CADKernel/Core/KernelParameters.h"
#include "CADKernel/Core/System.h"
#include "CADKernel/Geo/Curves/RestrictionCurve.h"
#include "CADKernel/Geo/Curves/SegmentCurve.h"
#include "CADKernel/Geo/GeoPoint.h"
#include "CADKernel/Geo/Surfaces/Surface.h"
#include "CADKernel/Mesh/Structure/FaceMesh.h"
#include "CADKernel/Mesh/Structure/Grid.h"
#include "CADKernel/Topo/TopologicalEdge.h"

using namespace CADKernel;


void FTopologicalFace::ComputeBoundary() const
{
	Boundary->Init();
	TArray<TArray<FPoint2D>> TmpLoops;
	Get2DLoopSampling(TmpLoops);

	for (const TArray<FPoint2D>& Loop : TmpLoops)
	{
		for (const FPoint2D& Point : Loop)
		{
			Boundary->ExtendTo(Point);
		}
	}

	// Check with the carrier surface bounds
	CarrierSurface->ExtendBoundaryTo(Boundary);

	Boundary->WidenIfDegenerated();
	Boundary.SetReady();
}

void FTopologicalFace::Presample()
{
	const FSurfacicBoundary& FaceBoundaries = GetBoundary();
	CarrierSurface->Presample(FaceBoundaries, CrossingCoordinates);
}

void FTopologicalFace::ApplyNaturalLoops()
{
	ensureCADKernel(Loops.Num() == 0);

	TArray<TSharedPtr<FTopologicalEdge>> Edges;
	Edges.Reserve(4);

	const FSurfacicBoundary& Bounds = CarrierSurface->GetBoundary();

	TFunction<void(const FPoint&, const FPoint&)> BuildEdge = [&](const FPoint& StartPoint, const FPoint& EndPoint)
	{
		double Tolerance3D = CarrierSurface->Get3DTolerance();
		TSharedRef<FCurve> Curve2D = FEntity::MakeShared<FSegmentCurve>(StartPoint, EndPoint, 2);
		TSharedRef<FRestrictionCurve> Curve3D = FEntity::MakeShared<FRestrictionCurve>(CarrierSurface.ToSharedRef(), Curve2D);
		TSharedPtr<FTopologicalEdge> Edge = FTopologicalEdge::Make(Curve3D);
		if (Edge.IsValid())
		{
			return;
		}
		Edges.Add(Edge);
	};

	FPoint StartPoint;
	FPoint EndPoint;

	// Build 4 bounding edges of the surface
	StartPoint.Set(Bounds.UVBoundaries[EIso::IsoU].Min, Bounds.UVBoundaries[EIso::IsoV].Min);
	EndPoint.Set(Bounds.UVBoundaries[EIso::IsoU].Min, Bounds.UVBoundaries[EIso::IsoV].Max);
	BuildEdge(StartPoint, EndPoint);

	StartPoint.Set(Bounds.UVBoundaries[EIso::IsoU].Min, Bounds.UVBoundaries[EIso::IsoV].Max);
	EndPoint.Set(Bounds.UVBoundaries[EIso::IsoU].Max, Bounds.UVBoundaries[EIso::IsoV].Max);
	BuildEdge(StartPoint, EndPoint);

	StartPoint.Set(Bounds.UVBoundaries[EIso::IsoU].Max, Bounds.UVBoundaries[EIso::IsoV].Max);
	EndPoint.Set(Bounds.UVBoundaries[EIso::IsoU].Max, Bounds.UVBoundaries[EIso::IsoV].Min);
	BuildEdge(StartPoint, EndPoint);

	StartPoint.Set(Bounds.UVBoundaries[EIso::IsoU].Max, Bounds.UVBoundaries[EIso::IsoV].Min);
	EndPoint.Set(Bounds.UVBoundaries[EIso::IsoU].Min, Bounds.UVBoundaries[EIso::IsoV].Min);
	BuildEdge(StartPoint, EndPoint);

	TSharedPtr<FTopologicalEdge> PreviousEdge = Edges.Last();
	for (TSharedPtr<FTopologicalEdge>& Edge : Edges)
	{
		PreviousEdge->GetEndVertex()->Link(Edge->GetStartVertex());
		PreviousEdge = Edge;
	}

	TArray<EOrientation> Orientations;
	Orientations.Init(EOrientation::Front, Edges.Num());

	TSharedPtr<FTopologicalLoop> Loop = FTopologicalLoop::Make(Edges, Orientations, CarrierSurface->Get3DTolerance());
	AddLoop(Loop);
}

void FTopologicalFace::AddLoops(const TArray<TSharedPtr<FTopologicalLoop>>& InLoops)
{
	for (TSharedPtr<FTopologicalLoop> Loop : InLoops)
	{
		AddLoop(Loop);
	}

	for (TSharedPtr<FTopologicalLoop> Loop : InLoops)
	{
		Loop->Orient();
	}
}

void FTopologicalFace::AddLoop(const TSharedPtr<FTopologicalLoop>& InLoop)
{
	TSharedRef<FTopologicalFace> Face = StaticCastSharedRef<FTopologicalFace>(AsShared());
	InLoop->SetSurface(Face);
	if (Loops.Num() > 0)
	{
		InLoop->SetAsInnerBoundary();
	}
	Loops.Add(InLoop);
}

void FTopologicalFace::RemoveLoop(const TSharedPtr<FTopologicalLoop>& Loop)
{
	int32 Index = Loops.Find(Loop);
	if (Index != INDEX_NONE)
	{
		Loop->ResetSurface();
		Loops.RemoveAt(Index);
	}

	if (Loops.Num() == 0)
	{
		SetDeleted();
	}
}

void FTopologicalFace::RemoveLinksWithNeighbours()
{
	for (const TSharedPtr<FTopologicalLoop>& Loop : GetLoops())
	{
		for (const FOrientedEdge& Edge : Loop->GetEdges())
		{
			Edge.Entity->RemoveFromLink();
		}
	}
}

bool FTopologicalFace::HasSameBoundariesAs(const TSharedPtr<FTopologicalFace>& OtherFace) const
{
	int32 EdgeCount = 0;
	for (const TSharedPtr<FTopologicalLoop>& Loop : GetLoops())
	{
		for (const FOrientedEdge& Edge : Loop->GetEdges())
		{
			if (Edge.Entity->IsDegenerated())
			{
				continue;
			}
			Edge.Entity->GetLinkActiveEntity()->SetMarker1();
			++EdgeCount;
		}
	}

	bool bSameBoundary = true;
	int32 OtherFaceEdgeCount = 0;
	for (const TSharedPtr<FTopologicalLoop>& Loop : OtherFace->GetLoops())
	{
		OtherFaceEdgeCount += Loop->EdgeCount();
		for (const FOrientedEdge& Edge : Loop->GetEdges())
		{
			if (Edge.Entity->IsDegenerated())
			{
				continue;
			}
			if (!Edge.Entity->GetLinkActiveEntity()->HasMarker1())
			{
				bSameBoundary = false;
				break;
			}
			++OtherFaceEdgeCount;
		}
	}

	for (const TSharedPtr<FTopologicalLoop>& Loop : GetLoops())
	{
		EdgeCount += Loop->EdgeCount();
		for (const FOrientedEdge& Edge : Loop->GetEdges())
		{
			Edge.Entity->GetLinkActiveEntity()->ResetMarkers();
		}
	}

	if (EdgeCount != OtherFaceEdgeCount)
	{
		bSameBoundary = false;
	}

	return bSameBoundary;
}

TSharedPtr<FTopologicalEdge> FTopologicalFace::GetLinkedEdge(const TSharedPtr<FTopologicalEdge>& LinkedEdge) const
{
	for (TWeakPtr<FTopologicalEdge> TwinEdge : LinkedEdge->GetTwinsEntities())
	{
		if (TwinEdge.Pin()->GetLoop()->GetFace() == AsShared())
		{
			return TwinEdge.Pin();
		}
	}

	return TSharedPtr<FTopologicalEdge>();
}

void FTopologicalFace::GetEdgeIndex(const TSharedPtr<FTopologicalEdge>& Edge, int32& OutBoundaryIndex, int32& OutEdgeIndex) const
{
	OutEdgeIndex = INDEX_NONE;
	for (OutBoundaryIndex = 0; OutBoundaryIndex < Loops.Num(); ++OutBoundaryIndex)
	{
		TSharedPtr<FTopologicalLoop> Loop = Loops[OutBoundaryIndex];
		if ((OutEdgeIndex = Loop->GetEdgeIndex(Edge)) >= 0)
		{
			return;
		}
	}
	OutBoundaryIndex = INDEX_NONE;
}

void FTopologicalFace::EvaluateGrid(FGrid& Grid) const
{
	CarrierSurface->EvaluateGrid(Grid);
}

const void FTopologicalFace::Get2DLoopSampling(TArray<TArray<FPoint2D>>& LoopSamplings) const
{
	LoopSamplings.Empty(GetLoops().Num());

	for (const TSharedPtr<FTopologicalLoop>& Loop : GetLoops())
	{
		TArray<FPoint2D>& LoopSampling2D = LoopSamplings.Emplace_GetRef();
		Loop->Get2DSampling(LoopSampling2D);
	}
}

void FTopologicalFace::SpawnIdent(FDatabase& Database)
{
	if (!FEntity::SetId(Database))
	{
		return;
	}

	SpawnIdentOnEntities(Loops, Database);
	CarrierSurface->SpawnIdent(Database);
	if (Mesh.IsValid())
	{
		Mesh->SpawnIdent(Database);
	}
}

#ifdef CADKERNEL_DEV
FInfoEntity& FTopologicalFace::GetInfo(FInfoEntity& Info) const
{
	return FTopologicalEntity::GetInfo(Info)
		.Add(TEXT("Hosted by"), (TWeakPtr<FEntity>&) HostedBy)
		.Add(TEXT("Carrier Surface"), CarrierSurface)
		.Add(TEXT("Boundary"), (FSurfacicBoundary&) Boundary)
		.Add(TEXT("Loops"), Loops)
		.Add(TEXT("QuadCriteria"), QuadCriteria)
		.Add(TEXT("mesh"), Mesh)
		.Add(*this);
}
#endif

TSharedRef<FFaceMesh> FTopologicalFace::GetOrCreateMesh(const TSharedRef<FModelMesh>& MeshModel)
{
	if (!Mesh.IsValid())
	{
		Mesh = FEntity::MakeShared<FFaceMesh>(MeshModel, StaticCastSharedRef<FTopologicalFace>(AsShared()));
	}
	return Mesh.ToSharedRef();
}

void FTopologicalFace::InitDeltaUs()
{
	CrossingPointDeltaMins[EIso::IsoU].Init(SMALL_NUMBER, CrossingCoordinates[EIso::IsoU].Num() - 1);
	CrossingPointDeltaMaxs[EIso::IsoU].Init(HUGE_VALUE, CrossingCoordinates[EIso::IsoU].Num() - 1);

	CrossingPointDeltaMins[EIso::IsoV].Init(SMALL_NUMBER, CrossingCoordinates[EIso::IsoV].Num() - 1);
	CrossingPointDeltaMaxs[EIso::IsoV].Init(HUGE_VALUE, CrossingCoordinates[EIso::IsoV].Num() - 1);
}

void FTopologicalFace::ChooseFinalDeltaUs()
{
	TFunction<void(const TArray<double>&, TArray<double>&)> ChooseFinalDeltas = [](const TArray<double>& DeltaUMins, TArray<double>& DeltaUMaxs)
	{
		int32 Index = 0;
		for (; Index < DeltaUMins.Num(); ++Index)
		{
			if (DeltaUMins[Index] > DeltaUMaxs[Index])
			{
				DeltaUMaxs[Index] = DeltaUMins[Index];
			}
		}
	};

	ChooseFinalDeltas(CrossingPointDeltaMins[EIso::IsoU], CrossingPointDeltaMaxs[EIso::IsoU]);
	ChooseFinalDeltas(CrossingPointDeltaMins[EIso::IsoV], CrossingPointDeltaMaxs[EIso::IsoV]);
}

// =========================================================================================================================================================================================================
// =========================================================================================================================================================================================================
// =========================================================================================================================================================================================================
//
//
//                                                                            NOT YET REVIEWED
//
//
// =========================================================================================================================================================================================================
// =========================================================================================================================================================================================================
// =========================================================================================================================================================================================================

TSharedPtr<FEntityGeom> FTopologicalFace::ApplyMatrix(const FMatrixH& InMatrix) const
{
	NOT_IMPLEMENTED;
	return TSharedPtr<FEntityGeom>();
}

// Quad ==============================================================================================================================================================================================================================

double FTopologicalFace::GetQuadCriteria()
{
	if (GetQuadType() == EQuadType::Unset)
	{
		return 0;
	}
	return QuadCriteria;
}

void FTopologicalFace::ComputeQuadCriteria()
{
	if (GetQuadType() != EQuadType::Unset)
	{
		QuadCriteria = FMath::Max(Curvatures[EIso::IsoU].Max, Curvatures[EIso::IsoU].Max);
	}
}

void FTopologicalFace::ComputeSurfaceSideProperties()
{
	TFunction<double(const int32)> GetSideLength = [&](const int32 SideIndex)
	{
		TSharedPtr<FTopologicalLoop> Loop = GetLoops()[0];

		double Length = 0;
		int32 NextSideIndex = SideIndex + 1;
		if (NextSideIndex == GetStartSideIndices().Num())
		{
			NextSideIndex = 0;
		}
		int32 EndIndex = GetStartSideIndices()[NextSideIndex];
		for (int32 Index = GetStartSideIndices()[SideIndex]; Index != EndIndex;)
		{
			Length += Loop->GetEdge(Index)->Length();
			if (++Index == Loop->EdgeCount())
			{
				Index = 0;
			}
		}
		return Length;
	};

	Loops[0]->FindSurfaceCorners(SurfaceCorners, StartSideIndices);
	Loops[0]->ComputeBoundaryProperties(StartSideIndices, SideProperties);

	LoopLength = 0;
	for (int32 Index = 0; Index < SurfaceCorners.Num(); ++Index)
	{
		SideProperties[Index].Length3D = GetSideLength(Index);
		LoopLength += SideProperties[Index].Length3D;
	}
}

void FTopologicalFace::DefineSurfaceType()
{
	const double Tolerance3D = CarrierSurface->Get3DTolerance();
	const double GeometricTolerance = 20.0 * Tolerance3D;

	switch (SurfaceCorners.Num())
	{
	case 3:
		QuadType = EQuadType::Triangular;
		break;

	case 4:
		QuadType = EQuadType::Other;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			// If the type is not ISO, the neighbor surface is checked, if it's quad so it's ok...
			if (SideProperties[Index].IsoType == EIso::UndefinedIso)
			{
				TSharedPtr<FTopologicalEdge> Edge = Loops[0]->GetEdge(StartSideIndices[Index]);
				int32 NeighborsNum = Edge->GetTwinsEntityCount();
				// If non manifold Edge => Stop
				if (NeighborsNum != 2)
				{
					return;
				}

				{
					int32 OppositIndex = (Index + 2) % 4;
					SideProperties[Index].IsoType = SideProperties[OppositIndex].IsoType;
					if (SideProperties[Index].IsoType == EIso::UndefinedIso)
					{
						int32 AdjacentIndex = (Index + 1) % 4;
						if (SideProperties[AdjacentIndex].IsoType != EIso::UndefinedIso)
						{
							SideProperties[Index].IsoType = (SideProperties[AdjacentIndex].IsoType == EIso::IsoU) ? EIso::IsoV : EIso::IsoU;
						}
					}
				}

				TSharedPtr<FTopologicalFace> Neighbor;
				{
					for(const TWeakPtr<FTopologicalEdge>& NeighborEdge : Edge->GetTwinsEntities() )
					{
						if (NeighborEdge.HasSameObject(Edge.Get()))
						{
							continue;
						}
						Neighbor = Edge->GetLoop()->GetFace();
					}
				}

				ensure(Neighbor.IsValid());

				// it's not a quad surface
				if (Neighbor->SurfaceCorners.Num() == 0)
				{
					return;
				}

				TSharedPtr<FTopologicalEdge> TwinEdge = Edge->GetFirstTwinEdge();
				int32 SideIndex = Neighbor->GetSideIndex(TwinEdge);
				if (SideIndex < 0)
				{
					return;
				}

				const FEdge2DProperties& Property = Neighbor->GetSideProperty(SideIndex);
				if (Property.IsoType == EIso::UndefinedIso)
				{
					return;
				}

				double SideLength = SideProperties[Index].Length3D;
				double OtherSideLength = Property.Length3D;

				if (FMath::Abs(SideLength - OtherSideLength) < GeometricTolerance)
				{
					int32 OppositIndex = (Index + 2) % 4;
					if (SideProperties[OppositIndex].IsoType == EIso::UndefinedIso)
					{
						if (Index < 2)
						{
							if (SideProperties[!Index].IsoType == EIso::IsoU)
							{
								SideProperties[Index].IsoType = EIso::IsoV;
							}
							else
							{
								SideProperties[Index].IsoType = EIso::IsoU;
							}
						}
						return;
					}
					SideProperties[Index].IsoType = SideProperties[OppositIndex].IsoType;
				}
			}
		}

		if ((SideProperties[0].IsoType != EIso::UndefinedIso) && (SideProperties[1].IsoType != EIso::UndefinedIso) && (SideProperties[0].IsoType == SideProperties[2].IsoType) && (SideProperties[1].IsoType == SideProperties[3].IsoType))
		{
			QuadType = EQuadType::Quadrangular;
		}
		break;

	default:
		QuadType = EQuadType::Other;
		break;
	}
}

