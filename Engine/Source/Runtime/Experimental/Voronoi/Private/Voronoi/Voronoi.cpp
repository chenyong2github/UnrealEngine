// Copyright Epic Games, Inc. All Rights Reserved.
#include "Voronoi/Voronoi.h"


THIRD_PARTY_INCLUDES_START
#include "voro++/voro++.hh"
THIRD_PARTY_INCLUDES_END

namespace {

	// initialize AABB, ignoring NaNs
	FBox SafeInitBounds(const TArrayView<const FVector>& Points)
	{
		FBox BoundingBox;
		BoundingBox.Init();
		for (const FVector &V : Points)
		{
			if (!V.ContainsNaN())
			{
				BoundingBox += V;
			}
		}
		return BoundingBox;
	}

	// Add sites to Voronoi container, with contiguous ids, ignoring NaNs
	void PutSites(voro::container *Container, const TArrayView<const FVector>& Sites, int32 Offset)
	{
		for (int i = 0; i < Sites.Num(); i++)
		{
			const FVector &V = Sites[i];
			if (V.ContainsNaN())
			{
				ensureMsgf(false, TEXT("Cannot construct voronoi neighbor for site w/ NaNs in position vector"));
			}
			else
			{
				Container->put(Offset + i, V.X, V.Y, V.Z);
			}
		}
	}

	// Add sites to Voronoi container, with contiguous ids, ignoring NaNs, ignoring Sites that are on top of existing sites
	int32 PutSitesWithDistanceCheck(voro::container *Container, const TArrayView<const FVector>& Sites, int32 Offset, float SquaredDistThreshold = 1e-4)
	{
		int32 SkippedPts = 0;
		for (int i = 0; i < Sites.Num(); i++)
		{
			const FVector &V = Sites[i];
			if (V.ContainsNaN())
			{
				SkippedPts++;
				ensureMsgf(false, TEXT("Cannot construct voronoi neighbor for site w/ NaNs in position vector"));
			}
			else
			{
				double EX, EY, EZ;
				int ExistingPtID;
				if (Container->find_voronoi_cell(V.X, V.Y, V.Z, EX, EY, EZ, ExistingPtID))
				{
					float dx = V.X - EX;
					float dy = V.Y - EY;
					float dz = V.Z - EZ;
					if (dx*dx + dy * dy + dz * dz < SquaredDistThreshold)
					{
						SkippedPts++;
						continue;
					}
				}
				Container->put(Offset + i, V.X, V.Y, V.Z);
			}
		}
		return SkippedPts;
	}

	TUniquePtr<voro::container> StandardVoroContainerInit(const TArrayView<const FVector>& Sites, FBox& BoundingBox, float BoundingBoxSlack = 0.1f, float SquaredDistSkipPtThreshold = 0.0f)
	{
		BoundingBox = BoundingBox.ExpandBy(BoundingBoxSlack);
		FVector BoundingBoxSize = BoundingBox.GetSize();

		// If points are too far apart, voro++ will ask for unbounded memory to build its grid over space
		// TODO: Figure out reasonable bounds / behavior for this case
		ensure(BoundingBoxSize.GetMax() < HUGE_VALF);


		int NumSites = Sites.Num();

		int GridCellsX, GridCellsY, GridCellsZ;
		voro::guess_optimal(Sites.Num(), BoundingBoxSize.X, BoundingBoxSize.Y, BoundingBoxSize.Z, GridCellsX, GridCellsY, GridCellsZ);

		TUniquePtr<voro::container> Container = MakeUnique<voro::container>(
			BoundingBox.Min.X, BoundingBox.Max.X, BoundingBox.Min.Y,
			BoundingBox.Max.Y, BoundingBox.Min.Z, BoundingBox.Max.Z,
			GridCellsX, GridCellsY, GridCellsZ, false, false, false, 10
		);

		if (SquaredDistSkipPtThreshold > 0)
		{
			PutSitesWithDistanceCheck(Container.Get(), Sites, 0, SquaredDistSkipPtThreshold);
		}
		else
		{
			PutSites(Container.Get(), Sites, 0);
		}

		return Container;
	}
};

FVoronoiDiagram::~FVoronoiDiagram() = default;

FVoronoiDiagram::FVoronoiDiagram(const TArrayView<const FVector>& Sites, float ExtraBoundingSpace, float SquaredDistSkipPtThreshold) : NumSites(0), Container(nullptr)
{
	FBox BoundingBox = SafeInitBounds(Sites);
	Container = StandardVoroContainerInit(Sites, BoundingBox, DefaultBoundingBoxSlack, SquaredDistSkipPtThreshold);
	this->Bounds = BoundingBox;
	NumSites = Sites.Num();
}

FVoronoiDiagram::FVoronoiDiagram(const TArrayView<const FVector>& Sites, const FBox &Bounds, float ExtraBoundingSpace, float SquaredDistSkipPtThreshold) : NumSites(0), Container(nullptr)
{
	FBox BoundingBox(Bounds);
	Container = StandardVoroContainerInit(Sites, BoundingBox, DefaultBoundingBoxSlack, SquaredDistSkipPtThreshold);
	this->Bounds = BoundingBox;
	NumSites = Sites.Num();
}



