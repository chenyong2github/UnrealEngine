// Copyright Epic Games, Inc. All Rights Reserved.
#include "PlanarCut.h"
#include "PlanarCutPlugin.h"

#include "Async/ParallelFor.h"
#include "Spatial/FastWinding.h"
#include "Spatial/PointHashGrid3.h"
#include "Arrangement2d.h"
#include "MeshAdapter.h"
#include "FrameTypes.h"
#include "Polygon2.h"

#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"


#include "DynamicMesh3.h"
#include "DynamicMeshEditor.h"
#include "DynamicMeshAABBTree3.h"
#include "Operations/MeshBoolean.h"
#include "Operations/MeshSelfUnion.h"
#include "Operations/MergeCoincidentMeshEdges.h"
#include "MeshBoundaryLoops.h"
#include "QueueRemesher.h"
#include "DynamicVertexAttribute.h"
#include "MeshNormals.h"
#include "ConstrainedDelaunay2.h"

#include "Algo/Rotate.h"

#if WITH_EDITOR
#include "Misc/ScopedSlowTask.h"
#endif

// Console variable to toggle between new and old fracture method; may delete this when we delete the old method
static TAutoConsoleVariable<int32> CVarFractureMethod(
	TEXT("Fracture.MeshCuttingMethod"),
	1,
	TEXT("Defines the which method is used under the hood to compute fracture geometry.\n")
	TEXT("<=0: original, with fake noise\n")
	TEXT("  1: experimental new method\n"),
	ECVF_Scalability);


/**
 * Adapter that lets the generic fast winding and AABB tree code view a geometry from a geometrycollection as a simple index buffer triangle mesh
 */
struct FGeometryCollectionMeshAdapter
{
	const FGeometryCollection *Collection;
	const int32 GeometryIdx;


	constexpr inline bool IsTriangle(int32 index) const
	{
		return true;
	}
	constexpr inline bool IsVertex(int32 index) const
	{
		return true;
	}
	inline int32 MaxTriangleID() const
	{
		return Collection->FaceCount[GeometryIdx];
	}
	inline int32 MaxVertexID() const
	{
		return Collection->VertexCount[GeometryIdx];
	}
	inline int32 TriangleCount() const
	{
		return Collection->FaceCount[GeometryIdx];
	}
	inline int32 VertexCount() const
	{
		return Collection->VertexCount[GeometryIdx];
	}
	constexpr inline int32 GetShapeTimestamp() const
	{
		return 0;
	}
	inline FIndex3i GetTriangle(int32 Idx) const
	{
		int32 VertexStart = Collection->VertexStart[GeometryIdx];
		FIndex3i Tri(Collection->Indices[Idx + Collection->FaceStart[GeometryIdx]]);
		Tri.A -= VertexStart;
		Tri.B -= VertexStart;
		Tri.C -= VertexStart;
		return Tri;
	}
	inline FVector3d GetVertex(int32 Idx) const
	{
		return FVector3d(Collection->Vertex[Idx + Collection->VertexStart[GeometryIdx]]);
	}

	inline void GetTriVertices(int TID, FVector3d& V0, FVector3d& V1, FVector3d& V2) const
	{
		FIntVector TriRaw = Collection->Indices[TID + Collection->FaceStart[GeometryIdx]];

		V0 = Collection->Vertex[TriRaw.X];
		V1 = Collection->Vertex[TriRaw.Y];
		V2 = Collection->Vertex[TriRaw.Z];
	}
};

// FDynamicMesh3 + additional attributes as needed to carry standard geometry collection attributes
struct FAugmentedDynamicMesh
{
	FDynamicMesh3 Mesh;
	const FName TangentUAttribName = "TangentUAttrib";
	const FName TangentVAttribName = "TangentVAttrib";
	const FName VisibleAttribName = "VisibleAttrib";

	FAugmentedDynamicMesh()
	{
		Mesh.EnableAttributes();
		Mesh.Attributes()->EnableMaterialID();
		Mesh.Attributes()->AttachAttribute(TangentUAttribName, new TDynamicMeshVertexAttribute<float, 3>(&Mesh));
		Mesh.Attributes()->AttachAttribute(TangentVAttribName, new TDynamicMeshVertexAttribute<float, 1>(&Mesh)); // just a sign
		TDynamicMeshScalarTriangleAttribute<bool>* VisAttrib = new TDynamicMeshScalarTriangleAttribute<bool>(&Mesh);
		VisAttrib->Initialize(true);
		Mesh.Attributes()->AttachAttribute(VisibleAttribName, VisAttrib);
	}

	// re-setup attributes to augment the mesh; only call this if we've lost the attached attributes
	void Augment()
	{
		Mesh.EnableAttributes();
		Mesh.Attributes()->EnableMaterialID();
		ensure(Mesh.Attributes()->NumAttachedAttributes() == 0);
		Mesh.Attributes()->AttachAttribute(TangentUAttribName, new TDynamicMeshVertexAttribute<float, 3>(&Mesh));
		Mesh.Attributes()->AttachAttribute(TangentVAttribName, new TDynamicMeshVertexAttribute<float, 1>(&Mesh)); // just a sign
		TDynamicMeshScalarTriangleAttribute<bool>* VisAttrib = new TDynamicMeshScalarTriangleAttribute<bool>(&Mesh);
		VisAttrib->Initialize(true);
		Mesh.Attributes()->AttachAttribute(VisibleAttribName, VisAttrib);
	}

	void SetDefaultMaterials(int32 MaterialID)
	{
		for (int TID : Mesh.TriangleIndicesItr())
		{
			Mesh.Attributes()->GetMaterialID()->SetNewValue(TID, MaterialID);
		}
	}

	void SetDefaultAttributes(const FInternalSurfaceMaterials& Materials)
	{
		TDynamicMeshVertexAttribute<float, 3>* Us =
			static_cast<TDynamicMeshVertexAttribute<float, 3>*>(Mesh.Attributes()->GetAttachedAttribute(TangentUAttribName));
		TDynamicMeshVertexAttribute<float, 1>* VSigns =
			static_cast<TDynamicMeshVertexAttribute<float, 1>*>(Mesh.Attributes()->GetAttachedAttribute(TangentVAttribName));

		for (int VID : Mesh.VertexIndicesItr())
		{
			FVector3f N = Mesh.GetVertexNormal(VID);
			FVector3f U;
			VectorUtil::MakePerpVector(N, U);
			Us->SetValue(VID, U);
			float VSign = 1;
			VSigns->SetValue(VID, &VSign);
		}

		TDynamicMeshScalarTriangleAttribute<bool>* Visible =
			static_cast<TDynamicMeshScalarTriangleAttribute<bool>*>(Mesh.Attributes()->GetAttachedAttribute(VisibleAttribName));
		for (int TID : Mesh.TriangleIndicesItr())
		{
			Visible->SetNewValue(TID, Materials.bGlobalVisibility);
		}
	}

	void SetVisibility(int TID, bool bIsVisible)
	{
		TDynamicMeshScalarTriangleAttribute<bool>* Visible =
			static_cast<TDynamicMeshScalarTriangleAttribute<bool>*>(Mesh.Attributes()->GetAttachedAttribute(VisibleAttribName));
		Visible->SetValue(TID, bIsVisible);
	}

	bool GetVisibility(int TID)
	{
		TDynamicMeshScalarTriangleAttribute<bool>* Visible =
			static_cast<TDynamicMeshScalarTriangleAttribute<bool>*>(Mesh.Attributes()->GetAttachedAttribute(VisibleAttribName));
		return Visible->GetValue(TID);
	}

	void SetTangent(int VID, FVector3f Normal, FVector3f TangentU, FVector3f TangentV)
	{
		TDynamicMeshVertexAttribute<float, 3>* Us =
			static_cast<TDynamicMeshVertexAttribute<float, 3>*>(Mesh.Attributes()->GetAttachedAttribute(TangentUAttribName));
		TDynamicMeshVertexAttribute<float, 1>* VSigns =
			static_cast<TDynamicMeshVertexAttribute<float, 1>*>(Mesh.Attributes()->GetAttachedAttribute(TangentVAttribName));
		float VSign = FMathf::SignNonZero(Normal.Cross(TangentU).Dot(TangentV));
		Us->SetValue(VID, TangentU);
		VSigns->SetValue(VID, &VSign);
	}
	void FixTangents()
	{
		TDynamicMeshVertexAttribute<float, 3>* Us =
			static_cast<TDynamicMeshVertexAttribute<float, 3>*>(Mesh.Attributes()->GetAttachedAttribute(TangentUAttribName));
		TDynamicMeshVertexAttribute<float, 1>* VSigns =
			static_cast<TDynamicMeshVertexAttribute<float, 1>*>(Mesh.Attributes()->GetAttachedAttribute(TangentVAttribName));

		for (int VID : Mesh.VertexIndicesItr())
		{
			FVector3f N = Mesh.GetVertexNormal(VID);
			FVector3f U;
			Us->GetValue(VID, U);
			U = N.Cross(U).Cross(N);
			U.Normalize();
			Us->SetValue(VID, U);
			float VSign;
			VSigns->GetValue(VID, &VSign);
			VSign = FMathf::SignNonZero(VSign);
			VSigns->SetValue(VID, &VSign);
		}
	}
	void GetTangent(int VID, FVector3f& U, FVector3f& V)
	{
		TDynamicMeshVertexAttribute<float, 3>* Us =
			static_cast<TDynamicMeshVertexAttribute<float, 3>*>(Mesh.Attributes()->GetAttachedAttribute(TangentUAttribName));
		TDynamicMeshVertexAttribute<float, 1>* VSigns =
			static_cast<TDynamicMeshVertexAttribute<float, 1>*>(Mesh.Attributes()->GetAttachedAttribute(TangentVAttribName));
		FVector3f Normal = Mesh.GetVertexNormal(VID);
		Us->GetValue(VID, U);
		float VSign;
		VSigns->GetValue(VID, &VSign);
		V = Normal.Cross(U) * FMathf::SignNonZero(VSign);
	}
};

struct FCellMeshes
{
	struct FCellInfo
	{
		FAugmentedDynamicMesh AugMesh;

		// TODO: compute spatial in advance?  (only useful if we rework mesh booleans to support it)
		//FDynamicMeshAABBTree3 Spatial;
	};
	TArray<FCellInfo> CellMeshes;
	int32 OutsideCellIndex = -1;

	// Noise Offsets, to randomize where perlin noise is sampled
	FVector NoiseOffsetX;
	FVector NoiseOffsetY;
	FVector NoiseOffsetZ;

	FCellMeshes() {}
	FCellMeshes(const FPlanarCells& Cells, FAxisAlignedBox3d DomainBounds, double ExtendDomain, bool bIncludeOutsideCell = true)
	{
		Init(Cells, DomainBounds, ExtendDomain, bIncludeOutsideCell);
	}

	void RemeshForNoise(FDynamicMesh3& Mesh, EEdgeRefineFlags EdgeFlags, double TargetEdgeLen)
	{
		FQueueRemesher Remesh(&Mesh);
		FMeshConstraints Constraints;

		FMeshBoundaryLoops Boundary(&Mesh);
		checkSlow(Boundary.GetLoopCount() == 1);

		for (int VID : Mesh.VertexIndicesItr())
		{
			FVertexConstraint FullyConstrain(true, false, VID);
			Constraints.SetOrUpdateVertexConstraint(VID, FullyConstrain);
		}


		FEdgeConstraint EdgeConstraint(EdgeFlags);
		for (int EID : Boundary[0].Edges)
		{
			Constraints.SetOrUpdateEdgeConstraint(EID, EdgeConstraint);
		}
		Remesh.SetExternalConstraints(Constraints);
		Remesh.SetTargetEdgeLength(TargetEdgeLen);
		Remesh.Precompute();
		Remesh.FastestRemesh();
	}

	float OctaveNoise(const FVector& V, const FNoiseSettings& Settings)
	{
		int32 Octaves = Settings.Octaves;
		float NoiseValue = 0;
		float OctaveScale = 1;
		for (int32 Octave = 0; Octave < Octaves; Octave++, OctaveScale *= 2)
		{
			NoiseValue += FMath::PerlinNoise3D(V * OctaveScale) / OctaveScale;
		}
		return NoiseValue;
	}

	FVector NoiseVector(const FVector& Pos, const FNoiseSettings& Settings)
	{
		float Frequency = Settings.Frequency;
		FVector Base = Pos * Frequency;
		return FVector(
			OctaveNoise(Base + NoiseOffsetX, Settings),
			OctaveNoise(Base + NoiseOffsetY, Settings),
			OctaveNoise(Base + NoiseOffsetZ, Settings)
		) * Settings.Amplitude;
	}

	FVector3d NoiseDisplacement(const FVector3d& Pos, const FNoiseSettings& Settings)
	{
		FVector P = FVector(Pos);
		return FVector3d(NoiseVector(P, Settings));
	}

	void ApplyNoise(FDynamicMesh3& Mesh, FVector3d Normal, const FNoiseSettings& Settings, bool bProjectBoundariesToNormal = false)
	{
		float Amplitude = Settings.Amplitude;
		float Frequency = Settings.Frequency;
		int32 Octaves = Settings.Octaves;
		FVector3d Z = Normal * Amplitude;

		for (int VID : Mesh.VertexIndicesItr())
		{
			FVector3d Pos = Mesh.GetVertex(VID);
			FVector3d Displacement = NoiseDisplacement(Pos, Settings);
			if (bProjectBoundariesToNormal || !Mesh.IsBoundaryVertex(VID))
			{
				// project displacement onto the normal direction
				Displacement = Normal * Displacement.Dot(Normal);
			}

			
			Mesh.SetVertex(VID, Pos + Displacement);
		}
	}

	/**
	 * Convert plane index to triangle group
	 * @return group id encoding the source plane into a triangle mesh
	 */
	int PlaneToGroup(int Plane)
	{
		return Plane + 1;
	}

	/**
	 * Convert group index to plane index
	 * @return index of source plane for triangle, or -1 if no such plane
	 */
	int GroupToPlane(int Group)
	{
		return Group - 1;
	}

	void Init(const FPlanarCells& Cells, FAxisAlignedBox3d DomainBounds, double ExtendDomain, bool bIncludeOutsideCell = true)
	{
		NoiseOffsetX = FMath::VRand() * 100;
		NoiseOffsetY = FMath::VRand() * 100;
		NoiseOffsetZ = FMath::VRand() * 100;

		float GlobalUVScale = Cells.InternalSurfaceMaterials.GlobalUVScale;
		if (!ensure(GlobalUVScale > 0))
		{
			GlobalUVScale = 1;
		}

		int NumCells = Cells.NumCells;
		bool bHasOutsideCell = false;
		OutsideCellIndex = -1;
		if (bIncludeOutsideCell && !Cells.IsInfinitePlane())
		{
			for (const TPair<int32, int32>& CellPair : Cells.PlaneCells)
			{
				if (CellPair.Value == -1)
				{
					bHasOutsideCell = true;
					break;
				}
			}
		}
		if (bHasOutsideCell)
		{
			OutsideCellIndex = NumCells;
			NumCells++;
		}

		CellMeshes.Reset();
		CellMeshes.SetNum(NumCells);

		DomainBounds.Expand(ExtendDomain);

		bool bNoise = Cells.InternalSurfaceMaterials.NoiseSettings.IsSet();
		if (bNoise)
		{
			ExtendDomain += Cells.InternalSurfaceMaterials.NoiseSettings->Amplitude;
		}

		// special handling for the infinite plane case; we need to adapt this to be a closed volume
		if (Cells.IsInfinitePlane())
		{
			CreateMeshesForSinglePlane(Cells, DomainBounds, bNoise, GlobalUVScale);
		}
		else if (!bNoise) // bounded cells w/ no noise
		{
			CreateMeshesForBoundedPlanesWithoutNoise(NumCells, Cells, DomainBounds, bNoise, GlobalUVScale);
		}
		else // bounded cells with noise -- make each boundary plane separately so we can remesh them w/ noise vertices
		{
			CreateMeshesForBoundedPlanesWithNoise(NumCells, Cells, DomainBounds, bNoise, GlobalUVScale);
		}
		
		// TODO: self-union on cells when it makes sense to do so (for non-single-plane inputs w/ high noise or possible untracked adjacencies)
		/*for (FCellInfo& CellInfo : CellMeshes)
		{
			FMeshSelfUnion SelfUnion(&CellInfo.AugMesh.Mesh);
			// TODO: need to have an option in SelfUnion to not weld edges
			SelfUnion.Compute();
		}*/

		for (FCellInfo& CellInfo : CellMeshes)
		{
			check(CellInfo.AugMesh.Mesh.Attributes() != nullptr);
			check(CellInfo.AugMesh.Mesh.Attributes()->NumAttachedAttributes() == 3);
			CellInfo.AugMesh.SetDefaultAttributes(Cells.InternalSurfaceMaterials);
			CellInfo.AugMesh.SetDefaultMaterials(Cells.InternalSurfaceMaterials.GlobalMaterialID);
		}
	}

