// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCO/UnrealConversionUtils.h"

#include "Animation/Skeleton.h"
#include "MuCO/CustomizableObjectSystemPrivate.h"
#include "Materials/Material.h"
#include "MuR/MeshPrivate.h"
#include "MuR/OpMeshFormat.h"
#include "MuR/Mesh.h"
#include "MuR/MutableTrace.h"
#include "MuR/Parameters.h"
#include "MuR/Ptr.h"

namespace UnrealConversionUtils
{
	// Hidden functions only used internally to aid other functions
	namespace
	{
		/**
		 * Initializes the static mesh vertex buffers object provided with the data found on the mutable buffers
		 * @param OutVertexBuffers - The Unreal's vertex buffers container to be updated with the mutable data.
		 * @param NumVertices - The amount of vertices on the buffer
		 * @param NumTexCoords - The amount of texture coordinates
		 * @param bUseFullPrecisionUVs - Determines if we want to use or not full precision UVs
		 * @param bNeedCPUAccess - Determines if CPU access is required
		 * @param InMutablePositionData - Mutable position data buffer
		 * @param InMutableTangentData - Mutable tangent data buffer
		 * @param InMutableTextureData - Mutable texture data buffer
		 */
		void FStaticMeshVertexBuffers_InitWithMutableData(
		FStaticMeshVertexBuffers& OutVertexBuffers,
		const int32 NumVertices,
		const int32 NumTexCoords,
		const bool bUseFullPrecisionUVs,
		const bool bNeedCPUAccess,
		const void* InMutablePositionData,
		const void* InMutableTangentData,
		const void* InMutableTextureData)
{
	// positions
	OutVertexBuffers.PositionVertexBuffer.Init(NumVertices, bNeedCPUAccess);
	FMemory::Memcpy(OutVertexBuffers.PositionVertexBuffer.GetVertexData(), InMutablePositionData, NumVertices * OutVertexBuffers.PositionVertexBuffer.GetStride());

	// tangent and texture coords
	OutVertexBuffers.StaticMeshVertexBuffer.SetUseFullPrecisionUVs(true);
	OutVertexBuffers.StaticMeshVertexBuffer.SetUseHighPrecisionTangentBasis(false);
	OutVertexBuffers.StaticMeshVertexBuffer.Init(NumVertices, NumTexCoords, bNeedCPUAccess);
	FMemory::Memcpy(OutVertexBuffers.StaticMeshVertexBuffer.GetTangentData(), InMutableTangentData, OutVertexBuffers.StaticMeshVertexBuffer.GetTangentSize());
	FMemory::Memcpy(OutVertexBuffers.StaticMeshVertexBuffer.GetTexCoordData(), InMutableTextureData, OutVertexBuffers.StaticMeshVertexBuffer.GetTexCoordSize());
}


		/**
		 * Initializes the color vertex buffers object provided with the data found on the mutable buffers
		 * @param OutVertexBuffers - The Unreal's vertex buffers container to be updated with the mutable data.
		 * @param NumVertices - The amount of vertices on the buffer
		 * @param InMutableColorData - Mutable color data buffer
		 */
		void FColorVertexBuffers_InitWithMutableData(
	FStaticMeshVertexBuffers& OutVertexBuffers,
	const int32 NumVertices,
	const void* InMutableColorData
)
{
	// positions
	OutVertexBuffers.ColorVertexBuffer.Init(NumVertices);
	FMemory::Memcpy(OutVertexBuffers.ColorVertexBuffer.GetVertexData(), InMutableColorData, NumVertices * OutVertexBuffers.ColorVertexBuffer.GetStride());
}


