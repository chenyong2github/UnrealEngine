// Copyright Epic Games, Inc. All Rights Reserved.

#include "NearestNeighborGeomCacheSampler.h"
#include "NearestNeighborModel.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "GeometryCacheComponent.h"
#include "GeometryCache.h"
#include "GeometryCacheMeshData.h"
#include "GeometryCacheTrack.h"

using namespace UE::MLDeformer;
namespace UE::NearestNeighborModel
{
	uint8 FNearestNeighborGeomCacheSampler::SamplePart(int32 InAnimFrameIndex, int32 PartId)
	{
		FMLDeformerSampler::Sample(InAnimFrameIndex);
		USkeletalMesh* SkeletalMesh = SkeletalMeshComponent.Get() ? SkeletalMeshComponent->GetSkeletalMeshAsset() : nullptr;
		UGeometryCache* GeometryCache = GeometryCacheComponent.Get() ? GeometryCacheComponent->GetGeometryCache() : nullptr;
		if (SkeletalMeshComponent && SkeletalMesh && GeometryCacheComponent && GeometryCache)
		{
			const FTransform& AlignmentTransform = Model->GetAlignmentTransform();
			FSkeletalMeshModel* ImportedModel = SkeletalMesh->GetImportedModel();
			const int32 LODIndex = 0;
			const FSkeletalMeshLODModel& LODModel = ImportedModel->LODModels[LODIndex];
			const TArray<FSkelMeshImportedMeshInfo>& SkelMeshInfos = LODModel.ImportedMeshInfos;

			if (HasError(CheckMeshMappingsEmpty()))
			{
				return EUpdateResult::ERROR;
			}

			if (PartId >= MeshMappingIndices.Num())
			{
				UE_LOG(LogNearestNeighborModel, Error, TEXT("SamplePart: MeshMappingIndices.Num()=%d is smaller than PartId %d"), MeshMappingIndices.Num(), PartId);
				return EUpdateResult::ERROR;
			}
			const UE::MLDeformer::FMLDeformerGeomCacheMeshMapping& MeshMapping = MeshMappings[MeshMappingIndices[PartId]]; 

			check(SkelMeshInfos.Num() > MeshMapping.MeshIndex);
			const FSkelMeshImportedMeshInfo& MeshInfo = SkelMeshInfos[MeshMapping.MeshIndex]; 
			check(MeshInfo.StartImportedVertex == 0);

			check(GeometryCache->Tracks.Num() > MeshMapping.TrackIndex);
			UGeometryCacheTrack* Track = GeometryCache->Tracks[MeshMapping.TrackIndex];
			GeomCacheMeshDatas.Reset(1);
			GeomCacheMeshDatas.AddDefaulted(1);
			FGeometryCacheMeshData& GeomCacheMeshData = GeomCacheMeshDatas[0];

			if (!Track->GetMeshDataAtTime(SampleTime, GeomCacheMeshData))
			{
				UE_LOG(LogNearestNeighborModel, Error, TEXT("SamplePart: Track cannot get mesh delta at frame %d"), InAnimFrameIndex);
				return EUpdateResult::ERROR;
			}

			// Calculate the vertex deltas.
			const FSkeletalMeshLODRenderData& SkelMeshLODData = SkeletalMesh->GetResourceForRendering()->LODRenderData[LODIndex];
			const FSkinWeightVertexBuffer& SkinWeightBuffer = *SkeletalMeshComponent->GetSkinWeightBuffer(LODIndex);

			UNearestNeighborModel* NearestNeighborModel = static_cast<UNearestNeighborModel*>(Model);
			check(NearestNeighborModel != nullptr);
			const TArray<uint32>& VertexMap = NearestNeighborModel->PartVertexMap(PartId);
			const int32 NumPartVerts = VertexMap.Num();
			PartVertexDeltas.Reset();
			PartVertexDeltas.SetNum(NumPartVerts * 3);

			for(int32 PartVertexIndex = 0; PartVertexIndex < NumPartVerts; PartVertexIndex++)
			{
				const int32 VertexIndex = VertexMap[PartVertexIndex];
				const int32 GeomCacheVertexIndex = MeshMapping.SkelMeshToTrackVertexMap[PartVertexIndex];

				if (GeomCacheVertexIndex != INDEX_NONE && GeomCacheMeshData.Positions.IsValidIndex(GeomCacheVertexIndex))
				{
					FVector3f Delta = FVector3f::ZeroVector;

					const int32 ArrayIndex = 3 * PartVertexIndex;
					// Calculate the inverse skinning transform for this vertex.
					const int32 RenderVertexIndex = MeshMapping.ImportedVertexToRenderVertexMap[PartVertexIndex];
					if (RenderVertexIndex != INDEX_NONE)
					{
						const FMatrix44f InvSkinningTransform = CalcInverseSkinningTransform(RenderVertexIndex, SkelMeshLODData, SkinWeightBuffer);

						// Calculate the pre-skinning data.
						const FVector3f UnskinnedPosition = SkelMeshLODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(RenderVertexIndex);
						const FVector3f GeomCacheVertexPos = (FVector3f)AlignmentTransform.TransformPosition((FVector)GeomCacheMeshData.Positions[GeomCacheVertexIndex]);
						const FVector3f PreSkinningTargetPos = InvSkinningTransform.TransformPosition(GeomCacheVertexPos);
						Delta = PreSkinningTargetPos - UnskinnedPosition;
					}

					PartVertexDeltas[ArrayIndex] = Delta.X;
					PartVertexDeltas[ArrayIndex + 1] = Delta.Y;
					PartVertexDeltas[ArrayIndex + 2] = Delta.Z;
				}
			}
		}
		return EUpdateResult::SUCCESS;
	}