	void AppendMesh(FDynamicMesh3& Base, FDynamicMesh3& ToAppend, bool bFlipped)
	{
		FDynamicMeshEditor Editor(&Base);
		FMeshIndexMappings Mapping;
		Editor.AppendMesh(&ToAppend, Mapping);
		if (bFlipped)
		{
			for (int TID : ToAppend.TriangleIndicesItr())
			{
				Base.ReverseTriOrientation(Mapping.GetNewTriangle(TID));
			}
			for (int VID : ToAppend.VertexIndicesItr())
			{
				int BaseVID = Mapping.GetNewVertex(VID);
				Base.SetVertexNormal(BaseVID, -Base.GetVertexNormal(BaseVID));
			}
		}
	}
private:
	void CreateMeshesForBoundedPlanesWithoutNoise(int NumCells, const FPlanarCells& Cells, const FAxisAlignedBox3d& DomainBounds, bool bNoise, double GlobalUVScale)
	{
		for (int32 PlaneIdx = 0; PlaneIdx < Cells.PlaneCells.Num(); PlaneIdx++)
		{
			const TPair<int32, int32>& CellPair = Cells.PlaneCells[PlaneIdx];
			FDynamicMesh3* Meshes[2]{ &CellMeshes[CellPair.Key].AugMesh.Mesh, nullptr };
			int32 OtherCell = CellPair.Value < 0 ? OutsideCellIndex : CellPair.Value;
			int NumMeshes = OtherCell < 0 ? 1 : 2;
			if (NumMeshes == 2)
			{
				Meshes[1] = &CellMeshes[OtherCell].AugMesh.Mesh;
			}

			const TArray<int>& PlaneBoundary = Cells.PlaneBoundaries[PlaneIdx];
			FVector3f Normal(Cells.Planes[PlaneIdx].GetNormal());
			FFrame3d PlaneFrame(Cells.Planes[PlaneIdx]);
			FVertexInfo PlaneVertInfo;
			PlaneVertInfo.bHaveC = true;
			PlaneVertInfo.bHaveUV = true;
			PlaneVertInfo.bHaveN = true;
			PlaneVertInfo.Color = FVector3f(1, 1, 1);
			int VertStart[2]{ -1, -1 };
			for (int MeshIdx = 0; MeshIdx < NumMeshes; MeshIdx++)
			{
				PlaneVertInfo.Normal = Normal;
				if (MeshIdx == 1 && OtherCell != OutsideCellIndex)
				{
					PlaneVertInfo.Normal *= -1;
				}
				VertStart[MeshIdx] = Meshes[MeshIdx]->MaxVertexID();
				FVector2f MinUV(FMathf::MaxReal, FMathf::MaxReal);
				for (int BoundaryVertex : PlaneBoundary)
				{
					FVector3d Position = FVector3d(Cells.PlaneBoundaryVertices[BoundaryVertex]);
					FVector2f UV = FVector2f(PlaneFrame.ToPlaneUV(Position));
					MinUV.X = FMathf::Min(PlaneVertInfo.UV.X, MinUV.X);
					MinUV.Y = FMathf::Min(PlaneVertInfo.UV.Y, MinUV.Y);
				}
				for (int BoundaryVertex : PlaneBoundary)
				{
					PlaneVertInfo.Position = FVector3d(Cells.PlaneBoundaryVertices[BoundaryVertex]);
					PlaneVertInfo.UV = (FVector2f(PlaneFrame.ToPlaneUV(PlaneVertInfo.Position)) - MinUV) * GlobalUVScale;
					Meshes[MeshIdx]->AppendVertex(PlaneVertInfo);
				}
			}

			for (int V0 = 0, V1 = 1, V2 = 2; V2 < PlaneBoundary.Num(); V1 = V2++)
			{
				for (int MeshIdx = 0; MeshIdx < NumMeshes; MeshIdx++)
				{
					int Offset = VertStart[MeshIdx];
					FIndex3i Tri(V0 + Offset, V1 + Offset, V2 + Offset);
					if (MeshIdx == 1 && OtherCell != OutsideCellIndex)
					{
						Swap(Tri.B, Tri.C);
					}
					Meshes[MeshIdx]->AppendTriangle(Tri, PlaneToGroup(PlaneIdx));
				}
			}
		}
	}
	void CreateMeshesForBoundedPlanesWithNoise(int NumCells, const FPlanarCells& Cells, const FAxisAlignedBox3d& DomainBounds, bool bNoise, double GlobalUVScale)
	{
		TArray<FDynamicMesh3> PlaneMeshes;
		PlaneMeshes.SetNum(Cells.Planes.Num());

		struct FPlaneIdxAndFlip
		{
			int32 PlaneIdx;
			bool bIsFlipped;
		};
		TArray<TArray<FPlaneIdxAndFlip>> CellPlanes; // per cell, the planes that border that cell
		CellPlanes.SetNum(NumCells);

		for (int32 PlaneIdx = 0; PlaneIdx < Cells.PlaneCells.Num(); PlaneIdx++)
		{
			const TPair<int32, int32>& CellPair = Cells.PlaneCells[PlaneIdx];
			int32 OtherCell = CellPair.Value < 0 ? OutsideCellIndex : CellPair.Value;
			if (ensure(CellPlanes.IsValidIndex(CellPair.Key)))
			{
				CellPlanes[CellPair.Key].Add({ PlaneIdx, false });
			}
			if (CellPlanes.IsValidIndex(OtherCell))
			{
				CellPlanes[OtherCell].Add({ PlaneIdx, true });
			}
		}

		// heuristic to protect against creating too many vertices on remeshing
		float MaxArea = 0;
		for (int32 PlaneIdx = 0; PlaneIdx < Cells.Planes.Num(); PlaneIdx++)
		{
			const TArray<int>& PlaneBoundary = Cells.PlaneBoundaries[PlaneIdx];
			const FVector& V0 = Cells.PlaneBoundaryVertices[PlaneBoundary[0]];
			FVector AreaVec = FVector::ZeroVector;
			for (int32 V1Idx = 1, V2Idx = 2; V2Idx < PlaneBoundary.Num(); V1Idx = V2Idx++)
			{
				const FVector& V1 = Cells.PlaneBoundaryVertices[PlaneBoundary[V1Idx]];
				const FVector& V2 = Cells.PlaneBoundaryVertices[PlaneBoundary[V2Idx]];
				AreaVec += (V1 - V0) ^ (V2 - V1);
			}
			MaxArea = AreaVec.Size();
		}
		double MaxVerts = 10000;
		double MinEdgeLen = FMathd::Sqrt((double)MaxArea / MaxVerts);
		double Spacing = FMath::Max3(.001, MinEdgeLen, (double)Cells.InternalSurfaceMaterials.NoiseSettings->PointSpacing);

		ParallelFor(Cells.Planes.Num(), [this, &PlaneMeshes, &Cells, GlobalUVScale, Spacing](int32 PlaneIdx)
			{
				FDynamicMesh3& Mesh = PlaneMeshes[PlaneIdx];
				const TArray<int>& PlaneBoundary = Cells.PlaneBoundaries[PlaneIdx];
				FVector3f Normal(Cells.Planes[PlaneIdx].GetNormal());
				FFrame3d PlaneFrame(Cells.Planes[PlaneIdx]);
				FVertexInfo PlaneVertInfo;
				PlaneVertInfo.bHaveC = true;
				PlaneVertInfo.bHaveUV = true;
				PlaneVertInfo.bHaveN = true;
				PlaneVertInfo.Normal = Normal;
				PlaneVertInfo.Color = FVector3f(1, 1, 1);

				FVector2f MinUV(FMathf::MaxReal, FMathf::MaxReal);
				for (int BoundaryVertex : PlaneBoundary)
				{
					FVector3d Position = FVector3d(Cells.PlaneBoundaryVertices[BoundaryVertex]);
					FVector2f UV = FVector2f(PlaneFrame.ToPlaneUV(Position));
					MinUV.X = FMathf::Min(PlaneVertInfo.UV.X, MinUV.X);
					MinUV.Y = FMathf::Min(PlaneVertInfo.UV.Y, MinUV.Y);
				}

				FPolygon2f Polygon;
				for (int BoundaryVertex : PlaneBoundary)
				{
					PlaneVertInfo.Position = FVector3d(Cells.PlaneBoundaryVertices[BoundaryVertex]);
					PlaneVertInfo.UV = (FVector2f(PlaneFrame.ToPlaneUV(PlaneVertInfo.Position)) - MinUV) * GlobalUVScale;
					Polygon.AppendVertex(PlaneVertInfo.UV);
					Mesh.AppendVertex(PlaneVertInfo);
				}

				// we do a CDT here to give a slightly better start to remeshing; we could try simple ear clipping instead
				FGeneralPolygon2f GeneralPolygon(Polygon);
				FConstrainedDelaunay2f Triangulation;
				Triangulation.FillRule = FConstrainedDelaunay2f::EFillRule::NonZero;
				Triangulation.Add(GeneralPolygon);
				Triangulation.Triangulate();

				int GID = PlaneToGroup(PlaneIdx);
				for (FIndex3i Triangle : Triangulation.Triangles)
				{
					Mesh.AppendTriangle(Triangle, GID);
				}

				RemeshForNoise(Mesh, EEdgeRefineFlags::SplitsOnly, Spacing);
				ApplyNoise(Mesh, FVector3d(Normal), Cells.InternalSurfaceMaterials.NoiseSettings.GetValue());
				FMeshNormals::QuickComputeVertexNormals(Mesh);
			}, EParallelForFlags::None);

		for (int CellIdx = 0; CellIdx < NumCells; CellIdx++)
		{
			FCellInfo& CellInfo = CellMeshes[CellIdx];
			FDynamicMesh3& Mesh = CellInfo.AugMesh.Mesh;
			bool bFlipForOutsideCell = CellIdx == OutsideCellIndex; // outside cell will be subtracted, and needs all planes flipped vs normal
			for (FPlaneIdxAndFlip PlaneInfo : CellPlanes[CellIdx])
			{
				AppendMesh(Mesh, PlaneMeshes[PlaneInfo.PlaneIdx], PlaneInfo.bIsFlipped ^ bFlipForOutsideCell);
			}
		}
	}
	void CreateMeshesForSinglePlane(const FPlanarCells& Cells, const FAxisAlignedBox3d& DomainBounds, bool bNoise, double GlobalUVScale)
	{
		int GID = PlaneToGroup(0);
		FPlane Plane = Cells.Planes[0];

		FFrame3d PlaneFrame(Plane);
		FInterval1d ZRange;
		FAxisAlignedBox2d XYRange;
		for (int CornerIdx = 0; CornerIdx < 8; CornerIdx++)
		{
			FVector3d Corner = DomainBounds.GetCorner(CornerIdx);
			XYRange.Contain(PlaneFrame.ToPlaneUV(Corner));
			ZRange.Contain(Plane.PlaneDot(FVector(Corner)));
		}
		//if (FMathd::SignNonZero(ZRange.Min) == FMathd::SignNonZero(ZRange.Max))
		//{
		//	// TODO: early out for plane that doesn't even intersect the domain bounding box?
		//}

		FDynamicMesh3 PlaneMesh;
		FVertexInfo PlaneVertInfo;
		PlaneVertInfo.bHaveC = true;
		PlaneVertInfo.bHaveUV = true;
		PlaneVertInfo.bHaveN = true;
		PlaneVertInfo.Color = FVector3f(1, 1, 1);
		PlaneVertInfo.Normal = FVector3f(Plane.GetNormal());

		for (int CornerIdx = 0; CornerIdx < 4; CornerIdx++)
		{
			PlaneVertInfo.Position = PlaneFrame.FromPlaneUV(XYRange.GetCorner(CornerIdx));
			PlaneVertInfo.UV = FVector2f(XYRange.GetCorner(CornerIdx) - XYRange.Min) * GlobalUVScale;
			PlaneMesh.AppendVertex(PlaneVertInfo);
		}
		PlaneMesh.AppendTriangle(0, 1, 2, GID);
		PlaneMesh.AppendTriangle(0, 2, 3, GID);

		if (bNoise)
		{
			RemeshForNoise(PlaneMesh, EEdgeRefineFlags::SplitsOnly, Cells.InternalSurfaceMaterials.NoiseSettings->PointSpacing);
			ApplyNoise(PlaneMesh, PlaneFrame.GetAxis(2), Cells.InternalSurfaceMaterials.NoiseSettings.GetValue(), true);
			FMeshNormals::QuickComputeVertexNormals(PlaneMesh);
		}
		FDynamicMesh3* Meshes[2]{ &CellMeshes[0].AugMesh.Mesh, &CellMeshes[1].AugMesh.Mesh };
		for (int Side = 0; Side < 2; Side++)
		{
			*Meshes[Side] = PlaneMesh;
			double Offset = ZRange.Max;
			FMeshBoundaryLoops Boundary(Meshes[Side]);
			checkSlow(Boundary.GetLoopCount() == 1);
			int FirstIdx;
			bool bFound = Boundary[0].Vertices.Find(0, FirstIdx);
			checkSlow(bFound);
			TArray<int> VertIDs[2], MatchedIndices[2];
			VertIDs[0] = Boundary[0].Vertices;
			if (FirstIdx != 0)
			{
				Algo::Rotate(VertIDs[0], FirstIdx);
			}
			checkSlow(VertIDs[0][0] == 0);
			MatchedIndices[0].Add(0);
			int FoundIndices = 1;
			for (int VIDIdx = 0; VIDIdx < VertIDs[0].Num(); VIDIdx++)
			{
				int VID = VertIDs[0][VIDIdx];
				if (VID == FoundIndices)
				{
					FoundIndices++;
					MatchedIndices[0].Add(VIDIdx);
				}
			}

			if (Side == 0)
			{
				Meshes[Side]->ReverseOrientation(true);
				Offset = ZRange.Min;
			}
			PlaneVertInfo.Normal = FVector3f(Plane.GetNormal()) * (-1 + Side * 2);
			FVector3d OffsetVec = FVector3d(Plane.GetNormal()) * Offset;

			for (int CornerIdx = 0; CornerIdx < 4; CornerIdx++)
			{
				PlaneVertInfo.Position = Meshes[Side]->GetVertex(CornerIdx) + OffsetVec;
				// UVs shouldn't matter for outer box vertices because they're outside of the domain by construction ...
				VertIDs[1].Add(Meshes[Side]->AppendVertex(PlaneVertInfo));
				MatchedIndices[1].Add(CornerIdx);
			}
			int NewTris[2]{
				Meshes[Side]->AppendTriangle(VertIDs[1][0], VertIDs[1][1], VertIDs[1][2], GID),
				Meshes[Side]->AppendTriangle(VertIDs[1][0], VertIDs[1][2], VertIDs[1][3], GID)
			};
			if (Side == 1)
			{
				Meshes[Side]->ReverseTriOrientation(NewTris[0]);
				Meshes[Side]->ReverseTriOrientation(NewTris[1]);
			}
			FDynamicMeshEditor Editor(Meshes[Side]);
			FDynamicMeshEditResult ResultOut;
			Editor.StitchSparselyCorrespondedVertexLoops(VertIDs[0], MatchedIndices[0], VertIDs[1], MatchedIndices[1], ResultOut, Side == 0);

			// re-enable tangents and visibility attributes, since these are lost when we set the mesh to a copy of the plane mesh
			CellMeshes[Side].AugMesh.Augment();
		}
	}
};



// organize metadata corresponding dynamic mesh and geometry collection data
struct FDynamicMeshCollection
{
	struct FMeshData
	{
		FAugmentedDynamicMesh AugMesh;
		
		// FDynamicMeshAABBTree3 Spatial; // TODO: maybe refactor mesh booleans to allow version where caller provides spatial data; it's computed every boolean now
		// FTransform3d Transform; // TODO: maybe pretransform the data to a space that is good for cutting; refactor mesh boolean so there is an option to have it not transform input
		int32 TransformIndex; // where the mesh was from in the geometry collection
		FTransform ToCollection; // transform that need be applied to go back to the local space of the geometry collection
	};
	TArray<FMeshData> Meshes;
	FAxisAlignedBox3d Bounds;

	FDynamicMeshCollection(const FGeometryCollection* Collection, const TArrayView<const int32>& TransformIndices, FTransform TransformCells)
	{
		Init(Collection, TransformIndices, TransformCells);
	}

	void Init(const FGeometryCollection* Collection, const TArrayView<const int32>& TransformIndices, FTransform TransformCells)
	{
		FTransform WorldToCells = TransformCells.Inverse();

		Meshes.Reset();
		Bounds = FAxisAlignedBox3d::Empty();

		for (int32 TransformIdx : TransformIndices)
		{
			if (Collection->Children[TransformIdx].Num() > 0)
			{
				// only store the meshes of leaf nodes
				continue;
			}

			FTransform3d CollectionToLocal = FTransform3d(GeometryCollectionAlgo::GlobalMatrix(Collection->Transform, Collection->Parent, TransformIdx) * WorldToCells);

			FMeshData& MeshData = Meshes.Emplace_GetRef();
			MeshData.TransformIndex = TransformIdx;
			MeshData.ToCollection = FTransform(CollectionToLocal.Inverse());
			FDynamicMesh3& Mesh = MeshData.AugMesh.Mesh;

			int32 GeometryIdx = Collection->TransformToGeometryIndex[TransformIdx];
			Mesh.EnableAttributes();
			Mesh.Attributes()->EnableMaterialID();

			int32 VertexStart = Collection->VertexStart[GeometryIdx];
			int32 VertexCount = Collection->VertexCount[GeometryIdx];
			int32 FaceCount = Collection->FaceCount[GeometryIdx];

			FVertexInfo VertexInfo;
			VertexInfo.bHaveC = true;
			VertexInfo.bHaveN = true;
			VertexInfo.bHaveUV = true;
			for (int32 Idx = VertexStart, N = VertexStart + VertexCount; Idx < N; Idx++)
			{
				VertexInfo.Position = CollectionToLocal.TransformPosition(FVector3d(Collection->Vertex[Idx]));
				VertexInfo.UV = FVector2f(Collection->UV[Idx]);
				VertexInfo.Color = FVector3f(Collection->Color[Idx]);
				VertexInfo.Normal = CollectionToLocal.TransformNormal(FVector3f(Collection->Normal[Idx]));
				int VID = Mesh.AppendVertex(VertexInfo);
				MeshData.AugMesh.SetTangent(VID, VertexInfo.Normal, Collection->TangentU[Idx], Collection->TangentV[Idx]);
			}
			FIntVector VertexOffset(VertexStart, VertexStart, VertexStart);
			for (int32 Idx = Collection->FaceStart[GeometryIdx], N = Collection->FaceStart[GeometryIdx] + FaceCount; Idx < N; Idx++)
			{
				int TID = Mesh.AppendTriangle(FIndex3i(Collection->Indices[Idx] - VertexOffset), 0);
				Mesh.Attributes()->GetMaterialID()->SetValue(TID, Collection->MaterialID[Idx]);
				MeshData.AugMesh.SetVisibility(TID, Collection->Visible[Idx]);
				// note: material index doesn't need to be passed through; will be rebuilt by a call to reindex materials once the cut mesh is returned back to geometry collection format
			}

			FDynamicMeshEditor Editor(&Mesh);
			Editor.RemoveIsolatedVertices();

			Bounds.Contain(Mesh.GetCachedBounds());

			// TODO: build spatial data (add this after setting up mesh boolean path that can use it)
			//MeshData.Spatial.SetMesh(&Mesh);
		}
	}


	int32 CutWithMultiplePlanes(
		const TArrayView<const FPlane>& Planes, 
		FGeometryCollection* Collection,
		FInternalSurfaceMaterials& InternalSurfaceMaterials,
		bool bSetDefaultInternalMaterialsFromCollection
		)
	{
#if WITH_EDITOR
		// Create progress indicator dialog
		static const FText SlowTaskText = NSLOCTEXT("CutMultipleWithMultiplePlanes", "CutMultipleWithMultiplePlanesText", "Cutting geometry collection with plane(s)...");

		FScopedSlowTask SlowTask(Planes.Num(), SlowTaskText);
		SlowTask.MakeDialog();

		// Declare progress shortcut lambdas
		auto EnterProgressFrame = [&SlowTask](float Progress)
		{
			SlowTask.EnterProgressFrame(Progress);
		};
#else
		auto EnterProgressFrame = [](float Progress) {};
#endif

		bool bHasProximity = Collection->HasAttribute("Proximity", FGeometryCollection::GeometryGroup);
		TArray<TUniquePtr<FMeshData>> ToCut;
		TArray<TUniquePtr<TPointHashGrid3d<int>>> VerticesHashes;
		auto HashMeshVertices = [&VerticesHashes, &ToCut](int32 HashIdx)
		{
			FDynamicMesh3& Mesh = ToCut[HashIdx]->AugMesh.Mesh;
			if (HashIdx >= VerticesHashes.Num())
			{
				VerticesHashes.SetNum(HashIdx + 1);
			}
			if (VerticesHashes[HashIdx].IsValid())
			{
				return;
			}
			VerticesHashes[HashIdx] = MakeUnique<TPointHashGrid3d<int>>(FMathd::ZeroTolerance * 1000, -1);
			TPointHashGrid3d<int>& Grid = *VerticesHashes[HashIdx].Get();
			for (int VID : Mesh.VertexIndicesItr())
			{
				Grid.InsertPointUnsafe(VID, Mesh.GetVertex(VID));
			}
		};
		auto ClearHash = [&VerticesHashes](int32 HashIdx)
		{
			if (HashIdx < VerticesHashes.Num())
			{
				VerticesHashes[HashIdx].Release();
			}
		};
		auto IsNeighbor = [&ToCut, &VerticesHashes](int32 A, int32 B)
		{
			if (!ensure(A < ToCut.Num() && B < ToCut.Num() && A < VerticesHashes.Num() && B < VerticesHashes.Num()))
			{
				return false;
			}
			if (!ensure(VerticesHashes[A].IsValid() && VerticesHashes[B].IsValid()))
			{
				return false;
			}
			if (!ToCut[A]->AugMesh.Mesh.GetCachedBounds().Intersects(ToCut[B]->AugMesh.Mesh.GetCachedBounds()))
			{
				return false;
			}
			if (ToCut[A]->AugMesh.Mesh.VertexCount() > ToCut[B]->AugMesh.Mesh.VertexCount())
			{
				Swap(A, B);
			}
			FDynamicMesh3& RefMesh = ToCut[B]->AugMesh.Mesh;
			for (const FVector3d& V : ToCut[A]->AugMesh.Mesh.VerticesItr())
			{
				TPair<int, double> Nearest = VerticesHashes[B]->FindNearestInRadius(V, FMathd::ZeroTolerance * 10, [&RefMesh, &V](int VID)
					{
						return RefMesh.GetVertex(VID).DistanceSquared(V);
					});
				if (Nearest.Key != -1)
				{
					return true;
				}
			}
			return false;
		};
		// copy initial surfaces
		for (FMeshData& MeshData : Meshes)
		{
			ToCut.Add(MakeUnique<FMeshData>(MeshData));
		}
		// track connections between meshes via their indices in the ToCut array
		TMultiMap<int32, int32> Proximity;
		auto ProxLink = [&Proximity](int32 A, int32 B)
		{
			Proximity.Add(A, B);
			Proximity.Add(B, A);
		};
		auto ProxUnlink = [&Proximity](int32 A, int32 B)
		{
			Proximity.RemoveSingle(A, B);
			Proximity.RemoveSingle(B, A);
		};
		for (int32 PlaneIdx = 0; PlaneIdx < Planes.Num(); PlaneIdx++)
		{
			EnterProgressFrame(1);
			FPlanarCells PlaneCells(Planes[PlaneIdx]);
			PlaneCells.InternalSurfaceMaterials = InternalSurfaceMaterials;
			FCellMeshes CellMeshes(PlaneCells, Bounds, InternalSurfaceMaterials.NoiseSettings.IsSet() ? InternalSurfaceMaterials.NoiseSettings->Amplitude : 0, false);

			// TODO: we could do these cuts in parallel (will takes some rework of the proximity and how results are added to the ToCut array)
			for (int32 ToCutIdx = 0, ToCutNum = ToCut.Num(); ToCutIdx < ToCutNum; ToCutIdx++)
			{
				FMeshData& Surface = *ToCut[ToCutIdx];

				FAxisAlignedBox3d Box = Surface.AugMesh.Mesh.GetCachedBounds();
				if (InternalSurfaceMaterials.NoiseSettings.IsSet())
				{
					Box.Expand(InternalSurfaceMaterials.NoiseSettings->Amplitude);
				}
				if (!FMath::PlaneAABBIntersection(Planes[PlaneIdx], FBox(Box)))
				{
					continue;
				}

				TArray<TUniquePtr<FMeshData>> BoolResults;
				for (int ResultIdx = 0; ResultIdx < 2; ResultIdx++)
				{
					BoolResults.Add(MakeUnique<FMeshData>());
					BoolResults[ResultIdx]->TransformIndex = Surface.TransformIndex;
					BoolResults[ResultIdx]->ToCollection = Surface.ToCollection;
				}
				check(CellMeshes.CellMeshes.Num() == 2);
				bool bKeepResults = true;
				for (int32 CellIdx = 0; CellIdx < 2; CellIdx++)
				{
					FCellMeshes::FCellInfo& Cell = CellMeshes.CellMeshes[CellIdx];

					FMeshBoolean::EBooleanOp Op = FMeshBoolean::EBooleanOp::Intersect;
					if (CellIdx == CellMeshes.OutsideCellIndex)
					{
						Op = FMeshBoolean::EBooleanOp::Difference;
					}
					FMeshBoolean Boolean(&Surface.AugMesh.Mesh, &Cell.AugMesh.Mesh, &BoolResults[CellIdx]->AugMesh.Mesh, Op);
					Boolean.bWeldSharedEdges = false;
					if (!Boolean.Compute())
					{
						// TODO: do something about failure cases?  e.g. try auto-filling small holes?
						// note: failure cases won't be detected at all unless we weld edges,
						//       which will require re-working how tangents are carried through
					}
					if (BoolResults[CellIdx]->AugMesh.Mesh.TriangleCount() == 0)
					{
						bKeepResults = false;
						break;
					}
				}

				if (bKeepResults)
				{
					ToCut[ToCutIdx] = MoveTemp(BoolResults[0]);
					int32 NewIdx = ToCut.Add(MoveTemp(BoolResults[1]));
					int32 ResultIndices[2] = { ToCutIdx, NewIdx };

					// update proximity for neighbors of the original piece
					if (bHasProximity)
					{
						ClearHash(ToCutIdx);
						TArray<int32> Nbrs;
						Proximity.MultiFind(ToCutIdx, Nbrs);
						if (Nbrs.Num() > 0)
						{
							HashMeshVertices(ToCutIdx);
							HashMeshVertices(NewIdx);

							for (int32 Nbr : Nbrs)
							{
								ProxUnlink(ToCutIdx, Nbr);
								HashMeshVertices(Nbr);
								for (int32 Idx = 0; Idx < 2; Idx++)
								{
									int32 ResultIdx = ResultIndices[Idx];
									int32 OldIdx = Nbr;
									if (IsNeighbor(ResultIdx, OldIdx))
									{
										ProxLink(ResultIdx, OldIdx);
									}
								}
							}
						}
					}

					// add the connection between the two new pieces
					ProxLink(ResultIndices[0], ResultIndices[1]);
				}
			}
		}

		TMultiMap<int32, int32> ParentTransformToChildren;
		for (int32 ToCutIdx = 0; ToCutIdx < ToCut.Num(); ToCutIdx++)
		{
			ParentTransformToChildren.Add(ToCut[ToCutIdx]->TransformIndex, ToCutIdx);
		}

		TArray<int32> ToCutIdxToGeometryIdx;  ToCutIdxToGeometryIdx.Init(-1, ToCut.Num());
		TArray<int32> ToCutIndices;
		int32 FirstCreatedIndex = -1;
		for (FMeshData& MeshData : Meshes)
		{
			int32 GeometryIdx = Collection->TransformToGeometryIndex[MeshData.TransformIndex];
			int32 MaterialIDOverride = bSetDefaultInternalMaterialsFromCollection ? InternalSurfaceMaterials.GetDefaultMaterialIDForGeometry(*Collection, GeometryIdx) : -1;
			ParentTransformToChildren.MultiFind(MeshData.TransformIndex, ToCutIndices);

			// if there's only one mesh here, i.e. it didn't get cut at all
			if (ToCutIndices.Num() <= 1)
			{
				continue;
			}

			// hide old parent geometry
			SetVisibility(*Collection, GeometryIdx, false);

			// add newly created geometry as children
			int32 SubPartIdx = 0;
			for (int32 ToCutIdx : ToCutIndices)
			{
				ToCut[ToCutIdx]->AugMesh.FixTangents();
				for (int TID : ToCut[ToCutIdx]->AugMesh.Mesh.TriangleIndicesItr())
				{
					int GID = ToCut[ToCutIdx]->AugMesh.Mesh.GetTriangleGroup(TID);
					if (GID > 0)
					{
						if (MaterialIDOverride != -1)
						{
							ToCut[ToCutIdx]->AugMesh.Mesh.Attributes()->GetMaterialID()->SetNewValue(TID, MaterialIDOverride);
						}
					}
				}
				int32 CreatedGeometryIdx = AppendToCollection(ToCut[ToCutIdx]->ToCollection, ToCut[ToCutIdx]->AugMesh, ToCut[ToCutIdx]->TransformIndex, SubPartIdx++, *Collection);
				ToCutIdxToGeometryIdx[ToCutIdx] = CreatedGeometryIdx;
				if (FirstCreatedIndex == -1)
				{
					FirstCreatedIndex = CreatedGeometryIdx;
				}
			}
		}

		// create proximity sets on geometry collection and populate using ToCut's Proximity multimap and the array ToCutIdxToGeometryIdx
		if (bHasProximity)
		{
			TManagedArray<TSet<int32>>& GCProximity = Collection->GetAttribute<TSet<int32>>("Proximity", FGeometryCollection::GeometryGroup);
			for (TPair<int32, int32> Link : Proximity)
			{
				GCProximity[ToCutIdxToGeometryIdx[Link.Key]].Add(ToCutIdxToGeometryIdx[Link.Value]);
			}
		}

		return FirstCreatedIndex;
	}