		/**
		 * Initializes the skin vertex buffers object provided with the data found on the mutable buffers
		 * @param OutVertexWeightBuffer - The Unreal's vertex buffers container to be updated with the mutable data.
		   * @param NumVertices - The amount of vertices on the buffer
		 * @param NumBones - The amount of bones to use to init the skin weights buffer
		   * @param NumBoneInfluences - The amount of bone influences on the buffer
		 * @param bNeedCPUAccess - Determines if CPU access is required
		 * @param InMutableData - Mutable data buffer
		 */
		void FSkinWeightVertexBuffer_InitWithMutableData(
	FSkinWeightVertexBuffer& OutVertexWeightBuffer,
	const int32 NumVertices,
	const int32 NumBones,
	const int32 NumBoneInfluences,
	const bool bNeedCPUAccess,
	const void* InMutableData)
{
	// \todo Ugly cast.
	FSkinWeightDataVertexBuffer* VertexBuffer = const_cast<FSkinWeightDataVertexBuffer*>(OutVertexWeightBuffer.GetDataVertexBuffer());
	VertexBuffer->SetMaxBoneInfluences(NumBoneInfluences);
	VertexBuffer->Init(NumBones, NumVertices);

	if (NumVertices)
	{
		OutVertexWeightBuffer.SetNeedsCPUAccess(bNeedCPUAccess);

		uint8* Data = VertexBuffer->GetWeightData();
		FMemory::Memcpy(Data, InMutableData, OutVertexWeightBuffer.GetVertexDataSize());
	}
}
	}


	
	void BuildRefSkeleton(FInstanceUpdateData::FSkeletonData* OutMutSkeletonData
	,const FReferenceSkeleton& InSourceReferenceSkeleton,const TArray<bool>& InUsedBones,
	FReferenceSkeleton& InRefSkeleton, const USkeleton* InSkeleton )
{
	const int32 SourceBoneCount = InSourceReferenceSkeleton.GetNum();
	const TArray<FTransform>& SourceRawMeshBonePose = InSourceReferenceSkeleton.GetRawRefBonePose();
	
	MUTABLE_CPUPROFILER_SCOPE(BuildSkeletonData_BuildRefSkeleton);

	// Build new RefSkeleton	
	FReferenceSkeletonModifier RefSkeletonModifier(InRefSkeleton, InSkeleton);

	const TArray<FMeshBoneInfo>& BoneInfo = InSourceReferenceSkeleton.GetRawRefBoneInfo();

	TMap<FName, uint16> BoneToFinalBoneIndexMap;
	BoneToFinalBoneIndexMap.Reserve(SourceBoneCount);
	
	uint32 FinalBoneCount = 0;
	for (int32 BoneIndex = 0; BoneIndex < SourceBoneCount; ++BoneIndex)
	{
		if (!InUsedBones[BoneIndex])
		{
			continue;
		}

		FName BoneName = BoneInfo[BoneIndex].Name;

		// Build a bone to index map so we can remap BoneMaps and ActiveBoneIndices later on
		BoneToFinalBoneIndexMap.Add(BoneName, FinalBoneCount);
		FinalBoneCount++;

		// Find parent index
		const int32 SourceParentIndex = BoneInfo[BoneIndex].ParentIndex;
		const int32 ParentIndex = SourceParentIndex != INDEX_NONE ? BoneToFinalBoneIndexMap[BoneInfo[SourceParentIndex].Name] : INDEX_NONE;

		RefSkeletonModifier.Add(FMeshBoneInfo(BoneName, BoneName.ToString(), ParentIndex), SourceRawMeshBonePose[BoneIndex]);
	}
}


