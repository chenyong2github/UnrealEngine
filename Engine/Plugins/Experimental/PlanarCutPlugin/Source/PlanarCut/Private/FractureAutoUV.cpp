// Copyright Epic Games, Inc. All Rights Reserved.
#include "FractureAutoUV.h"
#include "PlanarCutPlugin.h"

#include "GeometryMeshConversion.h"

#include "Async/ParallelFor.h"

#include "GeometryCollection/GeometryCollectionAlgo.h"

#include "DynamicMesh3.h"
#include "MeshNormals.h"
#include "MeshWeights.h"
#include "Parameterization/DynamicMeshUVEditor.h"
#include "Sampling/MeshImageBakingCache.h"

#include "Implicit/Solidify.h"

#include "Parameterization/UVPacking.h"
#include "Sampling/MeshCurvatureMapBaker.h"
#include "Sampling/MeshOcclusionMapBaker.h"
#include "Solvers/MeshSmoothing.h"
#include "DisjointSet.h"

#include "Image/ImageOccupancyMap.h"
#include "VectorUtil.h"
#include "FrameTypes.h"

#include "Templates/PimplPtr.h"

#if WITH_EDITOR
#include "Misc/ScopedSlowTask.h"
#endif

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;



namespace UE
{
	namespace UVPacking
	{
		using namespace UE::Geometry;
		/**
		 * Create UV islands from a triangle mesh connectivity.
		 * Assumes the triangles are already split at UV seams, but topologically connected otherwise.
		 * Not recommended for meshes that already have built-in edge connectivity data.
		 *
		 * @param Mesh The mesh to create islands for
		 * @param IslandsOut The triangle IDs for each island
		 * @param IncludeTri Optional function to filter which tris are assigned to islands
		 */
		template <typename TriangleMeshType>
		inline void CreateUVIslandsFromMeshTopology(TriangleMeshType& Mesh,
			TArray<TArray<int>>& IslandsOut,
			TFunctionRef<bool(int32)> IncludeTri = [](int32)
		{
			return true;
		})
		{
			FDisjointSet VertComponents(Mesh.MaxVertexID());
			// Add Source vertices to hash & disjoint sets
			for (int32 TID = 0; TID < Mesh.MaxTriangleID(); TID++)
			{
				if (!Mesh.IsTriangle(TID) || !IncludeTri(TID))
				{
					continue;
				}

				FIndex3i Tri = Mesh.GetTriangle(TID);
				for (int32 First = 2, Second = 0; Second < 3; First = Second++)
				{
					VertComponents.Union(Tri[First], Tri[Second]);
				}
			}

			IslandsOut.Reset();
			TMap<uint32, int32> IslandIDToIdx;
			for (int32 TID = 0; TID < Mesh.MaxTriangleID(); TID++)
			{
				if (!Mesh.IsTriangle(TID) || !IncludeTri(TID))
				{
					continue;
				}

				FIndex3i Tri = Mesh.GetTriangle(TID);
				uint32 IslandID = VertComponents.Find(Tri.A);
				int32 Idx = -1;
				int32* FoundIdx = IslandIDToIdx.Find(IslandID);
				if (!FoundIdx)
				{
					Idx = IslandsOut.Emplace();
					IslandIDToIdx.Add(IslandID, Idx);
				}
				else
				{
					Idx = *FoundIdx;
				}
				IslandsOut[Idx].Add(TID);
			}
		}
	}
} // namespace UE::UVPacking