	/**
	 * Cut collection meshes with cell meshes, and append results to a geometry collection
	 *
	 * @param PlanarCells The source definitions of the cells to cut with
	 * @param CellsMeshes Meshed versions of the cells, with noise and material properties baked in
	 * @param Collection Results will be stored in this
	 * @param bSetDefaultInternalMaterialsFromCollection If true, set internal materials to the most common external material + 1, following a convenient artist convention
	 * @return Index of the first created geometry
	 */
	int32 CutWithCellMeshes(const FPlanarCells& PlanarCells, FCellMeshes& CellMeshes, FGeometryCollection* Collection, bool bSetDefaultInternalMaterialsFromCollection)
	{
		// TODO: should we do these cuts in parallel, and the appends sequentially below?
		int32 FirstIdx = -1;
		int BadCount = 0;
		bool bHasProximity = Collection->HasAttribute("Proximity", FGeometryCollection::GeometryGroup);
		for (FMeshData& Surface : Meshes)
		{
			int32 GeometryIdx = Collection->TransformToGeometryIndex[Surface.TransformIndex];
			TArray<FAugmentedDynamicMesh> BooleanResults;  BooleanResults.SetNum(CellMeshes.CellMeshes.Num());
			ParallelFor(CellMeshes.CellMeshes.Num(), [&BooleanResults, &CellMeshes, &Surface](int32 CellIdx)
				{
					FCellMeshes::FCellInfo& Cell = CellMeshes.CellMeshes[CellIdx];
					if (Cell.AugMesh.Mesh.GetCachedBounds().Intersects(Surface.AugMesh.Mesh.GetCachedBounds()))
					{
						FAugmentedDynamicMesh& AugBoolResult = BooleanResults[CellIdx];

						FMeshBoolean::EBooleanOp Op = FMeshBoolean::EBooleanOp::Intersect;
						if (CellIdx == CellMeshes.OutsideCellIndex)
						{
							Op = FMeshBoolean::EBooleanOp::Difference;
						}
						FMeshBoolean Boolean(&Surface.AugMesh.Mesh, &Cell.AugMesh.Mesh, &AugBoolResult.Mesh, Op);
						Boolean.bWeldSharedEdges = false;
						if (!Boolean.Compute())
						{
							// TODO: do something about failure cases?  e.g. try auto-filling small holes?
							// note: failure cases won't be detected at all unless we weld edges,
							//       which will require re-working how tangents are carried through
						}
					}
				}, EParallelForFlags::None);

			int32 NonEmptyResults = 0;
			for (const FAugmentedDynamicMesh& AugBoolResult : BooleanResults)
			{
				if (AugBoolResult.Mesh.TriangleCount() > 0)
				{
					NonEmptyResults++;
				}
			}

			if (NonEmptyResults > 1) // only write to geometry collection if more than one result was non-empty
			{
				TSet<int32> PlanesInOutput;
				TMap<int32, int32> CellToGeometry;
				int32 SubPartIndex = 0;
				int32 MaterialIDOverride = bSetDefaultInternalMaterialsFromCollection ? PlanarCells.InternalSurfaceMaterials.GetDefaultMaterialIDForGeometry(*Collection, GeometryIdx) : -1;

				for (int32 CellIdx = 0; CellIdx < CellMeshes.CellMeshes.Num(); CellIdx++)
				{
					FAugmentedDynamicMesh& AugBoolResult = BooleanResults[CellIdx];
					if (AugBoolResult.Mesh.TriangleCount() > 0)
					{
						AugBoolResult.FixTangents();
						for (int TID : AugBoolResult.Mesh.TriangleIndicesItr())
						{
							int GID = AugBoolResult.Mesh.GetTriangleGroup(TID);
							int32 PlaneIdx = CellMeshes.GroupToPlane(GID);
							if (PlaneIdx >= 0)
							{
								PlanesInOutput.Add(PlaneIdx);
								if (MaterialIDOverride != -1)
								{
									AugBoolResult.Mesh.Attributes()->GetMaterialID()->SetNewValue(TID, MaterialIDOverride);
								}
							}
						}
						int32 CreatedGeometryIdx = AppendToCollection(Surface.ToCollection, AugBoolResult, Surface.TransformIndex, SubPartIndex, *Collection);
						CellToGeometry.Add(CellIdx, CreatedGeometryIdx);
						if (FirstIdx == -1)
						{
							FirstIdx = CreatedGeometryIdx;
						}
						SubPartIndex++;
					}
					AugBoolResult.Mesh.Clear(); // we can stop storing the dynamic mesh version now that it's in the geometry collection
				}
				if (bHasProximity)
				{
					TManagedArray<TSet<int32>>& Proximity = Collection->GetAttribute<TSet<int32>>("Proximity", FGeometryCollection::GeometryGroup);
					for (int32 PlaneIdx : PlanesInOutput)
					{
						TPair<int32, int32> Cells = PlanarCells.PlaneCells[PlaneIdx];
						int32 SecondCell = Cells.Value == -1 ? CellMeshes.OutsideCellIndex : Cells.Value;
						if (SecondCell != -1)
						{
							int32* GeomA = CellToGeometry.Find(Cells.Key);
							int32* GeomB = CellToGeometry.Find(SecondCell);
							if (GeomA && GeomB)
							{
								Proximity[*GeomA].Add(*GeomB);
								Proximity[*GeomB].Add(*GeomA);
							}
						}
					}
				}
				// turn off old geom visibility (preferred default behavior)
				SetVisibility(*Collection, GeometryIdx, false);
			}
		}

		return FirstIdx;
	}

	static void SetVisibility(FGeometryCollection& Collection, int32 GeometryIdx, bool bVisible)
	{
		int32 FaceEnd = Collection.FaceCount[GeometryIdx] + Collection.FaceStart[GeometryIdx];
		for (int32 FaceIdx = Collection.FaceStart[GeometryIdx]; FaceIdx < FaceEnd; FaceIdx++)
		{
			Collection.Visible[FaceIdx] = bVisible;
		}
	}

	static int32 AppendToCollection(const FTransform& ToCollection, FAugmentedDynamicMesh& AugMesh, int32 TransformParent, int32 SubPartIndex, FGeometryCollection& Output)
	{
		FDynamicMesh3& Mesh = AugMesh.Mesh;
		if (Mesh.TriangleCount() == 0)
		{
			return -1;
		}

		if (!Mesh.IsCompact())
		{
			Mesh.CompactInPlace(nullptr);
		}

		int32 NewGeometryStartIdx = Output.FaceStart.Num();
		int32 OriginalVertexNum = Output.Vertex.Num();
		int32 OriginalFaceNum = Output.Indices.Num();

		int32 GeometryIdx = Output.AddElements(1, FGeometryCollection::GeometryGroup);
		int32 TransformIdx = Output.AddElements(1, FGeometryCollection::TransformGroup);

		int32 NumTriangles = Mesh.TriangleCount();
		int32 NumVertices = Mesh.VertexCount();
		check(NumTriangles > 0);
		check(Mesh.IsCompact());
		Output.FaceCount[GeometryIdx] = NumTriangles;
		Output.FaceStart[GeometryIdx] = OriginalFaceNum;
		Output.VertexCount[GeometryIdx] = NumVertices;
		Output.VertexStart[GeometryIdx] = OriginalVertexNum;
		Output.TransformIndex[GeometryIdx] = TransformIdx;
		Output.TransformToGeometryIndex[TransformIdx] = GeometryIdx;
		if (TransformParent > -1)
		{
			// TODO: this is probably not the best way to build the bone name string?
			Output.BoneName[TransformIdx] = Output.BoneName[TransformParent] + "_" + FString::FromInt(SubPartIndex);
			Output.BoneColor[TransformIdx] = Output.BoneColor[TransformParent];
			Output.Parent[TransformIdx] = TransformParent;
			Output.Children[TransformParent].Add(TransformIdx);
		}
		Output.Transform[TransformIdx] = FTransform::Identity;

		int32 FacesStart = Output.AddElements(NumTriangles, FGeometryCollection::FacesGroup);
		int32 VerticesStart = Output.AddElements(NumVertices, FGeometryCollection::VerticesGroup);

		for (int32 VID = 0; VID < Mesh.MaxVertexID(); VID++)
		{
			checkSlow(Mesh.IsVertex(VID)); // mesh is compact
			int32 CopyToIdx = VerticesStart + VID;
			Output.Vertex[CopyToIdx] = ToCollection.TransformPosition(FVector(Mesh.GetVertex(VID)));
			Output.Normal[CopyToIdx] = ToCollection.TransformVectorNoScale(FVector(Mesh.GetVertexNormal(VID))); // TODO: use correct normal transform instead
			Output.UV[CopyToIdx] = FVector2D(Mesh.GetVertexUV(VID));
			FVector3f TangentU, TangentV;
			AugMesh.GetTangent(VID, TangentU, TangentV);
			Output.TangentU[CopyToIdx] = ToCollection.TransformVectorNoScale(FVector(TangentU));
			Output.TangentV[CopyToIdx] = ToCollection.TransformVectorNoScale(FVector(TangentV));
			Output.Color[CopyToIdx] = FVector(Mesh.GetVertexColor(VID));

			// Bone map is set based on the transform of the new geometry
			Output.BoneMap[CopyToIdx] = TransformIdx;
		}

		FIntVector VertexStartOffset(VerticesStart);
		for (int32 TID = 0; TID < Mesh.MaxTriangleID(); TID++)
		{
			check(Mesh.IsTriangle(TID));
			int32 CopyToIdx = FacesStart + TID;
			Output.Visible[CopyToIdx] = AugMesh.GetVisibility(TID);
			Output.MaterialID[CopyToIdx] = Mesh.Attributes()->GetMaterialID()->GetValue(TID);
			Output.Indices[CopyToIdx] = FIntVector(Mesh.GetTriangle(TID)) + VertexStartOffset;
		}

		if (Output.BoundingBox.Num())
		{
			Output.BoundingBox[GeometryIdx].Init();
			for (int32 Idx = OriginalVertexNum; Idx < Output.Vertex.Num(); ++Idx)
			{
				Output.BoundingBox[GeometryIdx] += Output.Vertex[Idx];
			}
		}

		return GeometryIdx;
	}
};


// logic from FMeshUtility::GenerateGeometryCollectionFromBlastChunk, sets material IDs based on construction pattern that external materials have even IDs and are matched to internal materials at InternalID = ExternalID+1
int32 FInternalSurfaceMaterials::GetDefaultMaterialIDForGeometry(const FGeometryCollection& Collection, int32 GeometryIdx) const
{
	int32 FaceStart = 0;
	int32 FaceEnd = Collection.Indices.Num();
	if (GeometryIdx > -1)
	{
		FaceStart = Collection.FaceStart[GeometryIdx];
		FaceEnd = Collection.FaceCount[GeometryIdx] + Collection.FaceStart[GeometryIdx];
	}

	// find most common non interior material
	TMap<int32, int32> MaterialIDCount;
	int32 MaxCount = 0;
	int32 MostCommonMaterialID = -1;
	const TManagedArray<int32>&  MaterialID = Collection.MaterialID;
	for (int i = FaceStart; i < FaceEnd; ++i)
	{
		int32 CurrID = MaterialID[i];
		int32 &CurrCount = MaterialIDCount.FindOrAdd(CurrID);
		CurrCount++;

		if (CurrCount > MaxCount)
		{
			MaxCount = CurrCount;
			MostCommonMaterialID = CurrID;
		}
	}

	// no face case?
	if (MostCommonMaterialID == -1)
	{
		MostCommonMaterialID = 0;
	}

	// We know that the internal materials are the ones that come right after the surface materials
	// #todo(dmp): formalize the mapping between material and internal material, perhaps on the GC
	// if the most common material is an internal material, then just use this
	int32 InternalMaterialID = MostCommonMaterialID % 2 == 0 ? MostCommonMaterialID + 1 : MostCommonMaterialID;

	return InternalMaterialID;
}

void FInternalSurfaceMaterials::SetUVScaleFromCollection(const FGeometryCollection& Collection, int32 GeometryIdx)
{
	int32 FaceStart = 0;
	int32 FaceEnd = Collection.Indices.Num();
	if (GeometryIdx > -1)
	{
		FaceStart = Collection.FaceStart[GeometryIdx];
		FaceEnd = Collection.FaceCount[GeometryIdx] + Collection.FaceStart[GeometryIdx];
	}
	float UVDistance = 0;
	float WorldDistance = 0;
	for (int32 FaceIdx = FaceStart; FaceIdx < FaceEnd; FaceIdx++)
	{
		const FIntVector& Tri = Collection.Indices[FaceIdx];
		WorldDistance += FVector::Distance(Collection.Vertex[Tri.X], Collection.Vertex[Tri.Y]);
		UVDistance += FVector2D::Distance(Collection.UV[Tri.X], Collection.UV[Tri.Y]);
		WorldDistance += FVector::Distance(Collection.Vertex[Tri.Z], Collection.Vertex[Tri.Y]);
		UVDistance += FVector2D::Distance(Collection.UV[Tri.Z], Collection.UV[Tri.Y]);
		WorldDistance += FVector::Distance(Collection.Vertex[Tri.X], Collection.Vertex[Tri.Z]);
		UVDistance += FVector2D::Distance(Collection.UV[Tri.X], Collection.UV[Tri.Z]);
	}

	if (WorldDistance > 0)
	{
		GlobalUVScale =  UVDistance / WorldDistance;
	}
	if (GlobalUVScale <= 0)
	{
		GlobalUVScale = 1;
	}
}

inline double PlaneDotDouble(const FPlane& Plane, const FVector& V)
{
	return (double)Plane.X*V.X + Plane.Y*V.Y + Plane.Z*V.Z - Plane.W;
}


inline int PlaneSide(const FPlane &Plane, const FVector& V, const double Epsilon = 1e-3)
{
	double SD = PlaneDotDouble(Plane, V);
	return SD > Epsilon ? 1 : SD < -Epsilon ? -1 : 0;
}

// TODO: warning -- If Epsilon is too small, we can hit an infinite loop on mesh cutting (if the edge cut is still seen as crossing!)
inline bool IsSegmentCrossing(const FPlane &Plane, const FVector& A, const FVector& B, double& CrossingT, const double Epsilon = 1e-3)
{
	double SDA = PlaneDotDouble(Plane, A);
	double SDB = PlaneDotDouble(Plane, B);
	CrossingT = SDA / (SDA - SDB);
	int32 ASide = SDA < -Epsilon ? -1 : SDA > Epsilon ? 1 : 0;
	int32 BSide = SDB < -Epsilon ? -1 : SDB > Epsilon ? 1 : 0;
	return ASide * BSide == -1 && CrossingT < 1 - Epsilon && CrossingT > Epsilon;
}


FPlanarCells::FPlanarCells(const FPlane& P)
{
	NumCells = 2;
	AddPlane(P, 0, 1);

	CellFromPosition = TFunction<int32(FVector)>([P](FVector Position)
	{
		return PlaneDotDouble(P, Position) > 0 ? 1 : 0;
	});
}

FPlanarCells::FPlanarCells(const TArrayView<const FVector> Sites, FVoronoiDiagram &Voronoi)
{
	TArray<FVoronoiCellInfo> VoronoiCells;
	Voronoi.ComputeAllCells(VoronoiCells);

	AssumeConvexCells = true;
	NumCells = VoronoiCells.Num();
	CellFromPosition = TFunction<int32(FVector)>([&](FVector Position)
	{
		return Voronoi.FindCell(Position);
	});
	for (int32 CellIdx = 0; CellIdx < NumCells; CellIdx++)
	{
		int32 LocalVertexStart = -1;

		const FVoronoiCellInfo& CellInfo = VoronoiCells[CellIdx];
		int32 CellFaceVertexIndexStart = 0;
		for (int32 CellFaceIdx = 0; CellFaceIdx < CellInfo.Neighbors.Num(); CellFaceIdx++, CellFaceVertexIndexStart += 1 + CellInfo.Faces[CellFaceVertexIndexStart])
		{
			int32 NeighborIdx = CellInfo.Neighbors[CellFaceIdx];
			if (CellIdx < NeighborIdx)  // Filter out faces that we expect to get by symmetry
			{
				continue;
			}

			FVector Normal = CellInfo.Normals[CellFaceIdx];
			if (Normal.IsZero())
			{
				if (NeighborIdx > -1)
				{
					Normal = Sites[NeighborIdx] - Sites[CellIdx];
					bool bNormalizeSucceeded = Normal.Normalize();
					ensureMsgf(bNormalizeSucceeded, TEXT("Voronoi diagram should not have Voronoi sites so close together!"));
				}
				else
				{
					// degenerate face on border; likely almost zero area so hopefully it won't matter if we just don't add it
					continue;
				}
			}
			FPlane P(Normal, FVector::DotProduct(Normal, CellInfo.Vertices[CellInfo.Faces[CellFaceVertexIndexStart + 1]]));
			if (LocalVertexStart < 0)
			{
				LocalVertexStart = PlaneBoundaryVertices.Num();
				PlaneBoundaryVertices.Append(CellInfo.Vertices);
			}
			TArray<int32> PlaneBoundary;
			int32 FaceSize = CellInfo.Faces[CellFaceVertexIndexStart];
			for (int32 i = 0; i < FaceSize; i++)
			{
				int32 CellVertexIdx = CellInfo.Faces[CellFaceVertexIndexStart + 1 + i];
				PlaneBoundary.Add(LocalVertexStart + CellVertexIdx);
			}

			AddPlane(P, CellIdx, NeighborIdx, PlaneBoundary);
		}
	}
}

FPlanarCells::FPlanarCells(const TArrayView<const FBox> Boxes)
{
	AssumeConvexCells = true;
	NumCells = Boxes.Num();
	CellFromPosition = TFunction<int32(FVector)>([&](FVector Position)
	{
		for (int32 Idx = 0; Idx < Boxes.Num(); Idx++)
		{
			if (Boxes[Idx].IsInsideOrOn(Position))
			{
				return Idx;
			}
		}
		return -1;
	});

	for (int32 BoxIdx = 0; BoxIdx < NumCells; BoxIdx++)
	{
		const FBox &Box = Boxes[BoxIdx];
		const FVector &Min = Box.Min;
		const FVector &Max = Box.Max;

		int32 VIdx = PlaneBoundaryVertices.Num();
		PlaneBoundaryVertices.Add(Min);
		PlaneBoundaryVertices.Add(FVector(Max.X, Min.Y, Min.Z));
		PlaneBoundaryVertices.Add(FVector(Max.X, Max.Y, Min.Z));
		PlaneBoundaryVertices.Add(FVector(Min.X, Max.Y, Min.Z));

		PlaneBoundaryVertices.Add(FVector(Min.X, Min.Y, Max.Z));
		PlaneBoundaryVertices.Add(FVector(Max.X, Min.Y, Max.Z));
		PlaneBoundaryVertices.Add(Max);
		PlaneBoundaryVertices.Add(FVector(Min.X, Max.Y, Max.Z));

		AddPlane(FPlane(FVector(0, 0, -1), -Min.Z), BoxIdx, -1, { VIdx + 0, VIdx + 1, VIdx + 2, VIdx + 3 });
		AddPlane(FPlane(FVector(0, 0, 1),	Max.Z), BoxIdx, -1, { VIdx + 4, VIdx + 7, VIdx + 6, VIdx + 5 });
		AddPlane(FPlane(FVector(0, -1, 0), -Min.Y), BoxIdx, -1, { VIdx + 0, VIdx + 4, VIdx + 5, VIdx + 1 });
		AddPlane(FPlane(FVector(0, 1, 0),	Max.Y), BoxIdx, -1, { VIdx + 3, VIdx + 2, VIdx + 6, VIdx + 7 });
		AddPlane(FPlane(FVector(-1, 0, 0), -Min.X), BoxIdx, -1, { VIdx + 0, VIdx + 3, VIdx + 7, VIdx + 4 });
		AddPlane(FPlane(FVector(1, 0, 0),	Max.X), BoxIdx, -1, { VIdx + 1, VIdx + 5, VIdx + 6, VIdx + 2 });
	}
}

