// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/ChaosFleshBindingsNodes.h"

#include "Chaos/AABBTree.h"
#include "Chaos/BoundingVolumeHierarchy.h"
#include "Chaos/Tetrahedron.h"
#include "Chaos/TriangleMesh.h"
#include "ChaosFlesh/TetrahedralCollection.h"
#include "Engine/StaticMesh.h"
#include "Dataflow/DataflowInputOutput.h"
#include "Engine/SkeletalMesh.h"
#include "Dataflow/DataflowNodeFactory.h"
#include "GeometryCollection/Facades/CollectionTetrahedralBindingsFacade.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "UObject/PrimaryAssetId.h"

namespace Dataflow
{
	void ChaosFleshBindingsNodes()
	{
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FGenerateBindings);
	}
}

void
FGenerateBindings::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<DataType>(&Collection))
	{
		DataType InCollection = GetValue<DataType>(Context, &Collection); // Deep copy

		TManagedArray<FIntVector4>* Tetrahedron = 
			InCollection.FindAttribute<FIntVector4>(
				FTetrahedralCollection::TetrahedronAttribute, FTetrahedralCollection::TetrahedralGroup);
		TManagedArray<int32>* TetrahedronStart =
			InCollection.FindAttribute<int32>(
				"TetrahedronStart", FGeometryCollection::GeometryGroup);
		TManagedArray<int32>* TetrahedronCount =
			InCollection.FindAttribute<int32>(
				"TetrahedronCount", FGeometryCollection::GeometryGroup);

		TManagedArray<FIntVector>* Triangle =
			InCollection.FindAttribute<FIntVector>(
				"Indices", FGeometryCollection::FacesGroup);
		TManagedArray<int32>* FacesStart =
			InCollection.FindAttribute<int32>(
				"FaceStart", FGeometryCollection::GeometryGroup);
		TManagedArray<int32>* FacesCount =
			InCollection.FindAttribute<int32>(
				"FaceCount", FGeometryCollection::GeometryGroup);

		TManagedArray<FVector3f>* Vertex = 
			InCollection.FindAttribute<FVector3f>(
				"Vertex", "Vertices");

		TObjectPtr<const USkeletalMesh> SkeletalMesh = GetValue<TObjectPtr<const USkeletalMesh>>(Context, &SkeletalMeshIn);
		TObjectPtr<const UStaticMesh> StaticMesh = GetValue<TObjectPtr<const UStaticMesh>>(Context, &StaticMeshIn);
		if ((SkeletalMesh || StaticMesh) && Tetrahedron && Triangle && Vertex)
		{
			// Extract positions to bind
			FString MeshId;
			TArray<TArray<FVector3f>> MeshVertices;
			if (SkeletalMesh)
			{
				FPrimaryAssetId Id = SkeletalMesh->GetPrimaryAssetId();
				if (Id.IsValid())
				{
					MeshId = Id.ToString();
				}
				else
				{
					MeshId = SkeletalMesh->GetName();
				}

				FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();

				MeshVertices.SetNum(RenderData->LODRenderData.Num());
				for (int32 i = 0; i < RenderData->LODRenderData.Num(); i++)
				{
					FSkeletalMeshLODRenderData* LODRenderData = &RenderData->LODRenderData[i];
					const FPositionVertexBuffer& PositionVertexBuffer =
						LODRenderData->StaticVertexBuffers.PositionVertexBuffer;

					TArray<FVector3f>& Vertices = MeshVertices[i];
					Vertices.SetNumUninitialized(PositionVertexBuffer.GetNumVertices());
					for (uint32 j = 0; j < PositionVertexBuffer.GetNumVertices(); j++)
					{
						const FVector3f& Pos = PositionVertexBuffer.VertexPosition(j);
						Vertices[j] = Pos;
					}
				}
			}
			else // StaticMesh
			{
				//StaticMesh->GetMeshId(MeshId); // not available at runtime!
				//MeshId = RenderData->DerivedDataKey; // same problem
				FPrimaryAssetId Id = StaticMesh->GetPrimaryAssetId();
				if (Id.IsValid())
				{
					MeshId = Id.ToString();
				}
				else
				{
					MeshId = StaticMesh->GetName();
				}

				const FStaticMeshRenderData* RenderData = StaticMesh->GetRenderData();
				const int32 NumLOD = RenderData->LODResources.Num();
				MeshVertices.SetNum(NumLOD);
				for (int32 i = 0; i < NumLOD; i++)
				{
					const FStaticMeshLODResources& LODResources = RenderData->LODResources[i];
					const FPositionVertexBuffer& PositionVertexBuffer = LODResources.VertexBuffers.PositionVertexBuffer;
					
					TArray<FVector3f>& Vertices = MeshVertices[i];
					Vertices.SetNumUninitialized(PositionVertexBuffer.GetNumVertices());
					for (uint32 j = 0; j < PositionVertexBuffer.GetNumVertices(); j++)
					{
						const FVector3f& Pos = PositionVertexBuffer.VertexPosition(j);
						Vertices[j] = Pos;
					}
				}

/*				// Geom lib does conversion like this:
				const FMeshDescription* MeshDescription =
					StaticMesh->IsHiResMeshDescriptionValid() ?
					StaticMesh->GetHiResMeshDescription() :
					StaticMesh->GetMeshDescription(0);
				TVertexAttributesConstRef<const FVector3f> VertexPositions = MeshDescription->GetVertexPositions();
				//TVertexAttributesConstRef<FVector3f> VertexPositions = MeshDescription->GetVertexPositions();

				MeshVertices.SetNum(1);
				MeshVertices[0].Reserve(MeshDescription->Vertices().Num());
				for (const FVertexID VertexID : MeshDescription->Vertices().GetElementIDs())
				{
					const FVector3f Pos = VertexPositions.Get(VertexID);
					MeshVertices[0].Add(Pos);
				}
*/
			}

			for (int32 TetMeshIdx = 0; TetMeshIdx < TetrahedronStart->Num(); TetMeshIdx++)
			{
				const int32 TetMeshStart = (*TetrahedronStart)[TetMeshIdx];
				const int32 TetMeshCount = (*TetrahedronCount)[TetMeshIdx];

				// Build Tetrahedra
				TArray<Chaos::TTetrahedron<Chaos::FReal>> Tets;
				TArray<Chaos::TTetrahedron<Chaos::FReal>*> BVHTetPtrs;
				Tets.SetNumUninitialized(TetMeshCount);
				BVHTetPtrs.SetNumUninitialized(TetMeshCount);
				for (int32 i = TetMeshStart; i < TetMeshStart + TetMeshCount; i++)
				{
					const FIntVector4& Tet = (*Tetrahedron)[i];
					Tets[i] = Chaos::TTetrahedron<Chaos::FReal>(
						(*Vertex)[Tet[0]],
						(*Vertex)[Tet[1]],
						(*Vertex)[Tet[2]],
						(*Vertex)[Tet[3]]);
					BVHTetPtrs[i] = &Tets[i];
				}

				// Init BVH for tetrahedra.
				Chaos::TBoundingVolumeHierarchy<
					TArray<Chaos::TTetrahedron<Chaos::FReal>*>, 
					TArray<int32>, 
					Chaos::FReal, 
					3> TetBVH(BVHTetPtrs);

				//
				// Init boundary mesh for projections.
				//

				const int32 TriMeshStart = (*FacesStart)[TetMeshIdx];
				const int32 TriMeshCount = (*FacesCount)[TetMeshIdx];
				Chaos::FTriangleMesh TetBoundaryMesh;
				if (TriMeshCount == Triangle->GetConstArray().Num())
				{
					TetBoundaryMesh.Init(
						reinterpret_cast<const TArray<Chaos::TVec3<int32>>&>(Triangle->GetConstArray()), 0, -1, false);
				}
				else
				{
					TArray<Chaos::TVec3<int32>> Faces; Faces.SetNumUninitialized(TriMeshCount);
					for (int32 i = TriMeshStart; i < TriMeshStart + TriMeshCount; i++)
					{
						Faces[i] = reinterpret_cast<const Chaos::TVec3<int32>&>(Triangle->GetConstArray()[i]);
					}
					TetBoundaryMesh.Init(Faces, 0, -1, false);
				}
				
				// Promote vertices to double because that's what FTriangleMesh wants.
				TArray<Chaos::FVec3> VertexD; VertexD.SetNumUninitialized(Vertex->Num());
				for (int32 i = 0; i < VertexD.Num(); i++)
				{
					VertexD[i] = Chaos::FVec3((*Vertex)[i][0], (*Vertex)[i][1], (*Vertex)[i][2]);
				}
				TConstArrayView<Chaos::TVec3<Chaos::FRealDouble>> VertexDView(VertexD);
				TArray<Chaos::FVec3> PointNormals = TetBoundaryMesh.GetPointNormals(VertexDView, false, true);

				Chaos::FTriangleMesh::TBVHType<Chaos::FRealDouble> TetBoundaryBVH;
				//TetBoundaryMesh.BuildBVH_(VertexDView, TetBoundaryBVH);
				TetBoundaryMesh.BuildBVH(VertexDView, TetBoundaryBVH);

				//
				// Do intersection tests against tets, then the surface.
				//

				TArray<TArray<FIntVector4>> Parents; Parents.SetNum(MeshVertices.Num());
				TArray<TArray<FVector4f>> Weights;	 Weights.SetNum(MeshVertices.Num());
				TArray<TArray<FVector3f>> Offsets;	 Offsets.SetNum(MeshVertices.Num());
				TArray<int32> Orphans;
				for (int32 LOD = 0; LOD < MeshVertices.Num(); LOD++)
				{
					Parents[LOD].SetNumUninitialized(MeshVertices[LOD].Num());
					Weights[LOD].SetNumUninitialized(MeshVertices[LOD].Num());
					Offsets[LOD].SetNumUninitialized(MeshVertices[LOD].Num());

					TArray<int32> TetIntersections; TetIntersections.Reserve(64);
					for (int32 i = 0; i < MeshVertices[LOD].Num(); i++)
					{
						Parents[LOD][i] = FIntVector4(INDEX_NONE);
						Weights[LOD][i] = FVector4f(0);
						Offsets[LOD][i] = FVector3f(0);

						const FVector3f& Pos = MeshVertices[LOD][i];
						TetIntersections = TetBVH.FindAllIntersections(Chaos::TVec3<Chaos::FReal>(Pos[0], Pos[1], Pos[2]));
						int32 j = 0;
						for (; j < TetIntersections.Num(); j++)
						{
							const int32 TetIdx = TetIntersections[j];
							if (Tets[TetIdx].RobustInside(Pos, -1.0e-4))
							{
								Parents[LOD][i] = (*Tetrahedron)[TetIdx];
								Chaos::TVector<Chaos::FReal, 4> WeightsD = Tets[TetIdx].GetBarycentricCoordinates(Pos);
								Weights[LOD][i] = FVector4f(WeightsD[0], WeightsD[1], WeightsD[2], WeightsD[3]);
								Offsets[LOD][i] = FVector3f(0);
								break;
							}
						}
						if (j == TetIntersections.Num())
						{
							// This vertex didn't land inside any tetrahedra. Project it to the tet boundary surface.
							int32 TriIdx = INDEX_NONE;
							Chaos::FVec3 TriWeights;
							//if (TetBoundaryMesh.SmoothProject_(
							if (TetBoundaryMesh.SmoothProject(
								TetBoundaryBVH,
								VertexDView,
								PointNormals, Pos,
								TriIdx, TriWeights, SurfaceProjectionIterations))
							{
								const FIntVector& Tri = (*Triangle)[TriIdx];
								Parents[LOD][i][0] = Tri[0];
								Parents[LOD][i][1] = Tri[1];
								Parents[LOD][i][2] = Tri[2];
								Parents[LOD][i][3] = INDEX_NONE;

								Weights[LOD][i][0] = TriWeights[0];
								Weights[LOD][i][1] = TriWeights[1];
								Weights[LOD][i][2] = TriWeights[2];
								Weights[LOD][i][3] = 0.0;

								const FVector3f EmbeddedPos =
									TriWeights[0] * Vertex->GetConstArray()[Tri[0]] +
									TriWeights[1] * Vertex->GetConstArray()[Tri[1]] +
									TriWeights[2] * Vertex->GetConstArray()[Tri[2]];
								Offsets[LOD][i] = Pos - EmbeddedPos;
							}
							else
							{
								// Despair...
								Orphans.Add(i);

								Parents[LOD][i][0] = INDEX_NONE;
								Parents[LOD][i][1] = INDEX_NONE;
								Parents[LOD][i][2] = INDEX_NONE;
								Parents[LOD][i][3] = INDEX_NONE;

								Weights[LOD][i][0] = 0.0;
								Weights[LOD][i][1] = 0.0;
								Weights[LOD][i][2] = 0.0;
								Weights[LOD][i][3] = 0.0;

								Offsets[LOD][i][0] = 0.0;
								Offsets[LOD][i][1] = 0.0;
								Offsets[LOD][i][2] = 0.0;
							}
						}
					} // end for all vertices
				} // end for all LOD

				// Stash bindings in the geometry collection
				GeometryCollection::Facades::FTetrahedralBindings TetBindings(InCollection);
				TetBindings.DefineSchema();
				FName MeshName(*MeshId, MeshId.Len());
				for (int32 LOD = 0; LOD < MeshVertices.Num(); LOD++)
				{
					TetBindings.AddBindingsGroup(TetMeshIdx, MeshName, LOD);
					TetBindings.SetBindingsData(Parents[LOD], Weights[LOD], Offsets[LOD]);
				}
			} // end for TetMeshIdx
		}
		SetValue<DataType>(Context, InCollection, &Collection);
	}
}