namespace UE { namespace PlanarCut {


namespace {
	inline bool IsTriActive(bool bIsVisible, int32 MaterialID, const TSet<int32>& InsideMaterials, bool bActivateInsideTriangles, bool bOddMaterialAreInside)
	{
		if (!bIsVisible)
		{
			return false;
		}

		if (bOddMaterialAreInside && ((MaterialID % 2) == 0) == bActivateInsideTriangles)
		{
			return false;
		}
		if (InsideMaterials.Num() > 0 && (InsideMaterials.Contains(MaterialID) != bActivateInsideTriangles))
		{
			return false;
		}

		return true;
	}
}

/**
 * Shared helper method to set 'active' subset of triangles for a collection, to be considered for texturing
 * @return number of active triangles
 */
int32 SetActiveTriangles(FGeometryCollection* Collection, TArray<bool>& ActiveTrianglesOut,
	bool bActivateInsideTriangles,
	bool bOddMaterialAreInside, TArrayView<int32> WhichMaterialsAreInside)
{
	TSet<int32> InsideMaterials;
	for (int32 MatID : WhichMaterialsAreInside)
	{
		InsideMaterials.Add(MatID);
	}

	ActiveTrianglesOut.Init(false, Collection->Indices.Num());
	int32 NumTriangles = 0;
	for (int TID = 0; TID < Collection->Indices.Num(); TID++)
	{
		bool bIsActive = IsTriActive(Collection->Visible[TID], Collection->MaterialID[TID], InsideMaterials, bActivateInsideTriangles, bOddMaterialAreInside);
		if (bIsActive)
		{
			ActiveTrianglesOut[TID] = true;
			NumTriangles++;
		}
	}

	return NumTriangles;
}

// Adapter to use geometry collections in the UVPacker, and other GeometricObjects generic mesh processing code
// Note: Adapts the whole geometry collection as a single mesh, with triangles filtered by an ActiveTriangles mask
struct FGeomMesh : public UE::Geometry::FUVPacker::IUVMeshView
{
	FGeometryCollection* Collection;
	const TArrayView<bool> ActiveTriangles;
	int32 NumTriangles;
	TArray<FVector3d> GlobalVertices; // vertices transformed to global space
	TArray<FVector3f> GlobalNormals; // normals transformed to global space

	// Construct a mesh from a geometry collection and a mask of active triangles
	FGeomMesh(FGeometryCollection* Collection, TArrayView<bool> ActiveTriangles, int32 NumTriangles) 
		: Collection(Collection), ActiveTriangles(ActiveTriangles), NumTriangles(NumTriangles)
	{
		InitVertices();
	}

	// Construct a mesh from an existing FGeomMesh and a mask of active triangles
	FGeomMesh(const FGeomMesh& OtherMesh, TArrayView<bool> ActiveTriangles, int32 NumTriangles)
		: Collection(OtherMesh.Collection), ActiveTriangles(ActiveTriangles), NumTriangles(NumTriangles)
	{
		GlobalVertices = OtherMesh.GlobalVertices;
	}

	void InitVertices()
	{
		// transform all vertices from local to global space
		TArray<FTransform> GlobalTransformArray;
		GeometryCollectionAlgo::GlobalMatrices(Collection->Transform, Collection->Parent, GlobalTransformArray);
		GlobalVertices.SetNum(Collection->Vertex.Num());
		GlobalNormals.SetNum(Collection->Vertex.Num());
		for (int32 Idx = 0; Idx < GlobalVertices.Num(); Idx++)
		{
			GlobalNormals[Idx] = (FVector3f)GlobalTransformArray[Collection->BoneMap[Idx]].TransformVectorNoScale(Collection->Normal[Idx]);
			GlobalVertices[Idx] = (FVector3d)GlobalTransformArray[Collection->BoneMap[Idx]].TransformPosition(Collection->Vertex[Idx]);
		}
	}

	///
	/// The FUVPacker::IUVMeshView interface functions; this is the subset of functionality required for FUVPacker
	/// 

	virtual FIndex3i GetTriangle(int32 TID) const
	{
		return FIndex3i(Collection->Indices[TID]);
	}

	virtual FIndex3i GetUVTriangle(int32 TID) const
	{
		return FIndex3i(Collection->Indices[TID]);
	}

	virtual FVector3d GetVertex(int32 VID) const
	{
		return GlobalVertices[VID];
	}

	virtual FVector2f GetUV(int32 EID) const
	{
		return FVector2f(Collection->UV[EID]);
	}

	virtual void SetUV(int32 EID, FVector2f UVIn)
	{
		FVector2D& UV = Collection->UV[EID];
		UV.X = UVIn.X;
		UV.Y = UVIn.Y;
	}