FPlanarCells::FPlanarCells(const FBox &Region, const FIntVector& CubesPerAxis)
{
	AssumeConvexCells = true;
	NumCells = CubesPerAxis.X * CubesPerAxis.Y * CubesPerAxis.Z;

	CellFromPosition = TFunction<int32(FVector)>([&](FVector Position)
	{
		if (!Region.IsInsideOrOn(Position))
		{
			return -1;
		}
		FVector Diagonal = Region.Max - Region.Min;
		FVector RelPos = Position - Region.Min;
		FIntVector GridIdx(
			CubesPerAxis.X * (RelPos.X / Diagonal.X), 
			CubesPerAxis.Y * (RelPos.Y / Diagonal.Y), 
			CubesPerAxis.Z * (RelPos.Z / Diagonal.Z)
		);
		GridIdx.X = FMath::Clamp(GridIdx.X, 0, CubesPerAxis.X - 1);
		GridIdx.Y = FMath::Clamp(GridIdx.Y, 0, CubesPerAxis.Y - 1);
		GridIdx.Z = FMath::Clamp(GridIdx.Z, 0, CubesPerAxis.Z - 1);
		return GridIdx.X + GridIdx.Y * (CubesPerAxis.X) + GridIdx.Z * (CubesPerAxis.X * CubesPerAxis.Y);
	});

	// cube X, Y, Z integer indices to a single cell index
	auto ToIdx = [](const FIntVector &PerAxis, int32 Xi, int32 Yi, int32 Zi)
	{
		if (Xi < 0 || Xi >= PerAxis.X || Yi < 0 || Yi >= PerAxis.Y || Zi < 0 || Zi >= PerAxis.Z)
		{
			return -1;
		}
		else
		{
			return Xi + Yi * (PerAxis.X) + Zi * (PerAxis.X * PerAxis.Y);
		}
	};

	auto ToIdxUnsafe = [](const FIntVector &PerAxis, int32 Xi, int32 Yi, int32 Zi)
	{
		return Xi + Yi * (PerAxis.X) + Zi * (PerAxis.X * PerAxis.Y);
	};

	FIntVector VertsPerAxis = CubesPerAxis + FIntVector(1);
	PlaneBoundaryVertices.SetNum(VertsPerAxis.X * VertsPerAxis.Y * VertsPerAxis.Z);

	FVector Diagonal = Region.Max - Region.Min;
	FVector CellSizes(
		Diagonal.X / CubesPerAxis.X,
		Diagonal.Y / CubesPerAxis.Y,
		Diagonal.Z / CubesPerAxis.Z
	);
	int32 VertIdx = 0;
	for (int32 Zi = 0; Zi < VertsPerAxis.Z; Zi++)
	{
		for (int32 Yi = 0; Yi < VertsPerAxis.Y; Yi++)
		{
			for (int32 Xi = 0; Xi < VertsPerAxis.X; Xi++)
			{
				PlaneBoundaryVertices[VertIdx] = Region.Min + FVector(Xi * CellSizes.X, Yi * CellSizes.Y, Zi * CellSizes.Z);
				ensure(VertIdx == ToIdxUnsafe(VertsPerAxis, Xi, Yi, Zi));
				VertIdx++;
			}
		}
	}
	float Z = Region.Min.Z;
	int32 ZSliceSize = VertsPerAxis.X * VertsPerAxis.Y;
	int32 VIdxOffs[8] = { 0, 1, VertsPerAxis.X + 1, VertsPerAxis.X, ZSliceSize, ZSliceSize + 1, ZSliceSize + VertsPerAxis.X + 1, ZSliceSize + VertsPerAxis.X };
	for (int32 Zi = 0; Zi < CubesPerAxis.Z; Zi++, Z += CellSizes.Z)
	{
		float Y = Region.Min.Y;
		float ZN = Z + CellSizes.Z;
		for (int32 Yi = 0; Yi < CubesPerAxis.Y; Yi++, Y += CellSizes.Y)
		{
			float X = Region.Min.X;
			float YN = Y + CellSizes.Y;
			for (int32 Xi = 0; Xi < CubesPerAxis.X; Xi++, X += CellSizes.X)
			{
				float XN = X + CellSizes.X;
				int VIdx = ToIdxUnsafe(VertsPerAxis, Xi, Yi, Zi);
				int BoxIdx = ToIdxUnsafe(CubesPerAxis, Xi, Yi, Zi);

				AddPlane(FPlane(FVector(0, 0, -1), -Z), BoxIdx, ToIdx(CubesPerAxis, Xi, Yi, Zi-1), { VIdx + VIdxOffs[0], VIdx + VIdxOffs[1], VIdx + VIdxOffs[2], VIdx + VIdxOffs[3] });
				AddPlane(FPlane(FVector(0, 0, 1), ZN), BoxIdx, ToIdx(CubesPerAxis, Xi, Yi, Zi+1), { VIdx + VIdxOffs[4], VIdx + VIdxOffs[7], VIdx + VIdxOffs[6], VIdx + VIdxOffs[5] });
				AddPlane(FPlane(FVector(0, -1, 0), -Y), BoxIdx, ToIdx(CubesPerAxis, Xi, Yi-1, Zi), { VIdx + VIdxOffs[0], VIdx + VIdxOffs[4], VIdx + VIdxOffs[5], VIdx + VIdxOffs[1] });
				AddPlane(FPlane(FVector(0, 1, 0), YN), BoxIdx, ToIdx(CubesPerAxis, Xi, Yi+1, Zi), { VIdx + VIdxOffs[3], VIdx + VIdxOffs[2], VIdx + VIdxOffs[6], VIdx + VIdxOffs[7] });
				AddPlane(FPlane(FVector(-1, 0, 0), -X), BoxIdx, ToIdx(CubesPerAxis, Xi-1, Yi, Zi), { VIdx + VIdxOffs[0], VIdx + VIdxOffs[3], VIdx + VIdxOffs[7], VIdx + VIdxOffs[4] });
				AddPlane(FPlane(FVector(1, 0, 0), XN), BoxIdx, ToIdx(CubesPerAxis, Xi+1, Yi, Zi), { VIdx + VIdxOffs[1], VIdx + VIdxOffs[5], VIdx + VIdxOffs[6], VIdx + VIdxOffs[2] });
			}
		}
	}
}

FPlanarCells::FPlanarCells(const FBox &Region, const TArrayView<const FColor> Image, int32 Width, int32 Height)
{
	const double SimplificationTolerance = 0.0; // TODO: implement simplification and make tolerance a param

	const FColor OutsideColor(0, 0, 0);

	int32 NumPix = Width * Height;
	check(Image.Num() == NumPix);

	// Union Find adapted from PBDRigidClustering.cpp version; customized to pixel grouping
	struct UnionFindInfo
	{
		int32 GroupIdx;
		int32 Size;
	};

	TArray<UnionFindInfo> PixCellUnions; // union find info per pixel
	TArray<int32> PixCells;  // Cell Index per pixel (-1 for OutsideColor pixels)

	PixCellUnions.SetNumUninitialized(NumPix);
	PixCells.SetNumUninitialized(NumPix);
	for (int32 i = 0; i < NumPix; ++i)
	{
		if (Image[i] == OutsideColor)
		{
			PixCellUnions[i].GroupIdx = -1;
			PixCellUnions[i].Size = 0;
			PixCells[i] = -1;
		}
		else
		{
			PixCellUnions[i].GroupIdx = i;
			PixCellUnions[i].Size = 1;
			PixCells[i] = -2;
		}
	}
	auto FindGroup = [&](int Idx) {
		int GroupIdx = Idx;

		int findIters = 0;
		while (PixCellUnions[GroupIdx].GroupIdx != GroupIdx)
		{
			ensure(findIters++ < 10); // if this while loop iterates more than a few times, there's probably a bug in the unionfind
			PixCellUnions[GroupIdx].GroupIdx = PixCellUnions[PixCellUnions[GroupIdx].GroupIdx].GroupIdx;
			GroupIdx = PixCellUnions[GroupIdx].GroupIdx;
		}

		return GroupIdx;
	};
	auto MergeGroup = [&](int A, int B) {
		int GroupA = FindGroup(A);
		int GroupB = FindGroup(B);
		if (GroupA == GroupB)
		{
			return;
		}
		if (PixCellUnions[GroupA].Size > PixCellUnions[GroupB].Size)
		{
			Swap(GroupA, GroupB);
		}
		PixCellUnions[GroupA].GroupIdx = GroupB;
		PixCellUnions[GroupB].Size += PixCellUnions[GroupA].Size;
	};
	// merge non-outside neighbors into groups
	int32 YOffs[4] = { -1, 0, 0, 1 };
	int32 XOffs[4] = { 0, -1, 1, 0 };
	for (int32 Yi = 0; Yi < Height; Yi++)
	{
		for (int32 Xi = 0; Xi < Width; Xi++)
		{
			int32 Pi = Xi + Yi * Width;
			if (PixCells[Pi] == -1) // outside cell
			{
				continue;
			}
			for (int Oi = 0; Oi < 4; Oi++)
			{
				int32 Yn = Yi + YOffs[Oi];
				int32 Xn = Xi + XOffs[Oi];
				int32 Pn = Xn + Yn * Width;
				if (Xn < 0 || Xn >= Width || Yn < 0 || Yn >= Height || PixCells[Pn] == -1) // outside nbr
				{
					continue;
				}
				
				MergeGroup(Pi, Pn);
			}
		}
	}
	// assign cell indices from compacted group IDs
	NumCells = 0;
	for (int32 Pi = 0; Pi < NumPix; Pi++)
	{
		if (PixCells[Pi] == -1)
		{
			continue;
		}
		int32 GroupID = FindGroup(Pi);
		if (PixCells[GroupID] == -2)
		{
			PixCells[GroupID] = NumCells++;
		}
		PixCells[Pi] = PixCells[GroupID];
	}

	// Dimensions of pixel corner data
	int32 CWidth = Width + 1;
	int32 CHeight = Height + 1;
	int32 NumCorners = CWidth * CHeight;
	TArray<int32> CornerIndices;
	CornerIndices.SetNumZeroed(NumCorners);

	TArray<TMap<int32, TArray<int32>>> PerCellBoundaryEdgeArrays;
	TArray<TArray<TArray<int32>>> CellBoundaryCorners;
	PerCellBoundaryEdgeArrays.SetNum(NumCells);
	CellBoundaryCorners.SetNum(NumCells);
	
	int32 COffX1[4] = { 1,0,1,0 };
	int32 COffX0[4] = { 0,0,1,1 };
	int32 COffY1[4] = { 0,0,1,1 };
	int32 COffY0[4] = { 0,1,0,1 };
	for (int32 Yi = 0; Yi < Height; Yi++)
	{
		for (int32 Xi = 0; Xi < Width; Xi++)
		{
			int32 Pi = Xi + Yi * Width;
			int32 Cell = PixCells[Pi];
			if (Cell == -1) // outside cell
			{
				continue;
			}
			for (int Oi = 0; Oi < 4; Oi++)
			{
				int32 Yn = Yi + YOffs[Oi];
				int32 Xn = Xi + XOffs[Oi];
				int32 Pn = Xn + Yn * Width;
				
				// boundary edge found
				if (Xn < 0 || Xn >= Width || Yn < 0 || Yn >= Height || PixCells[Pn] != PixCells[Pi])
				{
					int32 C0 = Xi + COffX0[Oi] + CWidth * (Yi + COffY0[Oi]);
					int32 C1 = Xi + COffX1[Oi] + CWidth * (Yi + COffY1[Oi]);
					TArray<int32> Chain = { C0, C1 };
					int32 Last;
					while (PerCellBoundaryEdgeArrays[Cell].Contains(Last = Chain.Last()))
					{
						Chain.Pop(false);
						Chain.Append(PerCellBoundaryEdgeArrays[Cell][Last]);
						PerCellBoundaryEdgeArrays[Cell].Remove(Last);
					}
					if (Last == C0)
					{
						CellBoundaryCorners[Cell].Add(Chain);
					}
					else
					{
						PerCellBoundaryEdgeArrays[Cell].Add(Chain[0], Chain);
					}
				}
			}
		}
	}

	FVector RegionDiagonal = Region.Max - Region.Min;

	for (int32 CellIdx = 0; CellIdx < NumCells; CellIdx++)
	{
		ensure(CellBoundaryCorners[CellIdx].Num() > 0); // there must not be any regions with no boundary
		ensure(PerCellBoundaryEdgeArrays[CellIdx].Num() == 0); // all boundary edge array should have been consumed and turned to full boundary loops
		ensureMsgf(CellBoundaryCorners[CellIdx].Num() == 1, TEXT("Have not implemented support for regions with holes!"));

		int32 BoundaryStart = PlaneBoundaryVertices.Num();
		const TArray<int32>& Bounds = CellBoundaryCorners[CellIdx][0];
		int32 Dx = 0, Dy = 0;
		auto CornerIdxToPos = [&](int32 CornerID)
		{
			int32 Xi = CornerID % CWidth;
			int32 Yi = CornerID / CWidth;
			return FVector2D(
				Region.Min.X + Xi * RegionDiagonal.X / float(Width),
				Region.Min.Y + Yi * RegionDiagonal.Y / float(Height)
			);
		};
		
		FVector2D LastP = CornerIdxToPos(Bounds[0]);
		int32 NumBoundVerts = 0;
		TArray<int32> FrontBound;
		for (int32 BoundIdx = 1; BoundIdx < Bounds.Num(); BoundIdx++)
		{
			FVector2D NextP = CornerIdxToPos(Bounds[BoundIdx]);
			FVector2D Dir = NextP - LastP;
			Dir.Normalize();
			int BoundSkip = BoundIdx;
			while (++BoundSkip < Bounds.Num())
			{
				FVector2D SkipP = CornerIdxToPos(Bounds[BoundSkip]);
				if (FVector2D::DotProduct(SkipP - NextP, Dir) < 1e-6)
				{
					break;
				}
				NextP = SkipP;
				BoundIdx = BoundSkip;
			}
			PlaneBoundaryVertices.Add(FVector(NextP.X, NextP.Y, Region.Min.Z));
			PlaneBoundaryVertices.Add(FVector(NextP.X, NextP.Y, Region.Max.Z));
			int32 Front = BoundaryStart + NumBoundVerts * 2;
			int32 Back = Front + 1;
			FrontBound.Add(Front);
			if (NumBoundVerts > 0)
			{
				AddPlane(FPlane(PlaneBoundaryVertices.Last(), FVector(Dir.Y, -Dir.X, 0)), CellIdx, -1, {Back, Front, Front - 2, Back - 2});
			}

			NumBoundVerts++;
			LastP = NextP;
		}

		// add the last edge, connecting the start and end
		FVector2D Dir = CornerIdxToPos(Bounds[1]) - LastP;
		Dir.Normalize();
		AddPlane(FPlane(PlaneBoundaryVertices.Last(), FVector(Dir.Y, -Dir.X, 0)), CellIdx, -1, {BoundaryStart+1, BoundaryStart, BoundaryStart+NumBoundVerts*2-2, BoundaryStart+NumBoundVerts*2-1});

		// add the front and back faces
		AddPlane(FPlane(Region.Min, FVector(0, 0, -1)), CellIdx, -1, FrontBound);
		TArray<int32> BackBound; BackBound.SetNum(FrontBound.Num());
		for (int32 Idx = 0, N = BackBound.Num(); Idx < N; Idx++)
		{
			BackBound[Idx] = FrontBound[N - 1 - Idx] + 1;
		}
		AddPlane(FPlane(Region.Max, FVector(0, 0, 1)), CellIdx, -1, BackBound);
	}


	AssumeConvexCells = false; // todo could set this to true if the 2D shape of each image region is convex

	CellFromPosition = TFunction<int32(FVector)>([=](FVector Position)
	{
		if (!Region.IsInsideOrOn(Position))
		{
			return -1;
		}
		
		FVector RelPos = Position - Region.Min;
		int32 Xg = Width * (RelPos.X / RegionDiagonal.X);
		int32 Yg = Height * (RelPos.Y / RegionDiagonal.Y);
		
		Xg = FMath::Clamp(Xg, 0, Width - 1);
		Yg = FMath::Clamp(Yg, 0, Height - 1);
		return PixCells[Xg + Yg * Width];
	});
}

void PLANARCUT_API DefaultVertexInterpolation(const FGeometryCollection& V0Collection, int32 V0, const FGeometryCollection& V1Collection, int32 V1, float T, int32 VOut, FGeometryCollection& Dest)
{
	// For now just manually write the interpolation for all default attributes
	Dest.Vertex[VOut] = FMath::Lerp(V0Collection.Vertex[V0], V1Collection.Vertex[V1], T);
	Dest.UV[VOut] = FMath::Lerp(V0Collection.UV[V0], V1Collection.UV[V1], T);
	Dest.Color[VOut] = FMath::Lerp(V0Collection.Color[V0], V1Collection.Color[V1], T);
	Dest.Normal[VOut] = FMath::Lerp(V0Collection.Normal[V0], V1Collection.Normal[V1], T).GetSafeNormal();
	FVector TangentU = FMath::Lerp(V0Collection.TangentU[V0], V1Collection.TangentU[V1], T);
	// don't lerp for TangentV, as it will be determined entirely by the lerp'd U and N
	Dest.TangentV[VOut] = (Dest.Normal[VOut] ^ TangentU).GetSafeNormal();
	Dest.TangentU[VOut] = (Dest.TangentV[VOut] ^ Dest.Normal[VOut]).GetSafeNormal();
	// bone map value does not actually matter here; we will overwrite it later when we copy vertices out to new geometry groups
	Dest.BoneMap[VOut] = V0Collection.BoneMap[V0];
}

void PLANARCUT_API ComputeTriangleNormals(
	const TArrayView<const FVector> Vertices,
	const TArrayView<const FIntVector> Triangles,
	TArray<FVector>& TriangleNormals
)
{
	TriangleNormals.SetNumUninitialized(Triangles.Num());
	// TODO: parallel for?
	for (int32 TriIdx = 0; TriIdx < Triangles.Num(); TriIdx++)
	{
		const FIntVector& Tri = Triangles[TriIdx];
		FVector Edge1 = Vertices[Tri.Y] - Vertices[Tri.X];
		FVector Edge2 = Vertices[Tri.Z] - Vertices[Tri.Y];
		FVector Normal = FVector::CrossProduct(Edge2, Edge1);
		Normal.Normalize();
		TriangleNormals[TriIdx] = Normal;
	}
}

// struct to store material info, e.g. to define what should go on to new faces along cut surfaces
struct FaceMaterialInfo
{
	int32 MaterialID;
	bool Visible;
};

// helper function to interpolate geometry collection data
int32 AddInterpolatedVertex(const FGeometryCollection& Source, int32 SourceVertexNum, int32 V0, int32 V1, float T, FGeometryCollection& Dest,
	TFunction<void(const FGeometryCollection&, int32, const FGeometryCollection&, int32, float, int32, FGeometryCollection&)> Interpolate)
{
	int32 AddedVertexIdx = Dest.AddElements(1, FGeometryCollection::VerticesGroup);

	const FGeometryCollection *V0Data = &Source;
	const FGeometryCollection *V1Data = &Source;
	if (V0 >= SourceVertexNum)
	{
		V0 -= SourceVertexNum;
		V0Data = &Dest;
	}
	if (V1 >= SourceVertexNum)
	{
		V1 -= SourceVertexNum;
		V1Data = &Dest;
	}

	Interpolate(*V0Data, V0, *V1Data, V1, T, AddedVertexIdx, Dest);

	return AddedVertexIdx;
}



// Output structure, stores one mesh per cell (including the "Outside of complex" cell, if needed)
// (currently for internal use; not exposed)
struct OutputCells
{
	FGeometryCollection AddedVerticesCollection;
	TArray<TArray<FIntVector>> CellTriangles;
	TArray<TArray<int32>> CellTriangleSources; // indices into the original GeometryCollection face arrays (for copying over face attrib data)
	TArray<TArray<int32>> CellVertexMapping; // indices into the original + added vertex arrays (for copying over vertex attrib data)
	TArray<TArray<int32>> NeighborCells; // indices of output cells that neighbor this cell
	int32 NoCellIdx = -1;

	/**
	 * Get index of the "outside" cell, for space that is classified as outside of all cells
	 * If that index does not exist yet, create it.
	 */
	int32 GetNoCellIdx()
	{
		if (NoCellIdx == -1)
		{
			NoCellIdx = CellTriangles.Num();
			CellTriangleSources.SetNum(NoCellIdx + 1);
			CellVertexMapping.SetNum(NoCellIdx + 1);
			CellTriangles.SetNum(NoCellIdx + 1);
			NeighborCells.SetNum(NoCellIdx + 1);
		}
		return NoCellIdx;
	}

	void ConnectCells(int32 CellA, int32 CellB)
	{
		if (INDEX_NONE == NeighborCells[CellA].Find(CellB))
		{
			NeighborCells[CellA].Add(CellB);
			NeighborCells[CellB].Add(CellA);
		}
		else
		{
			// cells should be symmetrically connected, so if the cell was already connected A->B, it should have been connected B->A
			ensure(INDEX_NONE != NeighborCells[CellB].Find(CellA));
		}
	}

	inline int32 OutputCellIdx(int32 CellID)
	{
		if (CellID < 0)
		{
			return GetNoCellIdx();
		}
		else
		{
			return CellID;
		}
	}

	int32 NumNonEmptyCells() const
	{
		int32 NonEmptyCells = 0;
		for (int32 CellIdx = 0; CellIdx < Num(); CellIdx++)
		{
			if (CellTriangles[CellIdx].Num() > 0)
			{
				NonEmptyCells++;
			}
		}
		return NonEmptyCells;
	}

	OutputCells(int32 NumCells)
	{
		CellTriangleSources.SetNum(NumCells);
		CellVertexMapping.SetNum(NumCells);
		CellTriangles.SetNum(NumCells);
		NeighborCells.SetNum(NumCells);
		NoCellIdx = -1;
	}
	
	int32 Num() const
	{
		check(CellVertexMapping.Num() == CellTriangles.Num());
		return CellTriangles.Num();
	}

	inline void AddTriangle(int32 CellIdx, int32 SourceTriangleIdx, FIntVector Triangle)
	{
		CellTriangles[CellIdx].Add(Triangle);
		CellTriangleSources[CellIdx].Add(SourceTriangleIdx);
	}