	bool IsPotentialMatch(const FString& TrackName, const FString& MeshName)
	{
		return (TrackName.Find(MeshName) == 0);
	}

	uint8 FNearestNeighborGeomCacheSampler::GeneratePartMeshMappings(const TArray<uint32>& VertexMap, bool bUsePartOnlyMesh)
	{
		uint8 Result = EUpdateResult::SUCCESS;
		USkeletalMesh* SkeletalMesh = SkeletalMeshComponent.Get() ? SkeletalMeshComponent->GetSkeletalMeshAsset() : nullptr;
		UGeometryCache* GeometryCache = GeometryCacheComponent.Get() ? GeometryCacheComponent->GetGeometryCache() : nullptr;
		if (SkeletalMeshComponent && SkeletalMesh && GeometryCacheComponent && GeometryCache)
		{
			if (!bUsePartOnlyMesh)
			{
				TArray<FString> FailedNames;
				TArray<FString> VertexMisMatchNames;
				GenerateGeomCacheMeshMappings(SkeletalMesh, GeometryCache, MeshMappings, FailedNames, VertexMisMatchNames);
				Result |= GenerateMeshMappingIndices();
				if (!FailedNames.IsEmpty() || !VertexMisMatchNames.IsEmpty())
				{
					Result |= EUpdateResult::WARNING;
				}
				for(int32 i = 0; i < VertexMisMatchNames.Num(); i++)
				{
					UE_LOG(LogNearestNeighborModel, Warning, TEXT("%s is skipped because it has different vertex counts in skeletal mesh and geometry cache."), *VertexMisMatchNames[i]);
				}
				GeomCacheMeshDatas.Reset();
				GeomCacheMeshDatas.AddDefaulted(MeshMappings.Num());

				Result |= CheckMeshMappingsEmpty();
				return Result;
			}
			FSkeletalMeshModel* ImportedModel = SkeletalMesh->GetImportedModel();

			if (!ImportedModel || ImportedModel->LODModels[0].ImportedMeshInfos.IsEmpty())
			{
				UE_LOG(LogNearestNeighborModel, Error, TEXT("Unable to generate mesh mappings because SkeletalMesh has no imported model."));
				return EUpdateResult::ERROR;
			}

			const TArray<FSkelMeshImportedMeshInfo>& SkelMeshInfos = ImportedModel->LODModels[0].ImportedMeshInfos;
			MeshMappings.Reset();

			FString SkelMeshName;
			const bool bIsSoloMesh = (GeometryCache->Tracks.Num() == 1 && SkelMeshInfos.Num() == 1);	// Do we just have one mesh and one track?

			for (int32 TrackIndex = 0; TrackIndex < GeometryCache->Tracks.Num(); ++TrackIndex)
			{
				// Check if this is a candidate based on the mesh and track name.
				UGeometryCacheTrack* Track = GeometryCache->Tracks[TrackIndex];

				bool bFoundMatch = false;
				for (int32 SkelMeshIndex = 0; SkelMeshIndex < SkelMeshInfos.Num(); ++SkelMeshIndex)
				{
					const FSkelMeshImportedMeshInfo& MeshInfo = SkelMeshInfos[SkelMeshIndex];

					SkelMeshName = MeshInfo.Name.ToString();
					if (Track &&
						(IsPotentialMatch(Track->GetName(), SkelMeshName) || bIsSoloMesh))
					{
						// Extract the geom cache mesh data.
						FGeometryCacheMeshData GeomCacheMeshData;
						if (!Track->GetMeshDataAtTime(SampleTime, GeomCacheMeshData))
						{
							continue;
						}

						// Verify that we have imported vertex numbers.
						if (GeomCacheMeshData.ImportedVertexNumbers.IsEmpty())
						{
							UE_LOG(LogNearestNeighborModel, Warning, TEXT("Geometry track %s is skipped because it has no imported vertex numbers."), *Track->GetName());
							continue;
						}

						const int32 NumVertsFromGeomCache = FMath::Max(GeomCacheMeshData.ImportedVertexNumbers) + 1;
						const int32 NumVertsFromVertexMap = VertexMap.Num();
						Result |= CheckGeomCacheVertCount(NumVertsFromGeomCache, NumVertsFromVertexMap);
						if (HasError(Result))
						{
							return Result;
						}

						// Create a new mesh mapping entry.
						MeshMappings.AddDefaulted();
						UE::MLDeformer::FMLDeformerGeomCacheMeshMapping& Mapping = MeshMappings.Last();
						Mapping.MeshIndex = SkelMeshIndex;
						Mapping.TrackIndex = TrackIndex;

						const int32 NumPartVerts = VertexMap.Num();
						Mapping.SkelMeshToTrackVertexMap.AddUninitialized(NumPartVerts);
						Mapping.ImportedVertexToRenderVertexMap.AddUninitialized(NumPartVerts);


						for(int32 PartVertexIndex = 0; PartVertexIndex < NumPartVerts; PartVertexIndex++)
						{
							Mapping.SkelMeshToTrackVertexMap[PartVertexIndex] = GeomCacheMeshData.ImportedVertexNumbers.Find(PartVertexIndex);
							const int32 VertexIndex = VertexMap[PartVertexIndex];
							const int32 RenderVertexIndex = ImportedModel->LODModels[0].MeshToImportVertexMap.Find(VertexIndex);
							Mapping.ImportedVertexToRenderVertexMap[PartVertexIndex] = RenderVertexIndex;
						}

						// We found a match, no need to iterate over more MeshInfos.
						bFoundMatch = true;
						break;
					} // If the track name matches the skeletal meshes internal mesh name.
				} // For all meshes in the Skeletal Mesh.

				if (Track && !bFoundMatch)
				{
					Result |= EUpdateResult::WARNING;
					UE_LOG(LogNearestNeighborModel, Warning, TEXT("Geometry cache '%s' cannot be matched with a mesh inside the Skeletal Mesh."), *Track->GetName());
				}
			} // For all tracks.
		}
		else
		{
			Result |= EUpdateResult::WARNING;
			UE_LOG(LogNearestNeighborModel, Warning, TEXT("SkeletalMesh or GeometryCache is none. No mapping is generated"));
		}
		Result |= CheckMeshMappingsEmpty();
		Result |= GenerateMeshMappingIndices();
		return Result;
	}