	///
	/// Interface for templated mesh code, not part of the the IUVMeshView interface
	///
	inline bool IsTriangle(int32 TID) const
	{
		return ActiveTriangles[TID];
	}
	constexpr inline bool IsVertex(int32 VID) const
	{
		return true; // we don't have sparse vertices, so can always return true
	}
	inline int32 MaxTriangleID() const
	{
		return Collection->Indices.Num();
	}
	inline int32 MaxVertexID() const
	{
		return GlobalVertices.Num();
	}
	inline int32 TriangleCount() const
	{
		return NumTriangles;
	}
	inline int32 VertexCount() const
	{
		return GlobalVertices.Num();
	}
	constexpr inline int32 GetShapeTimestamp() const
	{
		return 0;
	}

	inline void GetTriVertices(int TID, UE::Math::TVector<double>& V0, UE::Math::TVector<double>& V1, UE::Math::TVector<double>& V2) const
	{
		FIntVector TriRaw = Collection->Indices[TID];

		V0 = GlobalVertices[TriRaw.X];
		V1 = GlobalVertices[TriRaw.Y];
		V2 = GlobalVertices[TriRaw.Z];
	}

	/// End of interfaces
	
	FVector3f GetInterpolatedNormal(int TID, const FVector3d& Bary)
	{
		FIntVector TriRaw = Collection->Indices[TID];
		FVector3f Normal = (FVector3f) (Bary.X * GlobalNormals[TriRaw.X] + Bary.Y * GlobalNormals[TriRaw.Y] + Bary.Z * GlobalNormals[TriRaw.Z]);
		return Normalized(Normal);
	}
};

/// Like FGeomMesh, but the UVs are given as the vertices rather than accessed separately
/// Used by the texture sampling code
struct FGeomFlatUVMesh
{
	FGeometryCollection* Collection;
	const TArrayView<bool> ActiveTriangles;
	int32 NumTriangles;

	FGeomFlatUVMesh(FGeometryCollection* Collection, TArrayView<bool> ActiveTriangles, int32 NumTriangles)
		: Collection(Collection), ActiveTriangles(ActiveTriangles), NumTriangles(NumTriangles)
	{}

	inline FIndex3i GetTriangle(int32 TID) const
	{
		return FIndex3i(Collection->Indices[TID]);
	}

	inline FVector3d GetVertex(int32 VID) const
	{
		const FVector2D& UV = Collection->UV[VID];
		return FVector3d(UV.X, UV.Y, 0);
	}

	inline bool IsTriangle(int32 TID) const
	{
		return ActiveTriangles[TID];
	}
	constexpr inline bool IsVertex(int32 VID) const
	{
		return true; // we don't have sparse vertices, so can always return true
	}
	inline int32 MaxTriangleID() const
	{
		return Collection->Indices.Num();
	}
	inline int32 MaxVertexID() const
	{
		return Collection->UV.Num();
	}
	inline int32 TriangleCount() const
	{
		return NumTriangles;
	}
	inline int32 VertexCount() const
	{
		return Collection->UV.Num();
	}
	constexpr inline int32 GetShapeTimestamp() const
	{
		return 0;
	}