	int32 AddToGeometryCollection(FGeometryCollection& Source, const FInternalSurfaceMaterials &InternalMaterial, bool bIncludeOutsideCellInOutput, int32 SourceVertexNumWhenCut, int32 TransformParent = -1, int32 OverrideGlobalMaterialID = -1) const
	{
		int32 InternalMaterialID = OverrideGlobalMaterialID > -1 ? OverrideGlobalMaterialID : InternalMaterial.GlobalMaterialID;
		FGeometryCollection& Output = Source;  // keep separate variable names for Source and Output just in case we want to re-purpose this code later to add to a different collection

		bool bHasProximity = Source.HasAttribute("Proximity", FGeometryCollection::GeometryGroup);

		const FGeometryCollection& AddedVertices = AddedVerticesCollection;
		int32 NewGeometryStartIdx = Output.FaceStart.Num();
		int32 SourceVertexNum = Source.Vertex.Num();
		int32 SourceFaceNum = Source.Indices.Num();

		int32 TotalVerticesAdded = 0;
		int32 TotalFacesAdded = 0;
		int32 NumNewGeometries = NumNonEmptyCells();
		int NumCellsToDump = Num();
		// if we have a no-cell index set, and we're not including the outside geom, then skip that cell
		if (!bIncludeOutsideCellInOutput && NoCellIdx > -1)
		{
			NumCellsToDump--;
			ensure(NoCellIdx == NumCellsToDump); // by convention the index of the "no cell" geometry is the last index
			if (CellTriangles[NoCellIdx].Num())
			{
				NumNewGeometries--;
			}
		}

		int32 GeometryStart = Output.AddElements(NumNewGeometries, FGeometryCollection::GeometryGroup);
		int32 TransformsStart = Output.AddElements(NumNewGeometries, FGeometryCollection::TransformGroup);
		int32 GeometrySubIdx = 0;

		TArray<int32> CellIdxToGeometryIdxMap;
		if (bHasProximity)
		{
			CellIdxToGeometryIdxMap.SetNum(Num());
			for (int32 Idx = 0, N = Num(); Idx < N; Idx++)
			{
				CellIdxToGeometryIdxMap[Idx] = -1;
			}
		}

		TArray<FTranslationMatrix> ChildInverseTransforms;
		for (int32 OutputCellIdx = 0; OutputCellIdx < NumCellsToDump; OutputCellIdx++)
		{
			int32 NumTriangles = CellTriangles[OutputCellIdx].Num();
			int32 NumVertices = CellVertexMapping[OutputCellIdx].Num();
			if (NumTriangles > 0)
			{
				int32 GeometryIdx = GeometryStart + GeometrySubIdx;
				Output.FaceCount[GeometryIdx] = NumTriangles;
				Output.FaceStart[GeometryIdx] = SourceFaceNum + TotalFacesAdded;
				Output.VertexStart[GeometryIdx] = SourceVertexNum + TotalVerticesAdded;
				Output.VertexCount[GeometryIdx] = NumVertices;
				int32 TransformIdx = TransformsStart + GeometrySubIdx;
				Output.TransformIndex[GeometryIdx] = TransformIdx;
				Output.TransformToGeometryIndex[TransformIdx] = GeometryIdx;
				if (TransformParent > -1)
				{
					// TODO: this is probably not the best way to build the bone name string?
					Output.BoneName[TransformIdx] = Output.BoneName[TransformParent] + "_" + FString::FromInt(GeometrySubIdx);
					Output.BoneColor[TransformIdx] = Output.BoneColor[TransformParent];
					Output.Parent[TransformIdx] = TransformParent;
					Output.Children[TransformParent].Add(TransformIdx);
				}


				// Set the transform for the child geometry
				// Note to make it easier to procedurally texture later, it's better to leave it in the same space
				Output.Transform[TransformIdx] = FTransform::Identity;
				ChildInverseTransforms.Emplace(FVector::ZeroVector);

				GeometrySubIdx++;
			}
			TotalVerticesAdded += CellVertexMapping[OutputCellIdx].Num();
			TotalFacesAdded += CellTriangles[OutputCellIdx].Num();
		}
		int32 VerticesStart = Output.AddElements(TotalVerticesAdded, FGeometryCollection::VerticesGroup);
		int32 FacesStart = Output.AddElements(TotalFacesAdded, FGeometryCollection::FacesGroup);

		int32 VertexGroupStart = VerticesStart;
		int32 FaceGroupStart = FacesStart;
		GeometrySubIdx = 0;
		for (int32 OutputCellIdx = 0; OutputCellIdx < NumCellsToDump; OutputCellIdx++)
		{
			int32 NumTriangles = CellTriangles[OutputCellIdx].Num();
			int32 NumVertices = CellVertexMapping[OutputCellIdx].Num();
			int32 GeometryIdx = GeometryStart + GeometrySubIdx;
			if (bHasProximity)
			{
				CellIdxToGeometryIdxMap[OutputCellIdx] = GeometryIdx;
			}
			FTranslationMatrix ToLocal(FVector(0));
			if (NumTriangles > 0)
			{
				ToLocal = ChildInverseTransforms[GeometrySubIdx];
				GeometrySubIdx++;
			}
			for (int32 VertexSubIdx = 0; VertexSubIdx < NumVertices; VertexSubIdx++)
			{
				int32 CopyVertexIdx = CellVertexMapping[OutputCellIdx][VertexSubIdx];
				const FGeometryCollection* CopyFromCollection = &Source;
				if (CopyVertexIdx >= SourceVertexNumWhenCut)
				{
					CopyFromCollection = &AddedVertices;
					CopyVertexIdx -= SourceVertexNumWhenCut;
				}
				int32 CopyToIdx = VertexGroupStart + VertexSubIdx;
				// TODO: add a function to GeometryCollection api to copy all properties of an element automatically, rather than hard-coding this?
				Output.Vertex[CopyToIdx]		= ToLocal.TransformPosition(CopyFromCollection->Vertex[CopyVertexIdx]);
				Output.Normal[CopyToIdx]        = ToLocal.TransformVector(CopyFromCollection->Normal[CopyVertexIdx]);
				Output.UV[CopyToIdx]			= CopyFromCollection->UV[CopyVertexIdx];
				Output.TangentU[CopyToIdx]		= ToLocal.TransformVector(CopyFromCollection->TangentU[CopyVertexIdx]);
				Output.TangentV[CopyToIdx]		= ToLocal.TransformVector(CopyFromCollection->TangentV[CopyVertexIdx]);
				Output.Color[CopyToIdx]			= CopyFromCollection->Color[CopyVertexIdx];

				// Bone map should actually be set based on the transform of the new geometry, not copied from the old vertex
				Output.BoneMap[CopyToIdx] = Output.TransformIndex[GeometryIdx];
			}
			for (int32 FaceSubIdx = 0; FaceSubIdx < CellTriangles[OutputCellIdx].Num(); FaceSubIdx++)
			{
				int32 CopyToIdx = FaceGroupStart + FaceSubIdx;
				int32 SourceIdx = CellTriangleSources[OutputCellIdx][FaceSubIdx];
				if (SourceIdx > -1) // we know the source face; copy information from there
				{
					// TODO: add a function to GeometryCollection api to copy all properties of an element automatically, rather than hard-coding this?
					Output.Visible[CopyToIdx] = Source.Visible[SourceIdx];
					// Note: I don't worry about Output.MaterialIndex[CopyToIdx], this will need to be rebuilt regardless later.
					Output.MaterialID[CopyToIdx] = Source.MaterialID[SourceIdx];
				}
				else
				{
					Output.Visible[CopyToIdx] = InternalMaterial.bGlobalVisibility;
					// Note: I don't worry about Output.MaterialIndex[CopyToIdx], this will need to be rebuilt regardless later.
					Output.MaterialID[CopyToIdx] = InternalMaterialID;
				}

				// Face indices are the one property that isn't just copied over blindly; put the correct value *after* filling the rest of the data, in case we want to change the above to blindly copy all attributes
				Output.Indices[CopyToIdx] = CellTriangles[OutputCellIdx][FaceSubIdx] + FIntVector(VertexGroupStart);
			}
			VertexGroupStart += NumVertices;
			FaceGroupStart += CellTriangles[OutputCellIdx].Num();
		}

		if (bHasProximity)
		{
			TManagedArray<TSet<int32>>& Proximity = Source.GetAttribute<TSet<int32>>("Proximity", FGeometryCollection::GeometryGroup);

			for (int32 OutputCellIdx = 0; OutputCellIdx < NumCellsToDump; OutputCellIdx++)
			{
				int32 GeomAIdx = CellIdxToGeometryIdxMap[OutputCellIdx];
				if (GeomAIdx == -1)
				{
					continue;
				}
				for (int32 ConnectedCellIdx : NeighborCells[OutputCellIdx])
				{
					int32 GeomBIdx = CellIdxToGeometryIdxMap[ConnectedCellIdx];
					if (GeomBIdx == -1)
					{
						continue;
					}
					Proximity[GeomAIdx].Add(GeomBIdx);
					// TODO: make the NeighborCells thing be one-sided and do symmetric adds here
				}
			}
		}

		if (Source.BoundingBox.Num())
		{
			// Initialize BoundingBox
			for (int32 Idx = GeometryStart; Idx < Source.BoundingBox.Num(); ++Idx)
			{
				Source.BoundingBox[Idx].Init();
			}

			// Compute BoundingBox
			for (int32 Idx = SourceVertexNum; Idx < Source.Vertex.Num(); ++Idx)
			{
				int32 TransformIndexValue = Source.BoneMap[Idx];
				Source.BoundingBox[Source.TransformToGeometryIndex[TransformIndexValue]] += Source.Vertex[Idx];
			}
		}

		return NewGeometryStartIdx;
	}
};

/**
* Transform local geometry, updating the corresponding transform so the shape itself is not changed
*/
void TransformLocalGeometry(FGeometryCollection& Source, int32 TransformIdx, const FTransform& Transform, const FTransform& InverseTransform)
{
	int GeometryIdx = Source.TransformToGeometryIndex[TransformIdx];
	// recompute bounds (rather than directly transforming, so it remains a tight bound even if transform includes rotation)
	FBox Bounds; Bounds.Init();
	for (int32 VertIdx = Source.VertexStart[GeometryIdx], VertEnd = Source.VertexCount[GeometryIdx] + Source.VertexStart[GeometryIdx]; VertIdx < VertEnd; VertIdx++)
	{
		FVector Pos = Transform.TransformPosition(Source.Vertex[VertIdx]);
		Bounds += Pos;
		Source.Vertex[VertIdx] = Pos;
	}
	Source.BoundingBox[GeometryIdx] = Bounds;
	Source.Transform[TransformIdx] = InverseTransform * Source.Transform[TransformIdx];
}

/** 
 * Update a given transform w/ geometry to have vertices locally centered at the origin and positions not exceeding a -1,1 range, changing the transform appropriately so that the shape itself is not changed
 */
FTransform CenterAndScaleLocalGeometry(FGeometryCollection& Source, int32 TransformIdx)
{
	int GeometryIdx = Source.TransformToGeometryIndex[TransformIdx];
	if (!ensure(GeometryIdx != INDEX_NONE)) // transform had no geometry
	{
		return FTransform::Identity;
	}

	FBox GeomBox; GeomBox.Init();
	if (Source.BoundingBox.Num())
	{
		GeomBox = Source.BoundingBox[GeometryIdx];
	}
	if (!GeomBox.IsValid)
	{
		for (int32 VertIdx = Source.VertexStart[GeometryIdx], VertEnd = Source.VertexCount[GeometryIdx] + Source.VertexStart[GeometryIdx]; VertIdx < VertEnd; VertIdx++)
		{
			GeomBox += Source.Vertex[VertIdx];
		}
	}

	if (!ensure(GeomBox.IsValid)) // transform had corresponding geometry index but it had zero vertices?
	{
		return FTransform::Identity;
	}
	
	FVector Center, Extents;
	GeomBox.GetCenterAndExtents(Center, Extents);
	float MaxExtent = Extents.GetMax();
	float InvScaleFactor = MaxExtent < 1 ? 1 : MaxExtent;
	float ScaleFactor = 1.0f / InvScaleFactor;
	FTransform CenterAndFit, InverseCenterAndFit;
	CenterAndFit.SetTranslationAndScale3D(-Center*ScaleFactor, FVector(ScaleFactor, ScaleFactor, ScaleFactor));
	InverseCenterAndFit.SetTranslationAndScale3D(Center, FVector(InvScaleFactor, InvScaleFactor, InvScaleFactor));

	TransformLocalGeometry(Source, TransformIdx, CenterAndFit, InverseCenterAndFit);

	return InverseCenterAndFit;
}



//// useful for debugging: code to dump a single geometry to obj
//#include <fstream>
//#include <string>
//
//void WriteGeom(const FString& Path, FGeometryCollection &Collection, int32 GeometryIdx)
//{
//	std::ofstream f(std::string(TCHAR_TO_UTF8(*Path)));
//	for (FVector &V : Collection.Vertex)
//	{
//		f << "v " << V.X << " " << V.Y << " " << V.Z << std::endl;
//	}
//
//	int32 Start = Collection.FaceStart[GeometryIdx];
//	for (int32 i = Start; i < Start + Collection.FaceCount[GeometryIdx]; i++)
//	{
//		FIntVector Tri = Collection.Indices[i] + FIntVector(1);
//		f << "f " << Tri.X << " " << Tri.Y << " " << Tri.Z << std::endl;
//	}
//	f.close();
//}
//
//void WritePlanarCells(const FString& Path, FPlanarCells& Cells, const TArrayView<const FVector>& PlaneBoundaryVertices)
//{
//	std::ofstream f(std::string(TCHAR_TO_UTF8(*Path)));
//	for (const FVector &V : PlaneBoundaryVertices)
//	{
//		f << "v " << V.X << " " << V.Y << " " << V.Z << std::endl;
//	}
//
//	
//	for (int32 i = 0; i < Cells.PlaneBoundaries.Num(); i++)
//	{
//		TArray<int32>& Inds = Cells.PlaneBoundaries[i];
//		for (int32 j = 1; j + 1 < Inds.Num(); j++)
//		{
//			f << "f " << Inds[0] + 1 << " " << Inds[j] + 1 << " " << Inds[j + 1] + 1 << std::endl;
//		}
//	}
//	f.close();
//}

/**
 * Helper version of CutWithPlanarCells; exposes more parameters for later experimentation
 * Cut a (subset of a) GeometryCollection with PlanarCells, and add each cut cell to an OutputCells struct
 */