	void BuildSkeletalMeshElementDataAtLOD(const int32 MeshLODIndex,
	mu::MeshPtrConst InMutableMesh, USkeletalMesh* OutSkeletalMesh)
{
	int32 SurfaceCount = 0;
	if (InMutableMesh)
	{
		SurfaceCount = InMutableMesh->GetSurfaceCount();
	}
	
	Helper_GetLODInfoArray(OutSkeletalMesh)[MeshLODIndex].LODMaterialMap.SetNum(1);
	Helper_GetLODInfoArray(OutSkeletalMesh)[MeshLODIndex].LODMaterialMap[0] = 0;

	const int32 MaterialCount = SurfaceCount;
	int32 NewMaterialIndex = Helper_GetLODRenderSections(OutSkeletalMesh, MeshLODIndex).Num();
	for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; MaterialIndex++, NewMaterialIndex++)
	{
		new(Helper_GetLODRenderSections(OutSkeletalMesh, MeshLODIndex)) Helper_SkelMeshRenderSection();
		FSkelMeshRenderSection& Section = Helper_GetLODRenderSections(OutSkeletalMesh, MeshLODIndex)[NewMaterialIndex];
		Section.MaterialIndex = 0;
		Section.BaseIndex = 0;
		Section.NumTriangles = 0;
	}
}


	void SetupRenderSections(
	const mu::MeshPtrConst InMutableMesh,
	const USkeletalMesh* OutSkeletalMesh,
	const int32 MeshLODIndex,
	const int32 NumBoneInfluences,
	const TArray<uint16>& InBoneMap)
{
	const int32 SurfaceCount = InMutableMesh->GetSurfaceCount();
	for (int32 Surface = 0; Surface < SurfaceCount; Surface++)
	{
		MUTABLE_CPUPROFILER_SCOPE(UpdateSkeletalMesh_SurfaceLoop);

		int32 FirstIndex;
		int32 IndexCount;
		int32 FirstVertex;
		int32 VertexCount;
		InMutableMesh->GetSurface(Surface, &FirstVertex, &VertexCount, &FirstIndex, &IndexCount);
		FSkelMeshRenderSection& Section = Helper_GetLODRenderSections(OutSkeletalMesh, MeshLODIndex)[Surface];

		Section.DuplicatedVerticesBuffer.Init(1, TMap<int, TArray<int32>>());
			
		if (VertexCount == 0 || IndexCount == 0)
		{
			Section.bDisabled = true;
			continue; // Unreal doesn't like empty meshes
		}
			
		Section.BaseIndex = FirstIndex;
		Section.NumTriangles = IndexCount / 3;
		Section.BaseVertexIndex = FirstVertex;
		Section.MaxBoneInfluences = NumBoneInfluences;
		Section.NumVertices = VertexCount;

		//Section.BoneMap.Append(OperationData->InstanceUpdateData.LODs[LODIndex].Components[ComponentIndex].BoneMap);
		Section.BoneMap.Append(InBoneMap);
	}
}


	void CopyMutableVertexBuffers(
	USkeletalMesh* OutSkeletalMesh,
	const int32 NumVerticesLODModel, const int32 NumBoneInfluences,
	const int32 BoneIndexBuffer,
	const mu::FMeshBufferSet& MutableMeshVertexBuffers,
	const int32 MeshLODIndex,
	const mu::MESH_BUFFER_FORMAT& InBoneIndexFormat
	)
{
	FSkeletalMeshLODRenderData& LODModel = Helper_GetLODData(OutSkeletalMesh)[MeshLODIndex];
	
	MUTABLE_CPUPROFILER_SCOPE(UpdateSkeletalMesh_SurfaceLoop_MemCpy);

	const ESkeletalMeshVertexFlags BuildFlags = OutSkeletalMesh->GetVertexBufferFlags();
	const bool bUseFullPrecisionUVs = EnumHasAllFlags(BuildFlags, ESkeletalMeshVertexFlags::UseFullPrecisionUVs);
	const bool bHasVertexColors = EnumHasAllFlags(BuildFlags, ESkeletalMeshVertexFlags::HasVertexColors);
	const int NumTexCoords = MutableMeshVertexBuffers.GetBufferChannelCount(MUTABLE_VERTEXBUFFER_TEXCOORDS);

	const bool bNeedsCPUAccess = Helper_GetLODInfoArray(OutSkeletalMesh)[MeshLODIndex].bAllowCPUAccess;
	
	FStaticMeshVertexBuffers_InitWithMutableData(
		LODModel.StaticVertexBuffers,
		NumVerticesLODModel, 
		NumTexCoords, 
		bUseFullPrecisionUVs, 
		bNeedsCPUAccess,
		MutableMeshVertexBuffers.GetBufferData(MUTABLE_VERTEXBUFFER_POSITION),
		MutableMeshVertexBuffers.GetBufferData(MUTABLE_VERTEXBUFFER_TANGENT),
		MutableMeshVertexBuffers.GetBufferData(MUTABLE_VERTEXBUFFER_TEXCOORDS)
		);

	// Init skin weight buffer
	FSkinWeightVertexBuffer_InitWithMutableData(
		LODModel.SkinWeightVertexBuffer,
		NumVerticesLODModel,
		NumBoneInfluences * NumVerticesLODModel,
		NumBoneInfluences,
		bNeedsCPUAccess,
		MutableMeshVertexBuffers.GetBufferData(BoneIndexBuffer)
	);

	if (InBoneIndexFormat == mu::MBF_UINT16)
	{
		LODModel.SkinWeightVertexBuffer.SetUse16BitBoneIndex(true);
	}

	// Optional buffers
	for ( int Buffer= MUTABLE_VERTEXBUFFER_TEXCOORDS + 1; Buffer<MutableMeshVertexBuffers.GetBufferCount(); ++Buffer)
	{
		if (MutableMeshVertexBuffers.GetBufferChannelCount(Buffer) > 0)
		{
			mu::MESH_BUFFER_SEMANTIC Semantic;
			mu::MESH_BUFFER_FORMAT Format;
			int32 SemanticIndex;
			int32 ComponentCount;
			int32 Offset;
			MutableMeshVertexBuffers.GetChannel(Buffer, 0, &Semantic, &SemanticIndex, &Format, &ComponentCount, &Offset);
			
			if (Semantic == mu::MBS_BONEINDICES)
			{
				const int32 BonesPerVertex = ComponentCount;
				const int32 NumBones = BonesPerVertex * NumVerticesLODModel;

				check(LODModel.SkinWeightVertexBuffer.GetVariableBonesPerVertex() == false);
				FSkinWeightVertexBuffer_InitWithMutableData(
					LODModel.SkinWeightVertexBuffer,
					NumVerticesLODModel,
					NumBones, 
					NumBoneInfluences,
					bNeedsCPUAccess,
					MutableMeshVertexBuffers.GetBufferData(Buffer)
				);
			}

			// colour buffer?
			else if (Semantic == mu::MBS_COLOUR)
			{
				OutSkeletalMesh->SetHasVertexColors(true);
				const void* DataPtr = MutableMeshVertexBuffers.GetBufferData(Buffer);
				FColorVertexBuffers_InitWithMutableData(LODModel.StaticVertexBuffers, NumVerticesLODModel, DataPtr);
				check(LODModel.StaticVertexBuffers.ColorVertexBuffer.GetStride() == MutableMeshVertexBuffers.GetElementSize(Buffer));
			}
		}
	}
}


	bool CopyMutableIndexBuffers(mu::MeshPtrConst InMutableMesh,FSkeletalMeshLODRenderData& LODModel)
{
	MUTABLE_CPUPROFILER_SCOPE(UpdateSkeletalMesh_BuildSkeletalMeshRenderData_IndexLoop);

	const int32 IndexCount = InMutableMesh->GetIndexBuffers().GetElementCount();

	if (IndexCount)
	{
		if (InMutableMesh->GetIndexBuffers().GetElementSize(0) == 2)
		{
			const uint16* IndexPtr = (const uint16*)InMutableMesh->GetIndexBuffers().GetBufferData(0);
			LODModel.MultiSizeIndexContainer.CreateIndexBuffer(2);
			LODModel.MultiSizeIndexContainer.GetIndexBuffer()->Insert(0, IndexCount);
			FMemory::Memcpy(LODModel.MultiSizeIndexContainer.GetIndexBuffer()->GetPointerTo(0), IndexPtr, IndexCount * sizeof(uint16));
		}
		else
		{
			const uint32* IndexPtr = (const uint32*)InMutableMesh->GetIndexBuffers().GetBufferData(0);
			LODModel.MultiSizeIndexContainer.CreateIndexBuffer(4);
			LODModel.MultiSizeIndexContainer.GetIndexBuffer()->Insert(0, IndexCount);
			FMemory::Memcpy(LODModel.MultiSizeIndexContainer.GetIndexBuffer()->GetPointerTo(0), IndexPtr, IndexCount * sizeof(uint32));
		}

	}
	else
	{
		UE_LOG(LogMutable, Error, TEXT("UCustomizableInstancePrivateData::BuildSkeletalMeshRenderData is converting an empty mesh."));
		return false;
	}

	return true;
}

		}

	
		