	inline void GetTriVertices(int TID, UE::Math::TVector<double>& V0, UE::Math::TVector<double>& V1, UE::Math::TVector<double>& V2) const
	{
		FIntVector TriRaw = Collection->Indices[TID];

		V0 = GetVertex(TriRaw.X);
		V1 = GetVertex(TriRaw.Y);
		V2 = GetVertex(TriRaw.Z);
	}
};





bool UVLayout(
	FGeometryCollection& Collection,
	int32 UVRes,
	float GutterSize,
	bool bOnlyOddMaterials,
	TArrayView<int32> WhichMaterials,
	bool bRecreateUVsForDegenerateIslands
)
{
	TArray<bool> ActiveTriangles;
	int32 NumActive = SetActiveTriangles(&Collection, ActiveTriangles, true, bOnlyOddMaterials, WhichMaterials);
	FGeomMesh UVMesh(&Collection, ActiveTriangles, NumActive);

	TArray<TArray<int32>> UVIslands;
	UE::UVPacking::CreateUVIslandsFromMeshTopology<FGeomMesh>(UVMesh, UVIslands);

	if (bRecreateUVsForDegenerateIslands)
	{
		TArray<int> IslandVert; IslandVert.SetNumUninitialized(UVMesh.MaxVertexID());

		const int32 NumIslands = UVIslands.Num();
		for (int IslandIdx = 0; IslandIdx < NumIslands; IslandIdx++)
		{
			double UVArea = 0;
			FVector3d AvgNormal(0, 0, 0);
			FVector3d AnyPt(0, 0, 0);
			for (int TID : UVIslands[IslandIdx])
			{
				FIndex3i UVInds = UVMesh.GetUVTriangle(TID);
				FVector2f UVA = UVMesh.GetUV(UVInds.A), UVB = UVMesh.GetUV(UVInds.B), UVC = UVMesh.GetUV(UVInds.C);
				UVArea += (double)VectorUtil::Area(UVA, UVB, UVC);
				FVector3d VA, VB, VC;
				UVMesh.GetTriVertices(TID, VA, VB, VC);
				double Area;
				AnyPt = VA;
				FVector3d Normal = VectorUtil::NormalArea(VA, VB, VC, Area);
				AvgNormal += Area * Normal;
			}
			double AvgNormalLen = Normalize(AvgNormal);
			bool bHasValidNormal = AvgNormalLen > 0;
			bool bDoProjection = FMathd::Abs(UVArea) < FMathd::ZeroTolerance;
			if (bDoProjection)
			{
				// convert the island to a dynamic mesh so we can use the expmap UVs
				for (int VID = 0; VID < IslandVert.Num(); ++VID)
				{
					IslandVert[VID] = -1;
				}
				TArray<int> InvIslandVert; InvIslandVert.Reserve(3 * UVIslands[IslandIdx].Num());
				FDynamicMesh3 IslandMesh;
				for (int TID : UVIslands[IslandIdx])
				{
					FIndex3i Tri = UVMesh.GetUVTriangle(TID);
					FIndex3i NewTri = FIndex3i::Invalid();
					for (int SubIdx = 0; SubIdx < 3; SubIdx++)
					{
						int SrcVID = Tri[SubIdx];
						int& VID = IslandVert[SrcVID];
						if (VID == -1)
						{
							VID = IslandMesh.AppendVertex(UVMesh.GetVertex(SrcVID));
							check(InvIslandVert.Num() == VID);
							InvIslandVert.Add(SrcVID);
						}
						NewTri[SubIdx] = VID;
					}
					IslandMesh.AppendTriangle(NewTri);
				}
				FDynamicMeshUVEditor UVEditor(&IslandMesh, 0, true);
				TArray<int> Tris; Tris.Reserve(IslandMesh.TriangleCount());
				for (int TID : IslandMesh.TriangleIndicesItr())
				{
					Tris.Add(TID);
				}
				bool bExpMapSuccess = UVEditor.SetTriangleUVsFromExpMap(Tris);
				if (bExpMapSuccess)
				{
					// transfer UVs from the overlay
					FDynamicMeshUVOverlay* UVOverlay = IslandMesh.Attributes()->PrimaryUV();
					for (int IslandTID : IslandMesh.TriangleIndicesItr())
					{
						FIndex3i Tri = IslandMesh.GetTriangle(IslandTID);
						FIndex3i UVTri = UVOverlay->GetTriangle(IslandTID);
						if (!ensure(UVTri != FIndex3i::Invalid()))
						{
							// we shouldn't have unset tris like this if the ExpMap returned success
							bExpMapSuccess = false;
							break;
						}
						for (int SubIdx = 0; SubIdx < 3; SubIdx++)
						{
							int VID = InvIslandVert[Tri[SubIdx]];
							FVector2f UV = UVOverlay->GetElement(UVTri[SubIdx]);
							UVMesh.SetUV(VID, UV);
						}
					}
				}
				if (!bExpMapSuccess && bHasValidNormal)
				{
					// expmap failed; fall back to projecting UVs
					FFrame3d Frame(AnyPt, AvgNormal);
					for (int TID : UVIslands[IslandIdx])
					{
						FIndex3i Tri = UVMesh.GetUVTriangle(TID);
						for (int SubIdx = 0; SubIdx < 3; SubIdx++)
						{
							int VID = Tri[SubIdx];
							FVector2f UV = (FVector2f)Frame.ToPlaneUV(UVMesh.GetVertex(VID));
							UVMesh.SetUV(VID, UV);
						}
					}
				}
			}
		}
	}

	UE::Geometry::FUVPacker Packer;
	Packer.bScaleIslandsByWorldSpaceTexelRatio = true; // let packer scale islands separately to have consistent texel-to-world ratio
	Packer.bAllowFlips = false;
	// StandardPack doesn't support the Packer.GutterSize feature, and always tries to leave a 1 texel gutter.
	// To approximate a larger gutter, we tell it to consider a smaller output resolution --
	//  hoping that e.g. the UVs for a 1 pixel gutter at 256x256 are ~ a 2 pixel gutter at 512x512
	// TODO: If we make StandardPack support the GutterSize parameter, we can use that instead.
	Packer.TextureResolution = UVRes / FMathf::Max(1, GutterSize);
	return Packer.StandardPack(&UVMesh, UVIslands);
}


void TextureInternalSurfaces(
	FGeometryCollection& Collection,
	int32 GutterSize,
	FIndex4i BakeAttributes,
	const FTextureAttributeSettings& AttributeSettings,
	TImageBuilder<FVector4f>& TextureOut,
	bool bOnlyOddMaterials,
	TArrayView<int32> WhichMaterials
)
{
	TArray<bool> InsideTriangles;
	int32 NumInsideTris = SetActiveTriangles(&Collection, InsideTriangles, true, bOnlyOddMaterials, WhichMaterials);
	FGeomFlatUVMesh UVMesh(&Collection, InsideTriangles, NumInsideTris);

	FImageOccupancyMap OccupancyMap;
	OccupancyMap.GutterSize = GutterSize;
	OccupancyMap.Initialize(TextureOut.GetDimensions());
	OccupancyMap.ComputeFromUVSpaceMesh(UVMesh, [](int32 TriangleID) { return TriangleID; });

	FGeomMesh InsideMesh(&Collection, InsideTriangles, NumInsideTris);

	TArray<bool> OutsideTriangles;
	int32 NumOutsideTris = SetActiveTriangles(&Collection, OutsideTriangles, false, bOnlyOddMaterials, WhichMaterials);
	FGeomMesh OutsideMesh(InsideMesh, OutsideTriangles, NumOutsideTris);
	TMeshAABBTree3<FGeomMesh> OutsideSpatial(&OutsideMesh);

	int DistanceToExternalIdx = BakeAttributes.IndexOf((int)EBakeAttributes::DistanceToExternal);
	int AmbientIdx = BakeAttributes.IndexOf((int)EBakeAttributes::AmbientOcclusion);
	int CurvatureIdx = BakeAttributes.IndexOf((int)EBakeAttributes::Curvature);
	int NormalZIdx = BakeAttributes.IndexOf((int)EBakeAttributes::NormalZ);
	int PosZIdx = BakeAttributes.IndexOf((int)EBakeAttributes::PositionZ);
	
	if (OutsideSpatial.GetBoundingBox().Depth() == 0)
	{
		PosZIdx = -1; // if everything has same Z, can just leave PosZ values as default value
	}


	bool bNeedsDynamicMeshes = AmbientIdx > -1 || CurvatureIdx > -1;

	TArray<int32> TransformIndices;
	if (bNeedsDynamicMeshes)
	{
		for (int32 GeomIdx = 0; GeomIdx < Collection.TransformIndex.Num(); GeomIdx++)
		{
			TransformIndices.Add(Collection.TransformIndex[GeomIdx]);
		}
	}
	FDynamicMeshCollection CollectionMeshes(&Collection, TransformIndices, FTransform::Identity, false);
	if (bNeedsDynamicMeshes)
	{
		TSet<int32> InsideMaterials;
		for (int32 MatID : WhichMaterials)
		{
			InsideMaterials.Add(MatID);
		}

		// To bake only internal faces, unset texture on everything else
		for (int MeshIdx = 0; MeshIdx < CollectionMeshes.Meshes.Num(); MeshIdx++)
		{
			int32 TransformIdx = CollectionMeshes.Meshes[MeshIdx].TransformIndex;
			FDynamicMesh3& Mesh = CollectionMeshes.Meshes[MeshIdx].AugMesh;
			FMeshNormals::InitializeOverlayToPerVertexNormals(Mesh.Attributes()->PrimaryNormals(), true);
			AugmentedDynamicMesh::InitializeOverlayToPerVertexUVs(Mesh);
			FDynamicMeshUVOverlay* UV = Mesh.Attributes()->PrimaryUV();
			for (int TID : Mesh.TriangleIndicesItr())
			{
				bool bIsInsideTri = IsTriActive(
					AugmentedDynamicMesh::GetVisibility(Mesh, TID), 
					Mesh.Attributes()->GetMaterialID()->GetValue(TID), InsideMaterials, true, bOnlyOddMaterials);
				if (!bIsInsideTri)
				{
					UV->UnsetTriangle(TID);
				}
			}
		}
	}

	TArray64<float> CurvatureValues; // buffer to fill with curvature values
	if (CurvatureIdx > -1)
	{
		int UseVoxRes = FMath::Max(8, AttributeSettings.Curvature_VoxelRes);
		FImageDimensions Dimensions = TextureOut.GetDimensions();
		int64 TexelsNum = Dimensions.Num();
		CurvatureValues.SetNumZeroed(TexelsNum);
		for (int MeshIdx = 0; MeshIdx < CollectionMeshes.Meshes.Num(); MeshIdx++)
		{

			int32 TransformIdx = CollectionMeshes.Meshes[MeshIdx].TransformIndex;
			FDynamicMesh3& Mesh = CollectionMeshes.Meshes[MeshIdx].AugMesh;
			FDynamicMeshUVOverlay* UV = Mesh.Attributes()->PrimaryUV();

			FDynamicMeshAABBTree3 Spatial(&Mesh);
			TFastWindingTree<FDynamicMesh3> FastWinding(&Spatial);
			double ExtendBounds = Spatial.GetBoundingBox().MaxDim() * .01;

			TImplicitSolidify<FDynamicMesh3> Solidify(&Mesh, &Spatial, &FastWinding);
			Solidify.SetCellSizeAndExtendBounds(Spatial.GetBoundingBox(), ExtendBounds, UseVoxRes);
			Solidify.WindingThreshold = AttributeSettings.Curvature_Winding;
			Solidify.SurfaceSearchSteps = 3;
			Solidify.bSolidAtBoundaries = true;
			Solidify.ExtendBounds = ExtendBounds;

			FDynamicMesh3 SolidMesh(&Solidify.Generate());

			// Smooth mesh
			double SmoothAlpha = AttributeSettings.Curvature_SmoothingPerStep;
			TArray<FVector3d> PositionBuffer;
			PositionBuffer.SetNumUninitialized(SolidMesh.MaxVertexID());
			for (int VID : SolidMesh.VertexIndicesItr())
			{
				PositionBuffer[VID] = SolidMesh.GetVertex(VID);
			}
			UE::MeshDeformation::ComputeSmoothing_Forward(true, false, SolidMesh, [SmoothAlpha](int VID, bool bIsBoundary) { return SmoothAlpha; },
				AttributeSettings.Curvature_SmoothingSteps, PositionBuffer);
			for (int VID : SolidMesh.VertexIndicesItr())
			{
				SolidMesh.SetVertex(VID, PositionBuffer[VID]);
			}

			FDynamicMeshAABBTree3 SolidSpatial(&SolidMesh);

			FMeshImageBakingCache BakeCache;
			BakeCache.SetGutterSize(GutterSize);
			BakeCache.SetDetailMesh(&SolidMesh, &SolidSpatial);
			BakeCache.SetBakeTargetMesh(&Mesh);
			BakeCache.SetDimensions(Dimensions);
			BakeCache.SetUVLayer(0);
			// set distance to search for correspondences based on the voxel size
			double Thickness = AttributeSettings.Curvature_ThicknessFactor * Spatial.GetBoundingBox().MaxDim() / (float)UseVoxRes;
			BakeCache.SetThickness(Thickness);
			BakeCache.ValidateCache();

			FMeshCurvatureMapBaker CurvatureBaker;
			CurvatureBaker.bOverrideCurvatureRange = true;
			CurvatureBaker.OverrideRangeMax = AttributeSettings.Curvature_MaxValue;
			CurvatureBaker.UseColorMode = FMeshCurvatureMapBaker::EColorMode::BlackGrayWhite;
			CurvatureBaker.SetCache(&BakeCache);

			CurvatureBaker.BlurRadius = AttributeSettings.bCurvature_Blur ? AttributeSettings.Curvature_BlurRadius : 0.0;

			CurvatureBaker.Bake();

			const TUniquePtr<TImageBuilder<FVector3f>>& Result = CurvatureBaker.GetResult();
			// copy scalar curvature values out to the accumulated curvature buffer
			for (int64 LinearIdx = 0; LinearIdx < TexelsNum; LinearIdx++)
			{
				float& CurvatureOut = CurvatureValues[LinearIdx];
				CurvatureOut = FMathf::Max(CurvatureOut, Result->GetPixel(LinearIdx).X);
			}
		}
	}
	
	TArray64<float> AmbientValues; // buffer to fill with ambient occlusion values
	if (AmbientIdx > -1)
	{
		FImageDimensions Dimensions = TextureOut.GetDimensions();
		int64 TexelsNum = Dimensions.Num();
		AmbientValues.SetNumZeroed(TexelsNum);
		for (int MeshIdx = 0; MeshIdx < CollectionMeshes.Meshes.Num(); MeshIdx++)
		{
			int32 TransformIdx = CollectionMeshes.Meshes[MeshIdx].TransformIndex;
			FDynamicMesh3& Mesh = CollectionMeshes.Meshes[MeshIdx].AugMesh;
			FDynamicMeshUVOverlay* UV = Mesh.Attributes()->PrimaryUV();

			FDynamicMeshAABBTree3 Spatial(&Mesh);

			FMeshImageBakingCache BakeCache;
			BakeCache.SetGutterSize(GutterSize);
			BakeCache.SetDetailMesh(&Mesh, &Spatial);
			BakeCache.SetBakeTargetMesh(&Mesh);
			BakeCache.SetDimensions(Dimensions);
			BakeCache.SetUVLayer(0);
			// thickness is used for raycasting correspondences between detail and target mesh
			// TODO: make cache aware that detail mesh == target mesh so it doesn't have to do this?
			BakeCache.SetThickness(KINDA_SMALL_NUMBER);
			BakeCache.ValidateCache();

			FMeshOcclusionMapBaker OcclusionBaker;
			OcclusionBaker.SetCache(&BakeCache);
			
			OcclusionBaker.OcclusionType = EOcclusionMapType::AmbientOcclusion;
			OcclusionBaker.NumOcclusionRays = AttributeSettings.AO_Rays;
			OcclusionBaker.MaxDistance = AttributeSettings.AO_MaxDistance == 0 ? TNumericLimits<double>::Max() : AttributeSettings.AO_MaxDistance;
			OcclusionBaker.BiasAngleDeg = AttributeSettings.AO_BiasAngleDeg;
			OcclusionBaker.BlurRadius = AttributeSettings.bAO_Blur ? AttributeSettings.AO_BlurRadius : 0.0;

			OcclusionBaker.Bake();

			const TUniquePtr<TImageBuilder<FVector3f>>& Result = OcclusionBaker.GetResult(FMeshOcclusionMapBaker::EResult::AmbientOcclusion);
			// copy scalar ambient values out to the accumulated ambient buffer
			for (int64 LinearIdx = 0; LinearIdx < TexelsNum; LinearIdx++)
			{
				float& AmbientOut = AmbientValues[LinearIdx];
				AmbientOut = FMathf::Max(AmbientOut, Result->GetPixel(LinearIdx).X);
			}
		}
	}

	ParallelFor(OccupancyMap.Dimensions.GetHeight(),
		[&AttributeSettings, &TextureOut, &UVMesh, &OccupancyMap, &InsideMesh, &OutsideSpatial,
		 &DistanceToExternalIdx, &AmbientIdx, &NormalZIdx, &PosZIdx](int32 Y)
	{
		for (int32 X = 0; X < OccupancyMap.Dimensions.GetWidth(); X++)
		{
			int64 LinearCoord = OccupancyMap.Dimensions.GetIndex(X, Y);
			if (OccupancyMap.IsInterior(LinearCoord))
			{
				FVector4f OutColor = FVector4f::One();

				int32 TID = OccupancyMap.TexelQueryTriangle[LinearCoord];
				FVector2f UV = OccupancyMap.TexelQueryUV[LinearCoord];
				// Note this is the slowest way to get barycentric coordinates out of a point and a 2D triangle,
				//   ... but it's the one we currently have that's robust to (and gives good answers on) degenerate triangles
				FDistPoint3Triangle3d DistQuery = TMeshQueries<FGeomFlatUVMesh>::TriangleDistance(UVMesh, TID, FVector3d(UV.X, UV.Y, 0));
				FVector3d Bary = DistQuery.TriangleBaryCoords;
				FVector3f Normal(0, 0, 1);
				bool bNeedsNormal = NormalZIdx > -1 || AmbientIdx > -1;
				if (bNeedsNormal)
				{
					Normal = InsideMesh.GetInterpolatedNormal(TID, Bary);
				}

				if (DistanceToExternalIdx > -1 || PosZIdx > -1)
				{
					FTriangle3d Tri;
					InsideMesh.GetTriVertices(TID, Tri.V[0], Tri.V[1], Tri.V[2]);
					FVector3d InsidePoint = Tri.BarycentricPoint(Bary);

					if (DistanceToExternalIdx > -1)
					{
						double DistanceSq;
						OutsideSpatial.FindNearestTriangle(InsidePoint, DistanceSq, AttributeSettings.ToExternal_MaxDistance);
						float PercentDistance = FMathf::Min(1.0f, FMathf::Sqrt(DistanceSq) / (float)AttributeSettings.ToExternal_MaxDistance);
						checkSlow(FMath::IsFinite(PercentDistance));
						OutColor[DistanceToExternalIdx] = PercentDistance;
					}
					if (PosZIdx > -1)
					{
						double MinZ = OutsideSpatial.GetBoundingBox().Min.Z, MaxZ = OutsideSpatial.GetBoundingBox().Max.Z;
						checkSlow(MinZ != MaxZ);
						float PercentHeight = (float)(InsidePoint.Z - MinZ) / (MaxZ - MinZ);
						OutColor[PosZIdx] = PercentHeight;
					}
				}
				if (NormalZIdx > -1)
				{
					float NormalZ = Normal.Z;
					if (AttributeSettings.bNormalZ_TakeAbs)
					{
						NormalZ = FMathf::Abs(NormalZ);
					}
					else
					{
						NormalZ = (1 + NormalZ) * .5; // compress from [-1,1] to [0,1] range
					}
					OutColor[NormalZIdx] = NormalZ;
				}

				TextureOut.SetPixel(LinearCoord, OutColor);
			}
		}
	});

	auto CopyValuesToChannel = [&TextureOut, &OccupancyMap](int ChannelIdx, TArray64<float>& Values)
	{
		if (ChannelIdx > -1)
		{
			int64 TexelsNum = OccupancyMap.Dimensions.Num();
			for (int64 LinearIdx = 0; LinearIdx < TexelsNum; LinearIdx++)
			{
				FVector4f Pixel = TextureOut.GetPixel(LinearIdx);
				Pixel[ChannelIdx] = Values[LinearIdx];
				TextureOut.SetPixel(LinearIdx, Pixel);
			}
		}
	};

	CopyValuesToChannel(AmbientIdx, AmbientValues);
	CopyValuesToChannel(CurvatureIdx, CurvatureValues);

	for (TTuple<int64, int64>& GutterToInside : OccupancyMap.GutterTexels)
	{
		TextureOut.CopyPixel(GutterToInside.Value, GutterToInside.Key);
	}
}

}} // namespace UE::PlanarCut