void CutWithPlanarCellsHelper(
	const FPlanarCells &Cells,
	const TArrayView<const FPlane> TransformedPlanes,
	const TArrayView<const FVector> TransformedPlaneBoundaryVertices,
	const FTransform& LocalSpaceToPlanarCellSpace,
	const FGeometryCollection& Source,
	int32 GeometryIdx,
	int32 TriangleStart,
	int32 NumTriangles,
	const TArrayView<const FVector> TriangleNormals,
	double PlaneEps,
	float CheckDistanceAcrossOutsideCellForProximity,
	TFunction<void(const FGeometryCollection&, int32, const FGeometryCollection&, int32, float, int32, FGeometryCollection&)> Interpolate,
	OutputCells &Output,
	const FInternalSurfaceMaterials *InternalMaterials = nullptr, // optional override for cells' internal materials
	TMeshAABBTree3<FGeometryCollectionMeshAdapter> *PrecomputedAABBTree = nullptr // optionally pass in an already-computed AABB tree
)
{
	if (!InternalMaterials)
	{
		InternalMaterials = &Cells.InternalSurfaceMaterials;
	}
	// shorthand accessor for vertices in the source geometry collection
	const TArrayView<const FVector> Vertices(Source.Vertex.GetData(), Source.Vertex.Num());
	const TArrayView<const FIntVector> Triangles(Source.Indices.GetData() + TriangleStart, NumTriangles);

	// consider trade-offs between cases where we could have a more consistent mesh vs having simpler processing / fewer triangles
	constexpr bool bCareAboutTJunctionsEvenALittleBit = false;
	bool bNoiseOnPlane = InternalMaterials->NoiseSettings.IsSet();

	// extract an average scale for this transform to support properly spacing noise points, if requested
	float AverageGlobalScale = 1;
	if (bNoiseOnPlane)
	{
		FTransform LocalToGlobalTransform = GeometryCollectionAlgo::GlobalMatrix(Source.Transform, Source.Parent, Source.TransformIndex[GeometryIdx]);
		FVector Scales = LocalToGlobalTransform.GetScale3D();
		AverageGlobalScale = FMath::Max(KINDA_SMALL_NUMBER, FVector::DotProduct(Scales.GetAbs(), FVector(1. / 3.)));
	}
	float AverageGlobalScaleInv = 1.0 / AverageGlobalScale;

	struct PlaneFrame
	{
		FVector3d Origin, X, Y;

		PlaneFrame(FVector3d Origin, FVector3d Normal) : Origin(Origin)
		{
			VectorUtil::MakePerpVectors(Normal, X, Y);
		}

		inline FVector2d Project(const FVector3d& Pt)
		{
			FVector3d RelPt = Pt - Origin;
			return FVector2d(RelPt.Dot(X), RelPt.Dot(Y));
		}

		inline FVector3d UnProject(const FVector2d& Pt)
		{
			return Origin + X * Pt.X + Y * Pt.Y;
		}
	};

	TArray<PlaneFrame> PlaneFrames;
	PlaneFrames.Reserve(Cells.Planes.Num());
	for (int32 PlaneIdx = 0; PlaneIdx < Cells.Planes.Num(); PlaneIdx++)
	{
		const FPlane& Plane = TransformedPlanes[PlaneIdx];
		FVector3d Normal(Plane.X, Plane.Y, Plane.Z);
		const TArray<int32>& Boundary = Cells.PlaneBoundaries[PlaneIdx];
		int32 NumBoundary = Boundary.Num();
		if (NumBoundary)
		{
			PlaneFrames.Emplace(TransformedPlaneBoundaryVertices[Boundary[0]], Normal);
		}
		else
		{
			PlaneFrames.Emplace(Plane.W * Normal, Normal);
		}
	}

	auto IsProjectionInsideBoundary = [&](const FVector& Pt, int32 PlaneIdx)
	{
		const TArray<int32>& Boundary = Cells.PlaneBoundaries[PlaneIdx];
		int32 NumBoundary = Boundary.Num();
		if (!NumBoundary) // unbounded plane case
		{
			return true;
		}
		TArray<FVector2d> ProjV;
		ProjV.Reserve(NumBoundary);
		for (int32 VIdx : Boundary)
		{
			ProjV.Add(PlaneFrames[PlaneIdx].Project(TransformedPlaneBoundaryVertices[VIdx]));
		}
		FPolygon2d Polygon(ProjV);
		return Polygon.Contains(PlaneFrames[PlaneIdx].Project(Pt));
	};

	FGeometryCollectionMeshAdapter Adapter{ &Source, GeometryIdx };
	TMeshAABBTree3<FGeometryCollectionMeshAdapter> LocalAABBTree;
	TMeshAABBTree3<FGeometryCollectionMeshAdapter> *AABBTree = PrecomputedAABBTree;
	if (!PrecomputedAABBTree)
	{
		LocalAABBTree.SetMesh(&Adapter);
		AABBTree = &LocalAABBTree;
	}
	check(AABBTree->GetMesh()->Collection == &Source && AABBTree->GetMesh()->GeometryIdx == GeometryIdx); // verify that the AABBTree is looking at the correct geometry
	TFastWindingTree<FGeometryCollectionMeshAdapter> FastWindingTree(AABBTree);

	int32 InputVertexCount = Vertices.Num();
	int32 NumPlanes = TransformedPlanes.Num();

	TArray<TArray<int32>> PlanesThroughTriangle, PlanesOnTriangle; // PlanesThroughTriangle[TriIdx] -> All PlaneIdx crossing triangle
	PlanesThroughTriangle.SetNum(Triangles.Num());
	PlanesOnTriangle.SetNum(Triangles.Num());

	// Mappings from PlaneIdx -> Elements (triangles, edges) on that plane
	TArray<TArray<int32>> TrianglesOnPlane; // note: We will fill this later, using cut-down final triangles, so that no triangle ends up on two planes
	TrianglesOnPlane.SetNum(NumPlanes);
	TArray<TArray<TPair<FVector, FVector>>> EdgesOnPlane;
	EdgesOnPlane.SetNum(NumPlanes);

	// ~~~ PHASE 1: FIND COLLISIONS BETWEEN ALL PLANAR FACETS AND TRIANGLES ~~~	

	// Obstacles to making this parallel: PlanesThroughTriangle, PlanesOnTriangle can both be edited by multiple planes.  If we make a per-triangle lock, could be fine!
	for (int32 PlaneIdx = 0; PlaneIdx < NumPlanes; PlaneIdx++)
	{
		const FPlane &Plane = TransformedPlanes[PlaneIdx];
		FBox BoundingBox(EForceInit::ForceInit);
		if (Cells.PlaneBoundaries[PlaneIdx].Num())
		{
			for (int32 PlaneBoundaryVertexIdx : Cells.PlaneBoundaries[PlaneIdx])
			{
				BoundingBox += TransformedPlaneBoundaryVertices[PlaneBoundaryVertexIdx];
			}
		}

		FAxisAlignedBox3d PlaneFacetBoxPlusEps(BoundingBox);
		PlaneFacetBoxPlusEps.Max = PlaneFacetBoxPlusEps.Max + PlaneEps;
		PlaneFacetBoxPlusEps.Min = PlaneFacetBoxPlusEps.Min - PlaneEps;
		TMeshAABBTree3<FGeometryCollectionMeshAdapter>::FTreeTraversal TraverseNearPlane;
		TraverseNearPlane.NextBoxF = [&](const FAxisAlignedBox3d& Box, int Depth)
		{
			if (BoundingBox.IsValid && !Box.Intersects(PlaneFacetBoxPlusEps))
			{
				return false;
			}
			int sides[3] = { 0,0,0 };
			sides[PlaneSide(Plane, (FVector)Box.Min) + 1]++;
			sides[PlaneSide(Plane, (FVector)Box.Max) + 1]++;
			sides[PlaneSide(Plane, FVector(Box.Max.X, Box.Min.Y, Box.Min.Z)) + 1]++;
			sides[PlaneSide(Plane, FVector(Box.Min.X, Box.Max.Y, Box.Min.Z)) + 1]++;
			sides[PlaneSide(Plane, FVector(Box.Max.X, Box.Max.Y, Box.Min.Z)) + 1]++;
			sides[PlaneSide(Plane, FVector(Box.Min.X, Box.Min.Y, Box.Max.Z)) + 1]++;
			sides[PlaneSide(Plane, FVector(Box.Max.X, Box.Min.Y, Box.Max.Z)) + 1]++;
			sides[PlaneSide(Plane, FVector(Box.Min.X, Box.Max.Y, Box.Max.Z)) + 1]++;
			// we cross box if any vertex 'on' plane or vertices on both sides
			return sides[1] || (sides[0] && sides[2]);
		};
		//for (int32 TriIdx = 0; TriIdx < NumTriangles; TriIdx++)
		TraverseNearPlane.NextTriangleF = [&](int TriIdxInt) 
		{
			int32 TriIdx = TriIdxInt; // cast to int32 up top, because we use this a lot as a TArray index

			const FIntVector &Tri = Triangles[TriIdx];

			// TODO: further filter out cases where triangle doesn't intersect planar facet

			double SX = PlaneDotDouble(Plane,Vertices[Tri.X]), SY = PlaneDotDouble(Plane, Vertices[Tri.Y]), SZ = PlaneDotDouble(Plane, Vertices[Tri.Z]);
			int32 SXSide = SX < -PlaneEps ? -1 : SX > PlaneEps ? 1 : 0;
			int32 SYSide = SY < -PlaneEps ? -1 : SY > PlaneEps ? 1 : 0;
			int32 SZSide = SZ < -PlaneEps ? -1 : SZ > PlaneEps ? 1 : 0;
			if (SXSide * SYSide == -1 || SYSide * SZSide == -1 || SZSide * SXSide == -1) // triangle crosses plane
			{
				PlanesThroughTriangle[TriIdx].Add(PlaneIdx);
				int32 CrossIdx = 0;
				FVector CrossPosns[2];
				

				auto AddCross = [&](float SDA, float SDB, int32 ASide, int32 BSide, int32 AIdx, int32 BIdx, int32& CrossIdxRef)
				{
					if (CrossIdxRef < 2 && ASide*BSide == -1)
					{
						double T = SDA / (SDA - SDB);
						CrossPosns[CrossIdxRef] = (1 - T) * Vertices[AIdx] + T * Vertices[BIdx];
						CrossIdxRef++;
					}
				};
				AddCross(SX, SY, SXSide, SYSide, Tri.X, Tri.Y, CrossIdx);
				AddCross(SY, SZ, SYSide, SZSide, Tri.Y, Tri.Z, CrossIdx);
				AddCross(SZ, SX, SZSide, SXSide, Tri.Z, Tri.X, CrossIdx);
				if (CrossIdx == 1) // One of the vertices is on the plane
				{
					int32 MinSDIdx = Tri.X;
					float MinSDAbs = FMath::Abs(SX);
					float SYAbs = FMath::Abs(SY);
					if (SYAbs < MinSDAbs)
					{
						MinSDIdx = Tri.Y;
						MinSDAbs = SYAbs;
					}
					float SZAbs = FMath::Abs(SZ);
					if (SZAbs < MinSDAbs)
					{
						MinSDIdx = Tri.Z;
						MinSDAbs = SZAbs;
					}
					CrossPosns[1] = Vertices[MinSDIdx];
					CrossIdx++;
				}
				ensure(CrossIdx == 2);
				EdgesOnPlane[PlaneIdx].Add(TPair<FVector, FVector>(CrossPosns[0], CrossPosns[1]));
			}
			else {
				int32 OnPlaneCount =
					(FMath::Abs(SX) < PlaneEps ? 1 : 0) +
					(FMath::Abs(SY) < PlaneEps ? 1 : 0) +
					(FMath::Abs(SZ) < PlaneEps ? 1 : 0);
				if (OnPlaneCount == 3)
				{
					PlanesOnTriangle[TriIdx].Add(PlaneIdx);
				}
				else if (OnPlaneCount == 2)
				{
					int32 OnIdx = 0;
					FVector OnPosns[2];

					auto AddOn = [&Vertices, &PlaneEps, &OnPosns](float SD, int32 VIdx, int32 &OnIdxRef)
					{
						if (OnIdxRef < 2 && FMath::Abs(SD) < PlaneEps)
						{
							OnPosns[OnIdxRef] = Vertices[VIdx];
							OnIdxRef++;
						}
					};
					AddOn(SX, Tri.X, OnIdx);
					AddOn(SY, Tri.Y, OnIdx);
					AddOn(SZ, Tri.Z, OnIdx);
					ensure(OnIdx == 2);
					EdgesOnPlane[PlaneIdx].Add(TPair<FVector, FVector>(OnPosns[0], OnPosns[1]));
				}
			}
		};
		AABBTree->DoTraversal(TraverseNearPlane);
	}

	// ~~~ PHASE 2: CUT ALL TRIANGLES THAT CROSS PLANAR FACETS ~~~	
	// Obstacles to making this parallel: CompletedEdgeSplits and AddedVertices are both read and edited from multiple triangles; 
	//										CellFromPosition for FVoronoiDiagram is not thread safe
	TMap<TPair<int32, int32>, int32> CompletedEdgeSplits; // for storing edge splits after they have already happened
	int32 OrigTriNum = Triangles.Num();
	
	check(Output.Num() == Cells.NumCells);
	FGeometryCollection& AddedVerticesCollection = Output.AddedVerticesCollection;
	const TManagedArray<FVector> &AddedVertices = AddedVerticesCollection.Vertex;  // shorthand reference, only used for accesses

	auto VertexPos = [&](int32 VertexIdx)
	{
		return VertexIdx < InputVertexCount ? Vertices[VertexIdx] : AddedVertices[VertexIdx - InputVertexCount];
	};
	auto Centroid = [&](const FIntVector &Tri)
	{
		return (VertexPos(Tri.X) + VertexPos(Tri.Y) + VertexPos(Tri.Z)) * (1.0 / 3.0);
	};
	for (int32 OrigTriIdx = 0; OrigTriIdx < OrigTriNum; OrigTriIdx++)
	{
		const FIntVector &OrigTri = Triangles[OrigTriIdx];
		const FVector& TriNormal = TriangleNormals[OrigTriIdx];
		FPlane TriPlane(TriNormal, FVector::DotProduct(TriNormal, Vertices[OrigTri.X]));

		// TODO: consider a mode where we just handle every original triangle totally independently, with the caveat that we will duplicate vertices for every edge split w/ multiple triangles on the edge(!?)
		if (!bCareAboutTJunctionsEvenALittleBit)
		{
			CompletedEdgeSplits.Empty();
		}
		TMap<TPair<int32, int32>, int32> InternalEdgeSplits; // Edge splits that are entirely inside the triangle don't need to be added to global map
		TArray<FIntVector> AddedTriangles;  // Array of all triangles that will be added to cells based on this triangle
		AddedTriangles.Add(OrigTri);		//	(initially just the original triangle)

		// If there are any cutting planes, split up the triangle as needed
		if (PlanesThroughTriangle[OrigTriIdx].Num() > 0)
		{
			for (int32 PlaneIdx : PlanesThroughTriangle[OrigTriIdx]) // fully cut one plane at a time
			{
				const FPlane& Plane = TransformedPlanes[PlaneIdx];
				const FVector PlaneNormal(Plane.X, Plane.Y, Plane.Z);
				FVector IntersectionDirection = TriNormal ^ PlaneNormal;
				bool bFoundIntersectionLine = IntersectionDirection.Normalize(); // this should always succeed for anything in a PlanesThroughTriangle array ...

				const TArray<int32>& PlaneBoundary = Cells.PlaneBoundaries[PlaneIdx];

				// compute the extent of the planar facet on the line where it intersects the plane of the triangle.  used below to skip sub-triangles that don't touch the facet.
				FInterval1d PlaneFacetInterval = FInterval1d::Empty();
				int32 PlaneBoundaryNum = PlaneBoundary.Num();
				if (bFoundIntersectionLine && PlaneBoundaryNum)
				{
					for (int32 Idx = 0, LastIdx = PlaneBoundaryNum - 1; Idx < PlaneBoundaryNum; LastIdx = Idx++)
					{
						double T;
						const FVector& A = TransformedPlaneBoundaryVertices[PlaneBoundary[Idx]];
						const FVector& B = TransformedPlaneBoundaryVertices[PlaneBoundary[LastIdx]];
						IsSegmentCrossing(TriPlane, A, B, T);
						if (T > -PlaneEps && T < 1+PlaneEps) {
							PlaneFacetInterval.Contain(FVector::DotProduct(IntersectionDirection, FMath::Lerp(A, B, T)));
						}
					}

					// grow interval by a tolerance
					PlaneFacetInterval.Min -= PlaneEps;
					PlaneFacetInterval.Max += PlaneEps;
				}

				// all added triangles need processing w/ the new possible cutting plane
				TArray<int32> TriProcessQueue;
				for (int32 ToProcessIdx = 0; ToProcessIdx < AddedTriangles.Num(); ToProcessIdx++)
				{
					TriProcessQueue.Add(ToProcessIdx);
				}

				TSet<int32> BoundaryVertices;
				BoundaryVertices.Add(OrigTri.X);
				BoundaryVertices.Add(OrigTri.Y);
				BoundaryVertices.Add(OrigTri.Z);

				auto DoEdgeSplit = [&](int32 V0, int32 V1, int32 VOff, int32 CurTriIdx, int32 ConsiderPlaneIdx)
				{
					int32 VSmall = V0, VBig = V1;
					if (VSmall > VBig)
					{
						Swap(VSmall, VBig);
					}
					TPair<int32, int32> Edge(VSmall, VBig);

					bool BoundaryEdge = BoundaryVertices.Contains(V0) && BoundaryVertices.Contains(V1);

					auto MakeTwoTriangles = [&](int32 SplitIdx)
					{
						AddedTriangles[CurTriIdx] = FIntVector(V0, SplitIdx, VOff);
						int32 NewTriIdx = AddedTriangles.Emplace(SplitIdx, V1, VOff);
						TriProcessQueue.Add(CurTriIdx);
						TriProcessQueue.Add(NewTriIdx);
					};

					// check if this is a known split edge on boundary or internally
					// TODO: do we even want to do this?  can we disable / remove this stuff if we don't care about making T Junctions?  (the other code will make a ton of them anyway)
					// Partial answer: If we don't do this, we will repeat the edge split for every triangle on that edge, duplicating the vertex and disjoining the topology even if it's not a T Junction?
					/*if (bCareAboutTJunctionsEvenALittleBit)
					{*/
						int32 *FoundBorder = BoundaryEdge ? CompletedEdgeSplits.Find(Edge) : nullptr;
						if (FoundBorder)
						{
							BoundaryVertices.Add(*FoundBorder);
							MakeTwoTriangles(*FoundBorder);
							return true;
						}
						int32 *FoundInternal = !BoundaryEdge ? InternalEdgeSplits.Find(Edge) : nullptr;
						if (FoundInternal)
						{
							MakeTwoTriangles(*FoundInternal);
							return true;
						}
					//}

					{
						double t;
						FVector P0 = VertexPos(V0), P1 = VertexPos(V1);
						if (IsSegmentCrossing(TransformedPlanes[ConsiderPlaneIdx], P0, P1, t))
						{
							int32 SplitVert = InputVertexCount + AddInterpolatedVertex(Source, Vertices.Num(), V0, V1, t, AddedVerticesCollection, Interpolate);
							if (BoundaryEdge)
							{
								BoundaryVertices.Add(SplitVert);
								CompletedEdgeSplits.Add(Edge, SplitVert);
							}
							else
							{
								InternalEdgeSplits.Add(Edge, SplitVert);
							}
							MakeTwoTriangles(SplitVert);
							return true;
						}
					}

					return false;
				};

				while (TriProcessQueue.Num())
				{
					int32 TriToSplitIdx = TriProcessQueue.Pop();
					const FIntVector& Tri = AddedTriangles[TriToSplitIdx];
					if (bFoundIntersectionLine && PlaneBoundaryNum > 0)
					{
						// check if the triangle overlaps with the the planar facet; if no overlap, no need to split
						FInterval1d TriInterval = FInterval1d::Empty();
						FVector A = VertexPos(Tri.X), B = VertexPos(Tri.Y), C = VertexPos(Tri.Z);
						auto ContainCrossing = [&Plane, &PlaneEps, &IntersectionDirection](FInterval1d& Inter, const FVector& P0, const FVector& P1)
						{
							double T;
							IsSegmentCrossing(Plane, P0, P1, T);
							if (T > -PlaneEps && T < 1 + PlaneEps)
							{
								Inter.Contain(FVector::DotProduct(IntersectionDirection, FMath::Lerp(P0, P1, T)));
							}
						};
						ContainCrossing(TriInterval, A, B);
						ContainCrossing(TriInterval, B, C);
						ContainCrossing(TriInterval, C, A);
						if (!TriInterval.Overlaps(PlaneFacetInterval))
						{
							continue;
						}
					}
					DoEdgeSplit(Tri.X, Tri.Y, Tri.Z, TriToSplitIdx, PlaneIdx)
						|| DoEdgeSplit(Tri.Y, Tri.Z, Tri.X, TriToSplitIdx, PlaneIdx)
						|| DoEdgeSplit(Tri.Z, Tri.X, Tri.Y, TriToSplitIdx, PlaneIdx);
				}
			}
		}

		const FVector &TriangleNormal = TriangleNormals[OrigTriIdx];
		for (const FIntVector& Tri : AddedTriangles)
		{
			FVector TriPos = Centroid(Tri);
			int32 Cell = Cells.CellFromPosition(LocalSpaceToPlanarCellSpace.TransformPosition(TriPos));

			// triangles that were coplanar with a cutting plane can be re-assigned to the neighboring cell based on their normal
			int32 OwnedByPlane = -1;
			for (int32 PlaneIdx : PlanesOnTriangle[OrigTriIdx])
			{
				TPair<int32, int32> PlaneCells = Cells.PlaneCells[PlaneIdx];
				if ((PlaneCells.Key == Cell || PlaneCells.Value == Cell) && IsProjectionInsideBoundary(TriPos, PlaneIdx))
				{
					OwnedByPlane = PlaneIdx;
					// TODO: note if the original triangle was degenerate then this will be arbitrary, but not sure we care about that case!
					FVector PlaneNormal(TransformedPlanes[PlaneIdx].X, TransformedPlanes[PlaneIdx].Y, TransformedPlanes[PlaneIdx].Z);
					Cell = FVector::DotProduct(TriangleNormal, PlaneNormal) > 0 ? PlaneCells.Key : PlaneCells.Value;
					break;
				}
			}
			// Store plane ownership decision for later use by triangulation algo
			if (OwnedByPlane > -1)
			{
				TrianglesOnPlane[OwnedByPlane].AddUnique(OrigTriIdx); // Another trouble spot if we want to parallel-for this whole section per triangle
			}
			if (Cell < 0)
			{
				// dump triangles that have no cell (e.g. this happens if a Voronoi diagram didn't enclose the whole mesh)
				Cell = Output.GetNoCellIdx();
			}

			Output.AddTriangle(Cell, OrigTriIdx + TriangleStart, Tri);
		}
	}

	// ~~~ TODO: OPTIONAL PHASE 3: One additional pass over all triangles, splitting any triangles w/ boundary edges in the split map! ~~~

	// ~~~ PHASE 4: TRIANGULATE ALL PLANAR CUTTING SURFACES AND ADD NEW FACES TO OUTPUT STRUCT ~~~

	struct PlaneTriangulationInfo
	{
		TArray<FVector> LocalVertices; // positions of vertices used in the planar triangulation (TODO: we could try to optimize / not compute this in all cases / only store added vertices, at the cost of more complex lookups later.  probably not worth it?)
		TArray<FVector2D> LocalUVs;
		//TArray<int32> VertexSources; // indices of LocalVertices in to the global Vertices; -1 indicates the vertex has no corresponding global vertex
		
		TArray<FIntVector> LocalIndices;  // triangle indices into the local VertexSources array
	};
	TArray<PlaneTriangulationInfo> PlaneTriangulations; PlaneTriangulations.SetNum(NumPlanes);

	bool bNoParallel = false;
	//for (int32 PlaneIdx = 0; PlaneIdx < NumPlanes; PlaneIdx++)
	ParallelFor(NumPlanes, [&](int32 PlaneIdx)
	{
		const FPlane &Plane = TransformedPlanes[PlaneIdx];
		const TArray<int32> BoundaryIndices = Cells.PlaneBoundaries[PlaneIdx];
		PlaneTriangulationInfo& Triangulation = PlaneTriangulations[PlaneIdx];
		int32 NumBoundary = BoundaryIndices.Num();
		FVector PlaneNormal(Plane.X, Plane.Y, Plane.Z);
		FVector Origin = (FVector)PlaneFrames[PlaneIdx].Origin;

		// check if constrained Delaunay triangulation problem needed for plane (false if no geometry was touching the planar facet)
		bool bAnyElementsOnPlane = EdgesOnPlane[PlaneIdx].Num() + TrianglesOnPlane[PlaneIdx].Num() > 0;
		bool bConvexFacet = Cells.AssumeConvexCells;
		bool bHasBoundary = NumBoundary > 2;
		if (bAnyElementsOnPlane || (bHasBoundary && (bNoiseOnPlane || !bConvexFacet)))
		{
			FAxisAlignedBox2d Bounds2D = FAxisAlignedBox2d::Empty();
			TArray<FVector2d> Boundary;
			TArray<TPair<FVector2d, FVector2d>> PlanarEdges;
			int32 BoundaryEdgeStart = -1;
			for (const TPair<FVector, FVector>& Edge : EdgesOnPlane[PlaneIdx])
			{
				PlanarEdges.Emplace(PlaneFrames[PlaneIdx].Project(Edge.Key), PlaneFrames[PlaneIdx].Project(Edge.Value));
			}
			TArray<FVector2d> ProjectedTriVertices;
			for (const int32 TriIdx : TrianglesOnPlane[PlaneIdx])
			{
				const FIntVector& Tri = Triangles[TriIdx];
				FVector2d A = PlaneFrames[PlaneIdx].Project(Vertices[Tri.X]), B = PlaneFrames[PlaneIdx].Project(Vertices[Tri.Y]), C = PlaneFrames[PlaneIdx].Project(Vertices[Tri.Z]);
				PlanarEdges.Emplace(A, B);
				PlanarEdges.Emplace(B, C);
				PlanarEdges.Emplace(C, A);
				ProjectedTriVertices.Add(A);
				ProjectedTriVertices.Add(B);
				ProjectedTriVertices.Add(C);
			}
			double BoundaryArea = 0;
			if (NumBoundary)
			{
				for (int32 PlaneBoundaryVertIdx : Cells.PlaneBoundaries[PlaneIdx])
				{
					FVector2d ProjBoundary(PlaneFrames[PlaneIdx].Project(TransformedPlaneBoundaryVertices[PlaneBoundaryVertIdx]));
					Boundary.Add(ProjBoundary);
					Bounds2D.Contain(ProjBoundary);
				}
				// area check
				BoundaryArea = 0;
				for (int32 Idx = 0; Idx+2 < NumBoundary; Idx++)
				{
					BoundaryArea += VectorUtil::Area(Boundary[Idx], Boundary[Idx + 1], Boundary[Idx + 2]);
				}
				// don't bother triangulating if the whole boundary is a tiny sliver
				if (BoundaryArea < 1e-3)
				{
					return;
					//continue;
				}
				int32 ShouldCollapseEdgeCount = 0;
				for (int32 Idx = 0, LastIdx = NumBoundary - 1; Idx < NumBoundary; LastIdx = Idx++)
				{
					if (Boundary[Idx].DistanceSquared(Boundary[LastIdx]) < 1e-4)
					{
						ShouldCollapseEdgeCount++;
					}
				}
				// After collapsing tiny edges, boundary would be a line segment (TODO: this could discard some cases that should not be discarded!  do something a bit smarter)
				if (NumBoundary - ShouldCollapseEdgeCount < 3)
				{
					return;
					//continue;
				}

				// Clip planar edges against boundary
				// This is optional; after Triangulation we filter everything outside the boundary anyway.  TODO: benchmark whether it actually helps performance
				if (Cells.AssumeConvexCells)
				{
					TArray<bool> NukeEdges;
					NukeEdges.SetNumZeroed(PlanarEdges.Num());
					for (int32 BoundEdgePrevIdx = Boundary.Num() - 1, BoundEdgeIdx = 0; BoundEdgeIdx < Boundary.Num(); BoundEdgePrevIdx = BoundEdgeIdx++)
					{
						const FVector2d& Pt = Boundary[BoundEdgeIdx];
						FVector2d Dir = Pt - Boundary[BoundEdgePrevIdx];
						FVector2d EdgeNormal = Dir.Perp();
						EdgeNormal.Normalize();
						for (int32 EdgeIdx = 0; EdgeIdx < PlanarEdges.Num(); EdgeIdx++)
						{
							if (NukeEdges[EdgeIdx])
							{
								continue;
							}
							double SDA = (PlanarEdges[EdgeIdx].Key - Pt).Dot(EdgeNormal);
							double SDB = (PlanarEdges[EdgeIdx].Value - Pt).Dot(EdgeNormal);
							if (SDB < -PlaneEps && SDA < -PlaneEps )
							{
								NukeEdges[EdgeIdx] = true;
								continue;
							}
							if (SDA*SDB < -PlaneEps) //  TODO: use more robust crossing formula?
							{
								double T = SDA / (SDA - SDB);
								FVector2d OnBoundary = PlanarEdges[EdgeIdx].Key * (1 - T) + PlanarEdges[EdgeIdx].Value * T;
								double SDO = (OnBoundary - Pt).Dot(EdgeNormal);
								ensure(FMath::Abs(SDO) < 1e-4);
								if (SDA < 0)
								{
									PlanarEdges[EdgeIdx].Key = OnBoundary;
								}
								else
								{
									PlanarEdges[EdgeIdx].Value = OnBoundary;
								}
							}
						}
					}

					// copy the non-nuked edges down, and trim the nuked ones
					int32 RemainingEdgeCount = 0;
					for (int32 EdgeIdx = 0; EdgeIdx < NukeEdges.Num(); EdgeIdx++)
					{
						if (NukeEdges[EdgeIdx])
						{
							continue;
						}
						if (EdgeIdx != RemainingEdgeCount)
						{
							PlanarEdges[RemainingEdgeCount] = PlanarEdges[EdgeIdx];
						}
						RemainingEdgeCount++;
					}
					PlanarEdges.SetNum(RemainingEdgeCount);
				}

				// Add boundary edges as planar edges also
				BoundaryEdgeStart = PlanarEdges.Num();
				for (int32 BoundIdx = 0; BoundIdx + 1 < Boundary.Num(); BoundIdx++)
				{
					PlanarEdges.Emplace(Boundary[BoundIdx], Boundary[BoundIdx + 1]);
				}
				PlanarEdges.Emplace(Boundary[Boundary.Num()-1], Boundary[0]);
			}
			else // cell has no boundary; set bounding box just on crossing edges
			{
				BoundaryEdgeStart = PlanarEdges.Num();
				for (const TPair<FVector2d, FVector2d>& Edge2d : PlanarEdges)
				{
					Bounds2D.Contain(Edge2d.Key);
					Bounds2D.Contain(Edge2d.Value);
				}
			}
			

			const double ArrangementTol = 1e-4;
			const double ScaleF = 1.0 / FMathd::Max(.01, Bounds2D.MaxDim());
			const FVector2d Offset = -Bounds2D.Center();
			FAxisAlignedBox2d ScaledBounds2D((Bounds2D.Min + Offset)*ScaleF, (Bounds2D.Max + Offset)*ScaleF);
			FArrangement2d Arrangement(FMath::Max(ScaledBounds2D.MaxDim() / 64, ArrangementTol * 10));
			Arrangement.VertexSnapTol = ArrangementTol;
			int32 BoundaryEdgeGroupID = -1;
			for (int32 EdgeIdx = PlanarEdges.Num()-1; EdgeIdx >= 0; EdgeIdx--)
			{
				const TPair<FVector2d, FVector2d>& Edge2d = PlanarEdges[EdgeIdx];
				int32 EdgeGroupID = EdgeIdx >= BoundaryEdgeStart ? BoundaryEdgeGroupID : EdgeIdx; // give all boundary edges the same group ID
				Arrangement.Insert((Edge2d.Key+Offset)*ScaleF, (Edge2d.Value+Offset)*ScaleF, EdgeGroupID);
			}
			TArray<int32> SkippedEdges;
			TArray<FIntVector> PlaneTriangulation;

			TArray<int32> NoiseVertexIndices;
			if (bNoiseOnPlane)
			{
				const FNoiseSettings& Noise = InternalMaterials->NoiseSettings.GetValue();
				const float MinPointSpacing = .1 * float(ScaleF) * AverageGlobalScaleInv;
				float PointSpacing = FMath::Max(MinPointSpacing, Noise.PointSpacing*float(ScaleF) * AverageGlobalScaleInv);

				// additional safety to avoid asking for hundreds of millions of noise points
				float Area = (ScaledBounds2D.Max.X - ScaledBounds2D.Min.X) * (ScaledBounds2D.Max.Y - ScaledBounds2D.Min.Y);
				float ApproxPointsNeeded = Area / (PointSpacing * PointSpacing);
				float MaxPointsTarget = 100000;
				if (ApproxPointsNeeded > MaxPointsTarget)
				{
					PointSpacing = FMath::Sqrt(Area / MaxPointsTarget);
					ApproxPointsNeeded = Area / (PointSpacing * PointSpacing);
				}

				// make a new point hash for blue noise point location queries
				// this is essentially the same as the point hash in arrangement2d but with cell spacing set based on the point spacing; the arrangement2d one can have a way-too-small point spacing!
				TPointHashGrid2d<int> NoisePointHash(PointSpacing, -1);
				auto HasVertexNear = [&NoisePointHash, &Arrangement, PointSpacing](const FVector2d& V, float ScaleFactor = 1.0)
				{
					auto FuncDistSq = [&Arrangement, &V](int B) { return V.DistanceSquared(Arrangement.Graph.GetVertex(B)); };
					TPair<int, double> NearestPt = NoisePointHash.FindNearestInRadius(V, PointSpacing*ScaleFactor, FuncDistSq);
					return NearestPt.Key != NoisePointHash.GetInvalidValue();
				};
				auto AddNoiseVertex = [&NoisePointHash](int32 ID, const FVector2d& Pos)
				{
					NoisePointHash.InsertPointUnsafe(ID, Pos);
				};
				for (int32 VertIdx = 0; VertIdx < Arrangement.Graph.MaxVertexID(); VertIdx++)
				{
					if (Arrangement.Graph.IsVertex(VertIdx))
					{
						AddNoiseVertex(VertIdx, Arrangement.Graph.GetVertex(VertIdx));
					}
				}

				double SpacingSq = PointSpacing*PointSpacing;

				// split the edges, ensuring there is no span on any edge that is not within Spacing dist of a vertex
				for (int32 EdgeIdx : Arrangement.Graph.EdgeIndices())
				{
					FDynamicGraph::FEdge Edge = Arrangement.Graph.GetEdge(EdgeIdx);
					FVector2d A, B;
					
					Arrangement.Graph.GetEdgeV(EdgeIdx, A, B);
					FVector2d Diff = B - A;
					double DSq = Diff.SquaredLength();
					int32 WantSamples = FMath::Sqrt(DSq / SpacingSq) + 1;
					if (DSq > SpacingSq)
					{
						int EdgeToSplit = EdgeIdx;
						for (int SampleIdx = 1; SampleIdx < WantSamples; SampleIdx++)
						{
							double T = double(SampleIdx) / double(WantSamples);
							FVector2d Pt = A + Diff * T;
							if (!HasVertexNear(Pt, .499))
							{
								bool bTargetAtEnd = Arrangement.Graph.GetEdge(EdgeToSplit).B == Edge.B;
								FIndex2i NewVertEdge = Arrangement.SplitEdgeAtPoint(EdgeToSplit, Pt);
								int NewEdge = NewVertEdge.B;
								if (bTargetAtEnd)
								{
									EdgeToSplit = NewEdge;
								}
								
								AddNoiseVertex(NewVertEdge.A, Pt);
							}
						}
					}
				}
				// insert internal noise points
				for (double X = ScaledBounds2D.Min.X; X < ScaledBounds2D.Max.X; X += PointSpacing)
				{
					for (double Y = ScaledBounds2D.Min.Y; Y < ScaledBounds2D.Max.Y; Y += PointSpacing)
					{
						for (int Attempt = 0; Attempt < 5; Attempt++)
						{
							FVector2d Pt(X + FMath::FRand() * PointSpacing*.5, Y + FMath::FRand() * PointSpacing*.5);
							if (!HasVertexNear(Pt))
							{
								// we shouldn't need to check for overlap with any existing pt/edge b/c of the above edge covering
								// (and it's much faster not to)
								int32 PtIdx = Arrangement.InsertNewIsolatedPointUnsafe(Pt);
								AddNoiseVertex(PtIdx, Pt);
								NoiseVertexIndices.Add(PtIdx);
								break;
							}
						}
					}
				}
			}

			Arrangement.AttemptTriangulate(PlaneTriangulation, SkippedEdges, BoundaryEdgeGroupID);

			// undo scaling
			double InvScaleF = 1.0 / ScaleF;
			for (int GraphVertIdx : Arrangement.Graph.VertexIndices())
			{
				Arrangement.Graph.SetVertex(GraphVertIdx, (Arrangement.Graph.GetVertex(GraphVertIdx) * InvScaleF) - Offset);
			}

			// Eat any triangles that are inside coplanar triangles on the face
			// TODO: optimize this? currently doing this in the simplest way possible because it is likely a rare case
			
			int32 NumCoplanarTris = ProjectedTriVertices.Num() / 3;
			// note: depending on epsilon, could accept an infinitely-large extent as "on triangle", seems a little dangerous ...
			auto IsOnTriangle2D = [](const FVector2d& Pt, const TArray<FVector2d>& Tris, int32 TriIdx, double TriSideEps = 1e-6)
			{
				int32 IdxStart = TriIdx * 3;
				
				char NumSideA = 0;
				char NumSideB = 0;
				for (int32 Idx = 0, LastIdx = 2; Idx < 3; LastIdx = Idx++)
				{
					FVector2d E = Tris[IdxStart + Idx] - Tris[IdxStart + LastIdx];
					E.Normalize();
					double Side = E.DotPerp(Pt - Tris[IdxStart + LastIdx]);
					if (Side < TriSideEps)
					{
						NumSideA++;
					}
					if (Side > -TriSideEps)
					{
						NumSideB++;
					}
				}
				return NumSideA == 3 || NumSideB == 3;
			};
			if (NumCoplanarTris > 0)
			{
				int32 Ate = 0;
				for (int32 PlaneTriIdx = 0, CopyTriIdx = 0; PlaneTriIdx < PlaneTriangulation.Num(); PlaneTriIdx++)
				{
					const FIntVector& Tri = PlaneTriangulation[PlaneTriIdx];
					FVector2d TriCentroid = (Arrangement.Graph.GetVertex(Tri.X) + Arrangement.Graph.GetVertex(Tri.Y) + Arrangement.Graph.GetVertex(Tri.Z)) / 3.0f;
					bool bEatTri = false;
					for (int32 CoplanarTriIdx = 0; CoplanarTriIdx < NumCoplanarTris; CoplanarTriIdx++)
					{
						if (IsOnTriangle2D(TriCentroid, ProjectedTriVertices, CoplanarTriIdx))
						{
							bEatTri = true;
							Ate++;
							break;
						}
					}
					if (!bEatTri)
					{
						// copy back any triangle that we aren't eating away
						if (PlaneTriIdx != CopyTriIdx)
						{
							PlaneTriangulation[CopyTriIdx] = PlaneTriangulation[PlaneTriIdx];
						}
						CopyTriIdx++;
					}
				}
				if (Ate)
				{
					PlaneTriangulation.SetNum(PlaneTriangulation.Num() - Ate);
				}
			}

			ensure(SkippedEdges.Num() == 0); // TODO: remove this ensure; for now I'm just curious how much triangulation fails in practice

			ensure(Arrangement.Graph.IsCompact());
			for (int32 VertIdx = 0; VertIdx < Arrangement.Graph.MaxVertexID(); VertIdx++)
			{
				Triangulation.LocalVertices.Add((FVector)PlaneFrames[PlaneIdx].UnProject(Arrangement.Graph.GetVertex(VertIdx)));
			}
			
			for (FIntVector& Face : PlaneTriangulation)
			{
				FVector3d TriCentroid = (Triangulation.LocalVertices[Face.X] + Triangulation.LocalVertices[Face.Y] + Triangulation.LocalVertices[Face.Z]) / 3.0;
				float WindingFast = FastWindingTree.FastWindingNumber(TriCentroid);
				if (WindingFast > .5)
				{
					Triangulation.LocalIndices.Add(Face);
				}
			}
			if (Triangulation.LocalIndices.Num() == 0)
			{
				Triangulation.LocalVertices.Empty();
			}
			else if (bNoiseOnPlane)
			{
				float Amplitude = InternalMaterials->NoiseSettings->Amplitude;
				float Frequency = InternalMaterials->NoiseSettings->Frequency;
				int32 Octaves = InternalMaterials->NoiseSettings->Octaves;
				FVector3d Z = PlaneNormal * Amplitude;
				for (int32 VertexIdx : NoiseVertexIndices)
				{
					FVector2D V = (FVector2D)(Arrangement.Graph.GetVertex(VertexIdx) * Frequency);
					float NoiseValue = 0;
					float OctaveScale = 1;
					for (int32 Octave = 0; Octave < Octaves; Octave++, OctaveScale *= 2)
					{
						NoiseValue += FMath::PerlinNoise2D(V * OctaveScale * AverageGlobalScale) / OctaveScale;
					}
					Triangulation.LocalVertices[VertexIdx] += (FVector)(Z * NoiseValue * AverageGlobalScaleInv);
				}
			}
		}
		else // no CDT needed; just triangulate the cell directly
		{
			ensure(NumBoundary != 1 && NumBoundary != 2);  // Point or Line Segment boundaries would be weird / imply a bug maybe
			if (NumBoundary > 2) // if there are at least 3 boundary points, we still have something we could triangulate
			{
				// TODO: should actually be "AssumeConvexFacets"?
				ensure(Cells.AssumeConvexCells);  // should have followed the above code path w/ triangulation if non-convex faces

				FVector FacetCentroid(0, 0, 0);
				for (int32 VertIdx : BoundaryIndices)
				{
					FacetCentroid += TransformedPlaneBoundaryVertices[VertIdx];
				}
				FacetCentroid /= BoundaryIndices.Num();

				double Winding = FastWindingTree.FastWindingNumber(FacetCentroid);
				if (Winding > .5)
				{
					// create a simple triangle fan covering the convex facet
					Triangulation.LocalIndices.SetNum(NumBoundary - 2);
					for (int32 TriIdx = 0; TriIdx + 2 < NumBoundary; TriIdx++)
					{
						Triangulation.LocalIndices[TriIdx] = FIntVector(0, TriIdx + 2, TriIdx + 1);
					}
					Triangulation.LocalVertices.SetNum(NumBoundary);
					for (int32 VertIdx = 0; VertIdx < NumBoundary; VertIdx++)
					{
						Triangulation.LocalVertices[VertIdx] = TransformedPlaneBoundaryVertices[BoundaryIndices[VertIdx]];
					}
				}

			}
		}
		// UV projection
		int32 NumLocalVertices = Triangulation.LocalVertices.Num();
		if (NumLocalVertices > 0)
		{
			Triangulation.LocalUVs.SetNum(NumLocalVertices);
			FVector FrameX = (FVector)PlaneFrames[PlaneIdx].X;
			FVector FrameY = (FVector)PlaneFrames[PlaneIdx].Y;
			FVector LocalOrigin = Triangulation.LocalVertices[0];
			float MinX = FMathf::MaxReal;
			float MinY = FMathf::MaxReal;
			float WorldToUVScaleFactor = InternalMaterials->GlobalUVScale;
			for (int32 VertIdx = 0; VertIdx < NumLocalVertices; VertIdx++)
			{
				FVector VMinusO = Triangulation.LocalVertices[VertIdx] - LocalOrigin;
				FVector2D ProjectedPt(FVector::DotProduct(FrameX, VMinusO) * WorldToUVScaleFactor, FVector::DotProduct(FrameY, VMinusO) * WorldToUVScaleFactor);
				MinX = FMath::Min(MinX, ProjectedPt.X);
				MinY = FMath::Min(MinY, ProjectedPt.Y);
				Triangulation.LocalUVs[VertIdx] = ProjectedPt;
			}
			for (int32 VertIdx = 0; VertIdx < NumLocalVertices; VertIdx++)
			{
				Triangulation.LocalUVs[VertIdx].X -= MinX;
				Triangulation.LocalUVs[VertIdx].Y -= MinY;
			}
		}
	}
	, bNoParallel);

	// PHASE 4 PART 2: COPY THE PLANAR TRIANGULATION VERTICES INTO THEIR RESPECTIVE CELLS

	// allocate buffers for added vertices
	int32 TotalAddedVertices = 0;
	TArray<int32> VertexIndexToGlobalAddedOffset; VertexIndexToGlobalAddedOffset.SetNum(NumPlanes);
	for (int32 PlaneIdx = 0; PlaneIdx < NumPlanes; PlaneIdx++)
	{
		VertexIndexToGlobalAddedOffset[PlaneIdx] = TotalAddedVertices;
		TotalAddedVertices += PlaneTriangulations[PlaneIdx].LocalVertices.Num() * 2; // add each vertex twice to allow for opposite vertex normals
	}
	int32 AddedVertexStart = AddedVerticesCollection.AddElements(TotalAddedVertices, FGeometryCollection::VerticesGroup);

	for (int32 PlaneIdx = 0; PlaneIdx < NumPlanes; PlaneIdx++)
	{
		const PlaneTriangulationInfo& Triangulation = PlaneTriangulations[PlaneIdx];
		if (!Triangulation.LocalIndices.Num())
		{
			continue;
		}

		int32 AddedVertexOffset = VertexIndexToGlobalAddedOffset[PlaneIdx] + AddedVertexStart;
		FIntVector TriIdxOffset = FIntVector(AddedVertexOffset + InputVertexCount);
		int32 NumLocalVertices = Triangulation.LocalVertices.Num();
		FIntVector OtherCellOffset = FIntVector(NumLocalVertices);

		int32 CellA = Output.OutputCellIdx(Cells.PlaneCells[PlaneIdx].Key);
		int32 CellB = Output.OutputCellIdx(Cells.PlaneCells[PlaneIdx].Value);

		const FPlane &Plane = TransformedPlanes[PlaneIdx];
		FVector PlaneNormal(Plane.X, Plane.Y, Plane.Z);
		
		if (CheckDistanceAcrossOutsideCellForProximity > 0 && (CellA == Output.NoCellIdx || CellB == Output.NoCellIdx))
		{
			int32 InsideCell = CellA;
			FVector Direction = PlaneNormal;
			if (InsideCell == Output.NoCellIdx)
			{
				InsideCell = CellB;
				Direction = -Direction;
			}
			for (const FIntVector &LocalTri : Triangulation.LocalIndices)
			{
				FVector C = (
					Triangulation.LocalVertices[LocalTri.X] +
					Triangulation.LocalVertices[LocalTri.Y] +
					Triangulation.LocalVertices[LocalTri.Z]) / 3.0f;
				int32 AcrossCell = Cells.CellFromPosition(C + Direction * CheckDistanceAcrossOutsideCellForProximity);
				if (AcrossCell != InsideCell)
				{
					Output.ConnectCells(InsideCell, AcrossCell);
				}
			}
		}
		else
		{
			Output.ConnectCells(CellA, CellB);
		}
		for (const FIntVector &LocalTri : Triangulation.LocalIndices)
		{
			FIntVector GlobalTri = LocalTri + TriIdxOffset;
			Output.AddTriangle(CellB, -1, GlobalTri + OtherCellOffset);
			
			Swap(GlobalTri.Y, GlobalTri.Z);
			Output.AddTriangle(CellA, -1, GlobalTri);
		}
		for (int32 LocalVertIdx = 0; LocalVertIdx < Triangulation.LocalVertices.Num(); LocalVertIdx++)
		{
			int32 AddIdx = AddedVertexOffset + LocalVertIdx;
			AddedVerticesCollection.Vertex[AddIdx] = Triangulation.LocalVertices[LocalVertIdx];
			AddedVerticesCollection.Vertex[AddIdx + NumLocalVertices] = Triangulation.LocalVertices[LocalVertIdx];

			AddedVerticesCollection.UV[AddIdx] = Triangulation.LocalUVs[LocalVertIdx];
			AddedVerticesCollection.UV[AddIdx + NumLocalVertices] = Triangulation.LocalUVs[LocalVertIdx];

			AddedVerticesCollection.Normal[AddIdx] = PlaneNormal;
			AddedVerticesCollection.Normal[AddIdx + NumLocalVertices] = -AddedVerticesCollection.Normal[AddIdx];

			AddedVerticesCollection.TangentU[AddIdx] = (FVector)PlaneFrames[PlaneIdx].X;
			AddedVerticesCollection.TangentU[AddIdx + NumLocalVertices] = -AddedVerticesCollection.TangentU[AddIdx];

			AddedVerticesCollection.TangentV[AddIdx] = (FVector)PlaneFrames[PlaneIdx].Y;
			AddedVerticesCollection.TangentV[AddIdx + NumLocalVertices] = AddedVerticesCollection.TangentV[AddIdx];

			// TODO: also set color?
		}
	}


	// ~~~ PHASE 5: FIGURE OUT VERTEX MAPPING FROM SHARED ORIGINAL DATA INTO NEW CELLS ~~~
	// Obstacles to parallelize: None?
	for (int32 CellIdx = 0; CellIdx < Output.Num(); CellIdx++)
	{
		TMap<int32, int32> GlobalLocalVertexMap;
		auto RemapVertex = [](TMap<int32, int32> &AllVertexMap, TArray<int32>& CellVertexMapping, int32 GlobalIdx)
		{
			int32 *Mapped = AllVertexMap.Find(GlobalIdx);
			if (Mapped)
			{
				return *Mapped;
			}
			int32 LocalIdx = CellVertexMapping.Add(GlobalIdx);
			AllVertexMap.Add(GlobalIdx, LocalIdx);
			return LocalIdx;
		};
		for (FIntVector& Tri : Output.CellTriangles[CellIdx])
		{
			Tri.X = RemapVertex(GlobalLocalVertexMap, Output.CellVertexMapping[CellIdx], Tri.X);
			Tri.Y = RemapVertex(GlobalLocalVertexMap, Output.CellVertexMapping[CellIdx], Tri.Y);
			Tri.Z = RemapVertex(GlobalLocalVertexMap, Output.CellVertexMapping[CellIdx], Tri.Z);
		}
	}
}