	uint8 FNearestNeighborGeomCacheSampler::CheckGeomCacheVertCount(int32 NumVertsFromGeomCache, int32 NumVertsFromVertexMap) const
	{
		uint8 ResultCode = EUpdateResult::SUCCESS;		
		UNearestNeighborModel* NearestNeighborModel = static_cast<UNearestNeighborModel*>(Model);
		if (NearestNeighborModel->GetUsePartOnlyMesh())
		{
			if (NumVertsFromGeomCache != NumVertsFromVertexMap)
			{
				UE_LOG(LogNearestNeighborModel, Error, TEXT("Vertex count mismatch: GeomCache has %d vertices but vertex map has %d vertices. Maybe turn off UsePartOnlyMesh."), NumVertsFromGeomCache, NumVertsFromVertexMap);
				return EUpdateResult::ERROR;
			}
		}
		else
		{
			if (NumVertsFromVertexMap > NumVertsFromGeomCache)
			{
				UE_LOG(LogNearestNeighborModel, Error, TEXT("Vertex count in vertex map %d is larger than the vertex count in geometry cache %d. Something is wrong with vertex map or geometry cache"), NumVertsFromVertexMap, NumVertsFromGeomCache);
				return EUpdateResult::ERROR;
			}
		}
		return ResultCode;
	}

	uint8 FNearestNeighborGeomCacheSampler::CheckMeshMappingsEmpty() const
	{
		if(MeshMappings.IsEmpty())
		{
			UE_LOG(LogNearestNeighborModel, Error, TEXT("MeshMappings is empty. Unable to match skeletal mesh with geometry cache."));
			return EUpdateResult::ERROR;
		}
		else
		{
			return EUpdateResult::SUCCESS;
		}
	}