bool VoronoiNeighbors(const TArrayView<const FVector> &Sites, TArray<TArray<int>> &Neighbors, bool bExcludeBounds, float SquaredDistSkipPtThreshold) 
{
	FBox BoundingBox = SafeInitBounds(Sites);
	auto Container = StandardVoroContainerInit(Sites, BoundingBox, FVoronoiDiagram::DefaultBoundingBoxSlack, SquaredDistSkipPtThreshold);
	int32 NumSites = Sites.Num();

	Neighbors.Empty(NumSites);
	Neighbors.SetNum(NumSites, true);

	voro::c_loop_all CellIterator(*Container);
	voro::voronoicell_neighbor cell;
	if (CellIterator.start())
	{
		do
		{
			bool bCouldComputeCell = Container->compute_cell(cell, CellIterator);
			ensureMsgf(bCouldComputeCell, TEXT("Failed to compute a Voronoi cell -- this may indicate sites positioned directly on top of other sites, which is not valid for a Voronoi diagram"));
			if (bCouldComputeCell)
			{
				int id = CellIterator.pid();

				cell.neighborsTArray(Neighbors[id], bExcludeBounds);
			}
		} while (CellIterator.inc());
	}

	return true;
}

bool GetVoronoiEdges(const TArrayView<const FVector> &Sites, const FBox& Bounds, TArray<TTuple<FVector, FVector>> &Edges, TArray<int32> &CellMember, float SquaredDistSkipPtThreshold)
{
	int32 NumSites = Sites.Num();
	FBox BoundingBox(Bounds);
	auto Container = StandardVoroContainerInit(Sites, BoundingBox, FVoronoiDiagram::DefaultBoundingBoxSlack, SquaredDistSkipPtThreshold);

	voro::container &con = *Container;
	voro::c_loop_all CellIterator(con);
	voro::voronoicell cell;

	int32 id = 0;

	std::vector<double> Vertices;
	std::vector<int> FaceVertices;

	if (CellIterator.start())
	{
		do
		{
			bool bCouldComputeCell = Container->compute_cell(cell, CellIterator);
			ensureMsgf(bCouldComputeCell, TEXT("Failed to compute a Voronoi cell -- this may indicate sites positioned directly on top of other sites, which is not valid for a Voronoi diagram"));
			if (bCouldComputeCell)
			{
				const double *pp = con.p[CellIterator.ijk] + con.ps * CellIterator.q;
				const FVector Center(*pp, pp[1], pp[2]);
				
				cell.vertices(Center.X, Center.Y, Center.Z, Vertices);
				cell.face_vertices(FaceVertices);

				int32 FaceOffset = 0;
				for (size_t ii = 0, ni = FaceVertices.size(); ii < ni; ii += FaceVertices[ii] + 1)
				{
					int32 VertCount = FaceVertices[ii];
					int32 PreviousVertexIndex = FaceVertices[FaceOffset + VertCount] * 3;
					for (int32 kk = 0; kk < VertCount; ++kk)
					{
						CellMember.Emplace(id);
						int32 VertexIndex = FaceVertices[1 + FaceOffset + kk] * 3; // Index of vertex X coordinate in raw coordinates array

						Edges.Emplace(
							FVector(Vertices[PreviousVertexIndex], Vertices[PreviousVertexIndex + 1], Vertices[PreviousVertexIndex + 2]),
							FVector(Vertices[VertexIndex], Vertices[VertexIndex + 1], Vertices[VertexIndex + 2])
						);
						PreviousVertexIndex = VertexIndex;
					}
					FaceOffset += VertCount + 1;
				}

				++id;
			}
		} while (CellIterator.inc());
	}
	return true;
}




// TODO: maybe make this a "SetSites" instead of an Add?
void FVoronoiDiagram::AddSites(const TArrayView<const FVector>& AddSites, float SquaredDistSkipPtThreshold)
{
	int32 OrigSitesNum = NumSites;
	if (SquaredDistSkipPtThreshold > 0)
	{
		PutSitesWithDistanceCheck(Container.Get(), AddSites, OrigSitesNum, SquaredDistSkipPtThreshold);
	}
	else
	{
		PutSites(Container.Get(), AddSites, OrigSitesNum);
	}
	NumSites += AddSites.Num();
}

void FVoronoiDiagram::ComputeAllCells(TArray<FVoronoiCellInfo> &AllCells)
{
	check(Container);

	AllCells.SetNum(NumSites);

	voro::c_loop_all CellIterator(*Container);
	voro::voronoicell_neighbor cell;

	// TODO: multithread?
	if (CellIterator.start())
	{
		do
		{
			bool bCouldComputeCell = Container->compute_cell(cell, CellIterator);
			ensureMsgf(bCouldComputeCell, TEXT("Failed to compute a Voronoi cell -- this may indicate sites positioned directly on top of other sites, which is not valid for a Voronoi diagram"));
			if (bCouldComputeCell)
			{
				int32 id = CellIterator.pid();
				double x, y, z;
				CellIterator.pos(x, y, z);

				FVoronoiCellInfo& Cell = AllCells[id];
				cell.extractCellInfo(FVector(x,y,z), Cell.Vertices, Cell.Faces, Cell.Neighbors, Cell.Normals);
			}
		} while (CellIterator.inc());
	}
}

int32 FVoronoiDiagram::FindCell(const FVector& Pos)
{
	check(Container);
	double rx, ry, rz; // holds position of the found Voronoi site (if any); currently unused
	int pid;
	bool found = Container->find_voronoi_cell(Pos.X, Pos.Y, Pos.Z, rx, ry, rz, pid);
	return found ? pid : -1;
}

//void FVoronoiDiagram::GetCells(const TArrayView<const FVector> &Sites, TArray<FVoronoiCellInfo> &AllCells)
//{
//	FVoronoiDiagram Voronoi(Sites, FVoronoiDiagram::DefaultBoundingBoxSlack);
//	Voronoi.ComputeAllCells(AllCells);
//}

const float FVoronoiDiagram::DefaultBoundingBoxSlack = .1f;