void TransformPlanes(const FTransform& Transform, const FPlanarCells& Ref, TArray<FPlane>& Planes, TArray<FVector>& PlaneBoundaries)
{
	// Note: custom implementation of normal transform for robustness, especially to ensure we don't zero the normals for significantly scaled geometry
	FTransform NormalTransform = Transform;
	FVector3d ScaleVec(NormalTransform.GetScale3D());
	double ScaleDetSign = FMathd::SignNonZero(ScaleVec.X) * FMathd::SignNonZero(ScaleVec.Y) * FMathd::SignNonZero(ScaleVec.Z);
	double ScaleMaxAbs = ScaleVec.MaxAbsElement();
	if (ScaleMaxAbs > DBL_MIN)
	{
		ScaleVec /= ScaleMaxAbs;
	}
	FVector3d NormalScale(ScaleVec.Y*ScaleVec.Z*ScaleDetSign, ScaleVec.X*ScaleVec.Z*ScaleDetSign, ScaleVec.X*ScaleVec.Y*ScaleDetSign);
	NormalTransform.SetScale3D((FVector)NormalScale);
	
	Planes.SetNum(Ref.Planes.Num());
	for (int32 PlaneIdx = 0, PlanesNum = Planes.Num(); PlaneIdx < PlanesNum; PlaneIdx++)
	{
		FVector Pos = Transform.TransformPosition(Ref.Planes[PlaneIdx] * Ref.Planes[PlaneIdx].W);
		FVector Normal = NormalTransform.TransformVector(Ref.Planes[PlaneIdx]).GetSafeNormal(FLT_MIN);
		Planes[PlaneIdx] = FPlane(Pos, Normal);
	}

	PlaneBoundaries.SetNum(Ref.PlaneBoundaryVertices.Num());
	for (int32 VertIdx = 0, VertsNum = PlaneBoundaries.Num(); VertIdx < VertsNum; VertIdx++)
	{
		PlaneBoundaries[VertIdx] = Transform.TransformPosition(Ref.PlaneBoundaryVertices[VertIdx]);
	}
}


// Simpler invocation of CutWithPlanarCells w/ reasonable defaults
int32 CutWithPlanarCells(
	FPlanarCells& Cells,
	FGeometryCollection& Source,
	int32 TransformIdx,
	const TOptional<FTransform>& TransformCells,
	bool bIncludeOutsideCellInOutput,
	float CheckDistanceAcrossOutsideCellForProximity,
	bool bSetDefaultInternalMaterialsFromCollection,
	TFunction<void(const FGeometryCollection&, int32, const FGeometryCollection&, int32, float, int32, FGeometryCollection&)> VertexInterpolate
)
{
	TArray<int32> TransformIndices { TransformIdx };
	return CutMultipleWithPlanarCells(Cells, Source, TransformIndices, TransformCells, bIncludeOutsideCellInOutput, CheckDistanceAcrossOutsideCellForProximity, bSetDefaultInternalMaterialsFromCollection, VertexInterpolate);
}

int32 CutMultipleWithPlanarCells_MeshBooleanPath(
	FPlanarCells& Cells,
	FGeometryCollection& Source,
	const TArrayView<const int32>& TransformIndices,
	const TOptional<FTransform>& TransformCells,
	bool bIncludeOutsideCellInOutput,
	float CheckDistanceAcrossOutsideCellForProximity,
	bool bSetDefaultInternalMaterialsFromCollection,
	TFunction<void(const FGeometryCollection&, int32, const FGeometryCollection&, int32, float, int32, FGeometryCollection&)> VertexInterpolate
);