	bool FNearestNeighborGeomCacheSampler::SampleKMeansAnim(const int32 AnimId)
	{
		UNearestNeighborModel* NearestNeighborModel = static_cast<UNearestNeighborModel*>(Model);
		if (NearestNeighborModel && AnimId < NearestNeighborModel->SourceAnims.Num() && SkeletalMeshComponent)
		{
			const TObjectPtr<UAnimSequence> AnimSequence = NearestNeighborModel->SourceAnims[AnimId];
			SkeletalMeshComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode);
			SkeletalMeshComponent->SetAnimation(AnimSequence);
			SkeletalMeshComponent->SetPosition(0.0f);
			SkeletalMeshComponent->SetPlayRate(1.0f);
			SkeletalMeshComponent->Play(false);
			SkeletalMeshComponent->RefreshBoneTransforms();
			KMeansAnimId = AnimId;
			return true;
		}
		else
		{
			return false;
		}
	}

	// Write a function to get the animation of a skeletal mesh component

	bool FNearestNeighborGeomCacheSampler::SampleKMeansFrame(const int32 Frame)
	{
		const USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->GetSkeletalMeshAsset();
		if (SkeletalMeshComponent && SkeletalMesh)
		{
			UNearestNeighborModel* NearestNeighborModel = static_cast<UNearestNeighborModel*>(Model);
			const UAnimSequence* AnimSequence = NearestNeighborModel->SourceAnims[KMeansAnimId];
			if (NearestNeighborModel->GetSkeletalMesh() == nullptr)
			{
				UE_LOG(LogNearestNeighborModel, Error, TEXT("SkeletalMesh is nullptr. Unable to sample KMeans frame."));
				return false;
			}

			if (AnimSequence)
			{
				if (Frame < AnimSequence->GetDataModel()->GetNumberOfKeys())
				{
					AnimFrameIndex = Frame;
					SampleTime = GetTimeAtFrame(Frame);

					UpdateSkeletalMeshComponent();
					UpdateBoneRotations();
					UpdateCurveValues();
					return true;
				}
				else
				{
					UE_LOG(LogNearestNeighborModel, Error, TEXT("AnimSequence only has %d keys, but being sampled with key %d"), AnimSequence->GetDataModel()->GetNumberOfKeys(), Frame);
					return false;
				}
			}
			else
			{
				UE_LOG(LogNearestNeighborModel, Error, TEXT("AnimSequence %d is nullptr. Unable to sample KMeans frame."), KMeansAnimId);
				return false;
			}

		}
		else
		{
			UE_LOG(LogNearestNeighborModel, Error, TEXT("KMeans: SkeletalMesh does not exist"));
		}
		return false;
	}

	uint8 FNearestNeighborGeomCacheSampler::GenerateMeshMappingIndices()
	{
		UNearestNeighborModel* NearestNeighborModel = static_cast<UNearestNeighborModel*>(Model);
		if (NearestNeighborModel)
		{
			const int32 NumParts = NearestNeighborModel->GetNumParts();
			MeshMappingIndices.SetNum(NumParts);
			for (int32 PartId = 0; PartId < NumParts; PartId++)
			{
				bool bFoundIndex = false;
				for (int32 MappingId = 0; MappingId < MeshMappings.Num(); MappingId++)
				{
					if (MeshMappings[MappingId].MeshIndex == NearestNeighborModel->GetPartMeshIndex(PartId))
					{
						MeshMappingIndices[PartId] = MappingId;
						bFoundIndex = true;
						break;
					}
				}
				if (!bFoundIndex)
				{
					UE_LOG(LogNearestNeighborModel, Error, TEXT("Part %d could not find a mesh mapping."), PartId);
					return EUpdateResult::ERROR;
				}
			}
		}
		return EUpdateResult::SUCCESS;
	}

	TArray<uint32> FNearestNeighborGeomCacheSampler::GetMeshIndexBuffer() const
	{
		TArray<uint32> IndexBuffer;

		if (SkeletalMeshComponent == nullptr)
		{
			return IndexBuffer;
		}

		USkeletalMesh* Mesh = SkeletalMeshComponent->GetSkeletalMeshAsset();
		if (Mesh == nullptr)
		{
			return IndexBuffer;
		}

		constexpr int32 LODIndex = 0;
		const FSkeletalMeshLODRenderData& SkelMeshLODData = Mesh->GetResourceForRendering()->LODRenderData[LODIndex];
		SkelMeshLODData.MultiSizeIndexContainer.GetIndexBuffer(IndexBuffer);

		const FSkeletalMeshModel* SkeletalMeshModel = Mesh->GetImportedModel();
		const TArray<int32>& ImportedVertexNumbers = SkeletalMeshModel->LODModels[LODIndex].MeshToImportVertexMap;
		
		if (ImportedVertexNumbers.Num() > 0)
		{
			for (int32 Index = 0; Index < IndexBuffer.Num(); Index++)
			{
				IndexBuffer[Index] = ImportedVertexNumbers[IndexBuffer[Index]];
			}
		}

		return IndexBuffer;
	}
};