// Cut multiple Geometry groups inside a GeometryCollection with PlanarCells, and add each cut cell back to the GeometryCollection as a new child of their source Geometry
int32 CutMultipleWithPlanarCells(
	FPlanarCells &Cells,
	FGeometryCollection& Source,
	const TArrayView<const int32>& TransformIndices,
	const TOptional<FTransform>& TransformCells,
	bool bIncludeOutsideCellInOutput,
	float CheckDistanceAcrossOutsideCellForProximity,
	bool bSetDefaultInternalMaterialsFromCollection,
	TFunction<void(const FGeometryCollection&, int32, const FGeometryCollection&, int32, float, int32, FGeometryCollection&)> VertexInterpolate
)
{
	if (CVarFractureMethod.GetValueOnAnyThread() >= 1)
	{
		return CutMultipleWithPlanarCells_MeshBooleanPath(
			Cells,
			Source,
			TransformIndices,
			TransformCells,
			bIncludeOutsideCellInOutput,
			CheckDistanceAcrossOutsideCellForProximity,
			bSetDefaultInternalMaterialsFromCollection,
			VertexInterpolate);
	}
	float PlaneEps = 1e-4;

	int32 NewGeomStartIdx = -1;

	FTransform CellsToWorld = TransformCells.Get(FTransform::Identity);

	TArray<FPlane> TransformedPlanes;
	TArray<FVector> TransformedPlaneBoundaries;

	if (!Source.HasAttribute("Proximity", FGeometryCollection::GeometryGroup))
	{
		const FManagedArrayCollection::FConstructionParameters GeometryDependency(FGeometryCollection::GeometryGroup);
		Source.AddAttribute<TSet<int32>>("Proximity", FGeometryCollection::GeometryGroup, GeometryDependency);
	}

	if (bSetDefaultInternalMaterialsFromCollection)
	{
		Cells.InternalSurfaceMaterials.SetUVScaleFromCollection(Source);
	}

#if WITH_EDITOR
	// Create progress indicator dialog
	static const FText SlowTaskText = NSLOCTEXT("CutMultipleWithPlanarCells", "CutMultipleWithPlanarCellsText", "Cutting geometry collection...");

	int32 TotalFacesToProcess = 0;
	for (int32 TransformIdx : TransformIndices)
	{
		int32 GeometryIdx = Source.TransformToGeometryIndex[TransformIdx];
		TotalFacesToProcess += Source.FaceCount[GeometryIdx];
	}

	FScopedSlowTask SlowTask(float(TotalFacesToProcess), SlowTaskText);
	SlowTask.MakeDialog();

	// Declare progress shortcut lambdas
	auto EnterProgressFrame = [&SlowTask](int32 TaskSize)
	{
		SlowTask.EnterProgressFrame(float(TaskSize));
	};
#else
	auto EnterProgressFrame = [](int32 /*TaskSize*/) {};
#endif

	// TODO: Cannot yet make this a parallel for because the Voronoi pt->cell function is not thread-safe, but after that is fixed, consider threading this?
	for (int32 ParentTransformIndex : TransformIndices)
	{
		int32 GeometryIdx = Source.TransformToGeometryIndex[ParentTransformIndex];
		EnterProgressFrame(Source.FaceCount[GeometryIdx]);
		if (Source.Children[ParentTransformIndex].Num())
		{
			// don't fracture an already-fractured geometry
			UE_LOG(LogPlanarCut, Warning, TEXT("Skipping cut of a non-leaf geometry, as this would would create intersecting / duplicate geometry"));
			continue;
		}
		int32 TriangleStart = Source.FaceStart[GeometryIdx];
		int32 NumTriangles = Source.FaceCount[GeometryIdx];
		TArray<FVector> TriangleNormals;
		ComputeTriangleNormals(TArrayView<const FVector>(Source.Vertex.GetData(), Source.Vertex.Num()), TArrayView<const FIntVector>(Source.Indices.GetData() + TriangleStart, NumTriangles), TriangleNormals);

		TArrayView<const FPlane> Planes;
		TArrayView<const FVector> PlaneBoundaries;
		FTransform LocalToPlaneSpaceTransform = FTransform::Identity;

		LocalToPlaneSpaceTransform = GeometryCollectionAlgo::GlobalMatrix(Source.Transform, Source.Parent, ParentTransformIndex) * CellsToWorld.Inverse();
		FTransform PlanesToLocalTransform = LocalToPlaneSpaceTransform.Inverse();
		TransformPlanes(PlanesToLocalTransform, Cells, TransformedPlanes, TransformedPlaneBoundaries);
		Planes = TransformedPlanes;
		PlaneBoundaries = TransformedPlaneBoundaries;

		OutputCells Output(Cells.NumCells);
		// TODO: handle bIncludeOutsideCellInOutput in the CutWithPlanarCellsHelper so you don't even store the no-cell geometry in the temporary Output object (currently we always compute everything and then just don't dump the no-cell to the geom collection if bIncludeOutsideCellInOutput==false)
		CutWithPlanarCellsHelper(Cells, Planes, PlaneBoundaries, LocalToPlaneSpaceTransform, Source, GeometryIdx, TriangleStart, NumTriangles, TriangleNormals, PlaneEps, CheckDistanceAcrossOutsideCellForProximity, VertexInterpolate, Output);
		if (Output.NumNonEmptyCells() == 1) // nothing was actually cut; skip cutting this geometry entirely
		{
			continue;
		}
		int32 SourceVertexNum = Source.Vertex.Num();
		int MaterialIDOverride = bSetDefaultInternalMaterialsFromCollection ? Cells.InternalSurfaceMaterials.GetDefaultMaterialIDForGeometry(Source, GeometryIdx) : -1;
		int32 StartIdx = Output.AddToGeometryCollection(Source, Cells.InternalSurfaceMaterials, bIncludeOutsideCellInOutput, SourceVertexNum, ParentTransformIndex, MaterialIDOverride);
		if (NewGeomStartIdx < 0)
		{
			NewGeomStartIdx = StartIdx;
		}

		// turn off old geom visibility (preferred default behavior)
		int32 FaceEnd = Source.FaceCount[GeometryIdx] + Source.FaceStart[GeometryIdx];
		for (int32 FaceIdx = Source.FaceStart[GeometryIdx]; FaceIdx < FaceEnd; FaceIdx++)
		{
			Source.Visible[FaceIdx] = false;
		}
	}

	// Fix MaterialIndex values
	Source.ReindexMaterials();
	return NewGeomStartIdx;
}




int32 CutMultipleWithMultiplePlanes_old(
	const TArrayView<const FPlane>& Planes,
	FInternalSurfaceMaterials& InternalSurfaceMaterials,
	FGeometryCollection& Collection,
	const TArrayView<const int32>& TransformIndices,
	const TOptional<FTransform>& TransformCells,
	bool bSetDefaultInternalMaterialsFromCollection,
	TFunction<void(const FGeometryCollection&, int32, const FGeometryCollection&, int32, float, int32, FGeometryCollection&)> VertexInterpolate
)
{
	bool bFlattenToSingleLayer = true;
	float PlaneEps = 1e-4;

	int32 OrigNumGeom = Collection.FaceCount.Num();
	int32 CurNumGeom = OrigNumGeom;

	FTransform CellsToWorld = TransformCells.Get(FTransform::Identity);

	TArray<int32> NeedsCut;

	if (bSetDefaultInternalMaterialsFromCollection)
	{
		InternalSurfaceMaterials.SetUVScaleFromCollection(Collection);
	}

	TArray<int32> TransformsToDelete;
	NeedsCut.SetNum(TransformIndices.Num());
	for (int32 Idx = 0; Idx < NeedsCut.Num(); Idx++)
	{
		// add geometry idx corresponding to each input transform
		NeedsCut[Idx] = Collection.TransformToGeometryIndex[TransformIndices[Idx]];
	}

	if (!Collection.HasAttribute("Proximity", FGeometryCollection::GeometryGroup))
	{
		const FManagedArrayCollection::FConstructionParameters GeometryDependency(FGeometryCollection::GeometryGroup);
		Collection.AddAttribute<TSet<int32>>("Proximity", FGeometryCollection::GeometryGroup, GeometryDependency);
	}

	TMap<int32, TPair<TUniquePtr<FGeometryCollectionMeshAdapter>, TUniquePtr<TMeshAABBTree3<FGeometryCollectionMeshAdapter>>>> AABBTrees;
	FCriticalSection Mutex;
	auto GetTree = [&](int32 GeometryIdx)
	{
		{
			FScopeLock Lock(&Mutex);
			if (AABBTrees.Contains(GeometryIdx))
			{
				return AABBTrees[GeometryIdx].Value.Get();
			}
		}
		
		FGeometryCollectionMeshAdapter* Adapter = new FGeometryCollectionMeshAdapter{ &Collection, GeometryIdx };
		TMeshAABBTree3<FGeometryCollectionMeshAdapter>* AABBTree = new TMeshAABBTree3<FGeometryCollectionMeshAdapter>(Adapter);
		{
			FScopeLock Lock(&Mutex);
			AABBTrees.Add(GeometryIdx, TPair<TUniquePtr<FGeometryCollectionMeshAdapter>, TUniquePtr<TMeshAABBTree3<FGeometryCollectionMeshAdapter>>>(Adapter, AABBTree));
		}
		return AABBTree;
	};

#if WITH_EDITOR
	// Create progress indicator dialog
	static const FText SlowTaskText = NSLOCTEXT("CutMultipleWithMultiplePlanes", "CutMultipleWithMultiplePlanesText", "Cutting geometry collection with plane(s)...");

	const int32 UnitProgressOutOfLoop = 1;  // One progress frame is equivalent to a minimum of 2% of the loop progress

	FScopedSlowTask SlowTask(float(Planes.Num()), SlowTaskText);
	SlowTask.MakeDialog();

	// Declare progress shortcut lambdas
	auto EnterProgressFrame = [&SlowTask, UnitProgressOutOfLoop]()
	{
		SlowTask.EnterProgressFrame(float(UnitProgressOutOfLoop));
	};
#else
	auto EnterProgressFrame = []() {};
#endif

	for (int32 PlaneIdx = 0; PlaneIdx < Planes.Num(); PlaneIdx++)
	{
		EnterProgressFrame();

		TArray<int32> NeedsDelete, ChildrenOfTheDeleted;

		const FPlane& Plane = Planes[PlaneIdx];
		int32 LastNumGeom = CurNumGeom;
		CurNumGeom = Collection.FaceCount.Num();

		// TUniquePtr instead of just storing OutputCells as values because the OutputCells carry GeometryCollections and those will get messed up if the TArray resizes its storage ... TODO: fix better make less bad
		TArray<TUniquePtr<OutputCells>> AllOutputsForPlane;
		for (int32 Idx = 0; Idx < NeedsCut.Num(); Idx++)
		{
			AllOutputsForPlane.Add(TUniquePtr<OutputCells>(new OutputCells(2)));
		}

		// helper function to parallelize plane cuts
		auto CutGeometryWithPlane = [&](int OutputIdx)
		{
			int32 GeometryIdx = NeedsCut[OutputIdx];

			int32 ParentTransformIndex = Collection.TransformIndex[GeometryIdx];
			if (Collection.Children[ParentTransformIndex].Num())
			{
				// don't fracture an already-fractured geometry
				ensureMsgf(false, TEXT("Skipping cut of a non-leaf geometry, as this would would create intersecting / duplicate geometry"));
				return;
			}

			FPlane TransformedPlane = Plane;
			FTransform LocalToPlaneSpaceTransform = GeometryCollectionAlgo::GlobalMatrix(Collection.Transform, Collection.Parent, ParentTransformIndex) * CellsToWorld.Inverse();
			FTransform PlanesToLocalTransform = LocalToPlaneSpaceTransform.Inverse();
			FMatrix Matrix = PlanesToLocalTransform.ToMatrixWithScale();
			TransformedPlane = Plane.TransformBy(Matrix);

			if (!FMath::PlaneAABBIntersection(TransformedPlane, Collection.BoundingBox[GeometryIdx]))
			{
				// no intersection; can skip
				return;
			}

			int32 TriangleStart = Collection.FaceStart[GeometryIdx];
			int32 NumTriangles = Collection.FaceCount[GeometryIdx];
			TArray<FVector> TriangleNormals;
			ComputeTriangleNormals(TArrayView<const FVector>(Collection.Vertex.GetData(), Collection.Vertex.Num()), TArrayView<const FIntVector>(Collection.Indices.GetData() + TriangleStart, NumTriangles), TriangleNormals);

			OutputCells& Output = *AllOutputsForPlane[OutputIdx].Get();
			FPlanarCells PlaneCells(TransformedPlane);
			// TODO: handle bIncludeOutsideCellInOutput in the CutWithPlanarCellsHelper so you don't even store the no-cell geometry in the temporary Output object (currently we always compute everything and then just don't dump the no-cell to the geom collection if bIncludeOutsideCellInOutput==false)
			TMeshAABBTree3<FGeometryCollectionMeshAdapter> *AABBTree = nullptr;
			if (bFlattenToSingleLayer)
			{
				AABBTree = GetTree(GeometryIdx);
			}
			CutWithPlanarCellsHelper(PlaneCells, PlaneCells.Planes, PlaneCells.PlaneBoundaryVertices, FTransform::Identity, Collection, GeometryIdx, TriangleStart, NumTriangles, TriangleNormals, PlaneEps, 0, VertexInterpolate, Output, &InternalSurfaceMaterials, AABBTree);
		};

		// cut all geometries with the given plane
		bool bNoParallel = false;
		ParallelFor(NeedsCut.Num(), CutGeometryWithPlane, bNoParallel);

		int32 SourceVertexNumWhenCut = Collection.Vertex.Num();

		// collect outputs
		for (int32 OutputIdx = 0; OutputIdx < AllOutputsForPlane.Num(); OutputIdx++)
		{
			OutputCells& Output = *AllOutputsForPlane[OutputIdx].Get();

			// TODO: handle bIncludeOutsideCellInOutput in the CutWithPlanarCellsHelper so you don't even store the no-cell geometry in the temporary Output object (currently we always compute everything and then just don't dump the no-cell to the geom collection if bIncludeOutsideCellInOutput==false)
			if (Output.NumNonEmptyCells() <= 1) // nothing was actually cut; skip cutting this geometry entirely
			{
				continue;
			}

			int32 GeometryIdx = NeedsCut[OutputIdx];
			int32 ParentTransformIndex = Collection.TransformIndex[GeometryIdx];
			int MaterialIDOverride = bSetDefaultInternalMaterialsFromCollection ? InternalSurfaceMaterials.GetDefaultMaterialIDForGeometry(Collection, GeometryIdx) : -1;
			int32 AddedStartIdx = Output.AddToGeometryCollection(Collection, InternalSurfaceMaterials, true, SourceVertexNumWhenCut, ParentTransformIndex, MaterialIDOverride);
			check(Collection.FaceCount.Num() - AddedStartIdx == 2); // plane must have cut two

			// replace the old geometry with the first of the cut halves
			NeedsCut[OutputIdx] = AddedStartIdx;
			// add the other half to the end
			NeedsCut.Add(AddedStartIdx+1);

			// turn off old geom visibility (preferred default behavior)
			int32 FaceEnd = Collection.FaceCount[GeometryIdx] + Collection.FaceStart[GeometryIdx];
			for (int32 FaceIdx = Collection.FaceStart[GeometryIdx]; FaceIdx < FaceEnd; FaceIdx++)
			{
				Collection.Visible[FaceIdx] = false;
			}

			if (bFlattenToSingleLayer && GeometryIdx >= OrigNumGeom)
			{
				// flag the geometry for deletion, and store where its replacement sub-parts went (for fixing proximity data, below)
				TransformsToDelete.Add(ParentTransformIndex);
				NeedsDelete.Add(GeometryIdx);
				ChildrenOfTheDeleted.Add(AddedStartIdx);
			}
		}

		if (bFlattenToSingleLayer)
		{
			TArray<FTransform> GlobalTransforms;
			GeometryCollectionAlgo::GlobalMatrices(Collection.Transform, Collection.Parent, GlobalTransforms);

			// update proximities for the children of the deleted using the proximity data from their to-be-deleted parents
			for (int32 DeleteIdx = 0; DeleteIdx < NeedsDelete.Num(); DeleteIdx++)
			{
				int32 GeometryIdx = NeedsDelete[DeleteIdx];
				int32 ChildrenLoc = ChildrenOfTheDeleted[DeleteIdx];
				TManagedArray<TSet<int32>>& Proximity = Collection.GetAttribute<TSet<int32>>("Proximity", FGeometryCollection::GeometryGroup);
				auto ConnectPair = [&](int32 A, int32 B)
				{
					Proximity[A].Add(B);
					Proximity[B].Add(A);
				};
				const double ProximityThresholdDist = 1e-4;
				for (int32 NbrGeometryIdx : Proximity[GeometryIdx])
				{
					int32 NbrDeleteIdx = NeedsDelete.Find(NbrGeometryIdx);
					if (NbrDeleteIdx != INDEX_NONE)
					{
						int32 NbrChildrenLoc = ChildrenOfTheDeleted[NbrDeleteIdx];
						// we're deleting both geometries, so we have to consider how to connect their children
						if (DeleteIdx < NbrDeleteIdx)
						{	// we only consider if DeleteIdx < NbrDeleteIdx because otherwise we can assume that we already handled this relationship when we handled the Nbr earlier
							for (int NbrChildSubIdx = 0; NbrChildSubIdx < 2; NbrChildSubIdx++)
							{
								TMeshAABBTree3<FGeometryCollectionMeshAdapter>* NbrTree = GetTree(NbrChildrenLoc + NbrChildSubIdx);
								for (int ChildSubIdx = 0; ChildSubIdx < 2; ChildSubIdx++)
								{
									TMeshAABBTree3<FGeometryCollectionMeshAdapter>* ChildTree = GetTree(ChildrenLoc + ChildSubIdx);
									int32 ChildTransformIdx = Collection.TransformIndex[ChildrenLoc + ChildSubIdx];
									int32 NbrTransformIdx = Collection.TransformIndex[NbrChildrenLoc + NbrChildSubIdx];
									FTransform NbrToLocalTransform = GlobalTransforms[NbrTransformIdx] * GlobalTransforms[ChildTransformIdx].Inverse();
									auto NbrToLocal = [&](const FVector3d& V)
									{
										return FVector3d(NbrToLocalTransform.TransformPosition((FVector)V));
									};
									double OutDist;
									ChildTree->FindNearestTriangles(*NbrTree, NbrToLocal, OutDist, ProximityThresholdDist);
									if (OutDist < ProximityThresholdDist)
									{
										ConnectPair(NbrChildrenLoc + NbrChildSubIdx, ChildrenLoc + ChildSubIdx);
									}
								}
							}
							
						}
					}
					else
					{
						// we're just deleting this geometry; can connect directly to the neighbor
						FMatrix WorldToNbrGeom = GlobalTransforms[NbrGeometryIdx].Inverse().ToMatrixWithScale();
						int32 PlaneSide = FMath::PlaneAABBRelativePosition(Plane.TransformBy(WorldToNbrGeom), Collection.BoundingBox[NbrGeometryIdx]);

						switch (PlaneSide)
						{
						case -1:
							ConnectPair(ChildrenLoc, NbrGeometryIdx);
							break;
						case 1:
							ConnectPair(ChildrenLoc + 1, NbrGeometryIdx);
							break;
						case 0:
						{
							TMeshAABBTree3<FGeometryCollectionMeshAdapter> *NbrTree = GetTree(NbrGeometryIdx);
							for (int ChildSubIdx = 0; ChildSubIdx < 2; ChildSubIdx++)
							{
								TMeshAABBTree3<FGeometryCollectionMeshAdapter> *ChildTree = GetTree(ChildrenLoc + ChildSubIdx);
								int32 ChildTransformIdx = Collection.TransformIndex[ChildrenLoc + ChildSubIdx];
								int32 NbrTransformIdx = Collection.TransformIndex[NbrGeometryIdx];
								FTransform NbrToLocalTransform = GlobalTransforms[NbrTransformIdx] * GlobalTransforms[ChildTransformIdx].Inverse();
								auto NbrToLocal = [&](const FVector3d& V)
								{
									return FVector3d(NbrToLocalTransform.TransformPosition((FVector)V));
								};
								double OutDist;
								ChildTree->FindNearestTriangles(*NbrTree, NbrToLocal, OutDist, ProximityThresholdDist);
								if (OutDist < ProximityThresholdDist)
								{
									ConnectPair(NbrGeometryIdx, ChildrenLoc + ChildSubIdx);
								}
							}
						}
						break;
						default:
							ensure(false); // PlaneSide must be -1, 0, or 1
						}

					}
				}
			}
			for (int32 DeleteIdx = 0; DeleteIdx < NeedsDelete.Num(); DeleteIdx++)
			{
				AABBTrees.Remove(NeedsDelete[DeleteIdx]);
			}
		}
	}

	if (bFlattenToSingleLayer)
	{
		TransformsToDelete.Sort();
		Collection.RemoveElements(FGeometryCollection::TransformGroup, TransformsToDelete);
	}

	// Fix MaterialIndex values
	Collection.ReindexMaterials();

	return OrigNumGeom == Collection.FaceCount.Num() ? -1 : OrigNumGeom;
}


int32 CutMultipleWithMultiplePlanes(
	const TArrayView<const FPlane>& Planes,
	FInternalSurfaceMaterials& InternalSurfaceMaterials,
	FGeometryCollection& Collection,
	const TArrayView<const int32>& TransformIndices,
	const TOptional<FTransform>& TransformCells,
	bool bSetDefaultInternalMaterialsFromCollection,
	TFunction<void(const FGeometryCollection&, int32, const FGeometryCollection&, int32, float, int32, FGeometryCollection&)> VertexInterpolate
)
{
	if (CVarFractureMethod.GetValueOnAnyThread() <= 0)
	{
		return CutMultipleWithMultiplePlanes_old(Planes, InternalSurfaceMaterials, Collection, TransformIndices, TransformCells, bSetDefaultInternalMaterialsFromCollection, VertexInterpolate);
	}
	int32 OrigNumGeom = Collection.FaceCount.Num();
	int32 CurNumGeom = OrigNumGeom;

	if (bSetDefaultInternalMaterialsFromCollection)
	{
		InternalSurfaceMaterials.SetUVScaleFromCollection(Collection);
	}

	if (!Collection.HasAttribute("Proximity", FGeometryCollection::GeometryGroup))
	{
		const FManagedArrayCollection::FConstructionParameters GeometryDependency(FGeometryCollection::GeometryGroup);
		Collection.AddAttribute<TSet<int32>>("Proximity", FGeometryCollection::GeometryGroup, GeometryDependency);
	}

	FTransform CellsToWorld = TransformCells.Get(FTransform::Identity);
	FTransform WorldToCells = CellsToWorld.Inverse();

	FDynamicMeshCollection MeshCollection(&Collection, TransformIndices, CellsToWorld);
	double OnePercentExtend = MeshCollection.Bounds.MaxDim() * .01;

	int32 NewGeomStartIdx = -1;
	NewGeomStartIdx = MeshCollection.CutWithMultiplePlanes(Planes, &Collection, InternalSurfaceMaterials, bSetDefaultInternalMaterialsFromCollection);

	Collection.ReindexMaterials();
	return NewGeomStartIdx;
}


// Cut multiple Geometry groups inside a GeometryCollection with PlanarCells, and add each cut cell back to the GeometryCollection as a new child of their source Geometry
int32 CutMultipleWithPlanarCells_MeshBooleanPath(
	FPlanarCells& Cells,
	FGeometryCollection& Source,
	const TArrayView<const int32>& TransformIndices,
	const TOptional<FTransform>& TransformCells,
	bool bIncludeOutsideCellInOutput,
	float CheckDistanceAcrossOutsideCellForProximity,
	bool bSetDefaultInternalMaterialsFromCollection,
	TFunction<void(const FGeometryCollection&, int32, const FGeometryCollection&, int32, float, int32, FGeometryCollection&)> VertexInterpolate
)
{
	if (!Source.HasAttribute("Proximity", FGeometryCollection::GeometryGroup))
	{
		const FManagedArrayCollection::FConstructionParameters GeometryDependency(FGeometryCollection::GeometryGroup);
		Source.AddAttribute<TSet<int32>>("Proximity", FGeometryCollection::GeometryGroup, GeometryDependency);
	}

	if (bSetDefaultInternalMaterialsFromCollection)
	{
		Cells.InternalSurfaceMaterials.SetUVScaleFromCollection(Source);
	}

	FTransform CellsToWorld = TransformCells.Get(FTransform::Identity);

	FDynamicMeshCollection MeshCollection(&Source, TransformIndices, CellsToWorld);
	double OnePercentExtend = MeshCollection.Bounds.MaxDim() * .01;
	FCellMeshes CellMeshes(Cells, MeshCollection.Bounds, OnePercentExtend, bIncludeOutsideCellInOutput);

	int32 NewGeomStartIdx = -1;

	NewGeomStartIdx = MeshCollection.CutWithCellMeshes(Cells, CellMeshes, &Source, bSetDefaultInternalMaterialsFromCollection);

	Source.ReindexMaterials();
	return NewGeomStartIdx;
}










