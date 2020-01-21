// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rendering/SkeletalMeshLODModel.h"

#if WITH_EDITOR
#include "RenderUtils.h"
#include "EngineUtils.h"
#include "SkeletalMeshTypes.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/EditorObjectVersion.h"
#include "Interfaces/ITargetPlatform.h"
#include "Rendering/MultiSizeIndexContainer.h"
#include "Rendering/SkeletalMeshVertexBuffer.h"
#include "Rendering/ColorVertexBuffer.h"
#include "Rendering/SkeletalMeshVertexClothBuffer.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "UObject/ReleaseObjectVersion.h"
#include "UObject/RenderingObjectVersion.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "UObject/FortniteMainBranchObjectVersion.h"
#include "GPUSkinVertexFactory.h"

/*-----------------------------------------------------------------------------
FSoftSkinVertex
-----------------------------------------------------------------------------*/

/**
* Serializer
*
* @param Ar - archive to serialize with
* @param V - vertex to serialize
* @return archive that was used
*/
FArchive& operator<<(FArchive& Ar, FSoftSkinVertex& V)
{
	Ar << V.Position;

	if (Ar.CustomVer(FRenderingObjectVersion::GUID) < FRenderingObjectVersion::IncreaseNormalPrecision)
	{
		FDeprecatedSerializedPackedNormal Temp;
		Ar << Temp;
		V.TangentX = Temp;
		Ar << Temp;
		V.TangentY = Temp;
		Ar << Temp;
		V.TangentZ = Temp;
	}
	else
	{
		Ar << V.TangentX << V.TangentY << V.TangentZ;
	}

	for (int32 UVIdx = 0; UVIdx < MAX_TEXCOORDS; ++UVIdx)
	{
		Ar << V.UVs[UVIdx];
	}

	Ar << V.Color;

	// serialize bone and weight uint8 arrays in order
	// this is required when serializing as bulk data memory (see TArray::BulkSerialize notes)
	for (uint32 InfluenceIndex = 0; InfluenceIndex < MAX_INFLUENCES_PER_STREAM; InfluenceIndex++)
	{
		Ar << V.InfluenceBones[InfluenceIndex];
	}

	if (Ar.UE4Ver() >= VER_UE4_SUPPORT_8_BONE_INFLUENCES_SKELETAL_MESHES)
	{
		for (uint32 InfluenceIndex = MAX_INFLUENCES_PER_STREAM; InfluenceIndex < MAX_TOTAL_INFLUENCES; InfluenceIndex++)
		{
			Ar << V.InfluenceBones[InfluenceIndex];
		}
	}
	else
	{
		if (Ar.IsLoading())
		{
			for (uint32 InfluenceIndex = MAX_INFLUENCES_PER_STREAM; InfluenceIndex < MAX_TOTAL_INFLUENCES; InfluenceIndex++)
			{
				V.InfluenceBones[InfluenceIndex] = 0;
			}
		}
	}

	for (uint32 InfluenceIndex = 0; InfluenceIndex < MAX_INFLUENCES_PER_STREAM; InfluenceIndex++)
	{
		Ar << V.InfluenceWeights[InfluenceIndex];
	}

	if (Ar.UE4Ver() >= VER_UE4_SUPPORT_8_BONE_INFLUENCES_SKELETAL_MESHES)
	{
		for (uint32 InfluenceIndex = MAX_INFLUENCES_PER_STREAM; InfluenceIndex < MAX_TOTAL_INFLUENCES; InfluenceIndex++)
		{
			Ar << V.InfluenceWeights[InfluenceIndex];
		}
	}
	else
	{
		if (Ar.IsLoading())
		{
			for (uint32 InfluenceIndex = MAX_INFLUENCES_PER_STREAM; InfluenceIndex < MAX_TOTAL_INFLUENCES; InfluenceIndex++)
			{
				V.InfluenceWeights[InfluenceIndex] = 0;
			}
		}
	}

	return Ar;
}

bool FSoftSkinVertex::GetRigidWeightBone(uint8& OutBoneIndex) const
{
	bool bIsRigid = false;

	for (int32 WeightIdx = 0; WeightIdx < MAX_TOTAL_INFLUENCES; WeightIdx++)
	{
		if (InfluenceWeights[WeightIdx] == 255)
		{
			bIsRigid = true;
			OutBoneIndex = InfluenceBones[WeightIdx];
			break;
		}
	}

	return bIsRigid;
}

uint8 FSoftSkinVertex::GetMaximumWeight() const
{
	uint8 MaxInfluenceWeight = 0;

	for (int32 Index = 0; Index < MAX_TOTAL_INFLUENCES; Index++)
	{
		const uint8 Weight = InfluenceWeights[Index];

		if (Weight > MaxInfluenceWeight)
		{
			MaxInfluenceWeight = Weight;
		}
	}

	return MaxInfluenceWeight;
}

/** Legacy 'rigid' skin vertex */
struct FLegacyRigidSkinVertex
{
	FVector			Position;
	FVector			TangentX;	// Tangent, U-direction
	FVector			TangentY;	// Binormal, V-direction
	FVector			TangentZ;	// Normal
	FVector2D		UVs[MAX_TEXCOORDS]; // UVs
	FColor			Color;		// Vertex color.
	uint8			Bone;

	friend FArchive& operator<<(FArchive& Ar, FLegacyRigidSkinVertex& V)
	{
		Ar << V.Position;

		if (Ar.CustomVer(FRenderingObjectVersion::GUID) < FRenderingObjectVersion::IncreaseNormalPrecision)
		{
			FDeprecatedSerializedPackedNormal Temp;
			Ar << Temp;
			V.TangentX = Temp;
			Ar << Temp;
			V.TangentY = Temp;
			Ar << Temp;
			V.TangentZ = Temp;
		}
		else
		{
			Ar << V.TangentX << V.TangentY << V.TangentZ;
		}

		for (int32 UVIdx = 0; UVIdx < MAX_TEXCOORDS; ++UVIdx)
		{
			Ar << V.UVs[UVIdx];
		}

		Ar << V.Color;
		Ar << V.Bone;

		return Ar;
	}

	/** Util to convert from legacy */
	void ConvertToSoftVert(FSoftSkinVertex& DestVertex)
	{
		DestVertex.Position = Position;
		DestVertex.TangentX = TangentX;
		DestVertex.TangentY = TangentY;
		DestVertex.TangentZ = TangentZ;
		// store the sign of the determinant in TangentZ.W
		DestVertex.TangentZ.W = GetBasisDeterminantSign(TangentX, TangentY, TangentZ);

		// copy all texture coordinate sets
		FMemory::Memcpy(DestVertex.UVs, UVs, sizeof(FVector2D)*MAX_TEXCOORDS);

		DestVertex.Color = Color;
		DestVertex.InfluenceBones[0] = Bone;
		DestVertex.InfluenceWeights[0] = 255;
		for (int32 InfluenceIndex = 1; InfluenceIndex < MAX_TOTAL_INFLUENCES; InfluenceIndex++)
		{
			DestVertex.InfluenceBones[InfluenceIndex] = 0;
			DestVertex.InfluenceWeights[InfluenceIndex] = 0;
		}
	}
};


/**
* Calculate max # of bone influences used by this skel mesh chunk
*/
void FSkelMeshSection::CalcMaxBoneInfluences()
{
	// if we only have rigid verts then there is only one bone
	MaxBoneInfluences = 1;
	// iterate over all the soft vertices for this chunk and find max # of bones used
	for (int32 VertIdx = 0; VertIdx < SoftVertices.Num(); VertIdx++)
	{
		FSoftSkinVertex& SoftVert = SoftVertices[VertIdx];

		// calc # of bones used by this soft skinned vertex
		int32 BonesUsed = 0;
		for (int32 InfluenceIdx = 0; InfluenceIdx < MAX_TOTAL_INFLUENCES; InfluenceIdx++)
		{
			if (SoftVert.InfluenceWeights[InfluenceIdx] > 0)
			{
				BonesUsed++;
			}
		}
		// reorder bones so that there aren't any unused influence entries within the [0,BonesUsed] range
		for (int32 InfluenceIdx = 0; InfluenceIdx < BonesUsed; InfluenceIdx++)
		{
			if (SoftVert.InfluenceWeights[InfluenceIdx] == 0)
			{
				for (int32 ExchangeIdx = InfluenceIdx + 1; ExchangeIdx < MAX_TOTAL_INFLUENCES; ExchangeIdx++)
				{
					if (SoftVert.InfluenceWeights[ExchangeIdx] != 0)
					{
						Exchange(SoftVert.InfluenceWeights[InfluenceIdx], SoftVert.InfluenceWeights[ExchangeIdx]);
						Exchange(SoftVert.InfluenceBones[InfluenceIdx], SoftVert.InfluenceBones[ExchangeIdx]);
						break;
					}
				}
			}
		}

		// maintain max bones used
		MaxBoneInfluences = FMath::Max(MaxBoneInfluences, BonesUsed);
	}
}

// Serialization.
FArchive& operator<<(FArchive& Ar, FSkelMeshSection& S)
{
	Ar.UsingCustomVersion(FEditorObjectVersion::GUID);
	Ar.UsingCustomVersion(FReleaseObjectVersion::GUID);
	Ar.UsingCustomVersion(FRenderingObjectVersion::GUID);

	// When data is cooked for server platform some of the
	// variables are not serialized so that they're always
	// set to their initial values (for safety)
	FStripDataFlags StripFlags(Ar);

	Ar << S.MaterialIndex;

	Ar.UsingCustomVersion(FSkeletalMeshCustomVersion::GUID);
	if (Ar.CustomVer(FSkeletalMeshCustomVersion::GUID) < FSkeletalMeshCustomVersion::CombineSectionWithChunk)
	{
		uint16 DummyChunkIndex;
		Ar << DummyChunkIndex;
	}

	if (!StripFlags.IsDataStrippedForServer())
	{
		Ar << S.BaseIndex;
	}

	if (!StripFlags.IsDataStrippedForServer())
	{
		Ar << S.NumTriangles;
	}

	if (Ar.CustomVer(FSkeletalMeshCustomVersion::GUID) < FSkeletalMeshCustomVersion::RemoveTriangleSorting)
	{
		uint8 DummyTriangleSorting;
		Ar << DummyTriangleSorting;
	}

	// for clothing info
	if (Ar.UE4Ver() >= VER_UE4_APEX_CLOTH)
	{
		// Load old 'disabled' flag on sections, as this was used to identify legacy clothing sections for conversion
		if (Ar.CustomVer(FSkeletalMeshCustomVersion::GUID) < FSkeletalMeshCustomVersion::DeprecateSectionDisabledFlag)
		{
			Ar << S.bLegacyClothingSection_DEPRECATED;
		}

		// No longer serialize this if it's not used to map sections any more.
		if(Ar.CustomVer(FSkeletalMeshCustomVersion::GUID) < FSkeletalMeshCustomVersion::RemoveDuplicatedClothingSections)
		{
			Ar << S.CorrespondClothSectionIndex_DEPRECATED;
		}
	}

	if (Ar.UE4Ver() >= VER_UE4_APEX_CLOTH_LOD)
	{
		Ar << S.bEnableClothLOD_DEPRECATED;
	}

	Ar.UsingCustomVersion(FRecomputeTangentCustomVersion::GUID);
	if (Ar.CustomVer(FRecomputeTangentCustomVersion::GUID) >= FRecomputeTangentCustomVersion::RuntimeRecomputeTangent)
	{
		Ar << S.bRecomputeTangent;
	}

	if (Ar.CustomVer(FEditorObjectVersion::GUID) >= FEditorObjectVersion::RefactorMeshEditorMaterials)
	{
		Ar << S.bCastShadow;
	}
	else
	{
		S.bCastShadow = true;
	}

	if (Ar.CustomVer(FSkeletalMeshCustomVersion::GUID) >= FSkeletalMeshCustomVersion::CombineSectionWithChunk)
	{

		if (!StripFlags.IsDataStrippedForServer())
		{
			// This is so that BaseVertexIndex is never set to anything else that 0 (for safety)
			Ar << S.BaseVertexIndex;
		}

		if (!StripFlags.IsEditorDataStripped() && !(Ar.IsFilterEditorOnly() && Ar.IsCountingMemory()))
		{
			// For backwards compat, read rigid vert array into array
			TArray<FLegacyRigidSkinVertex> LegacyRigidVertices;
			if (Ar.IsLoading() && Ar.CustomVer(FSkeletalMeshCustomVersion::GUID) < FSkeletalMeshCustomVersion::CombineSoftAndRigidVerts)
			{
				Ar << LegacyRigidVertices;
			}

			Ar << S.SoftVertices;

			// Once we have read in SoftVertices, convert and insert legacy rigid verts (if present) at start
			const int32 NumRigidVerts = LegacyRigidVertices.Num();
			if (NumRigidVerts > 0 && Ar.CustomVer(FSkeletalMeshCustomVersion::GUID) < FSkeletalMeshCustomVersion::CombineSoftAndRigidVerts)
			{
				S.SoftVertices.InsertUninitialized(0, NumRigidVerts);

				for (int32 VertIdx = 0; VertIdx < NumRigidVerts; VertIdx++)
				{
					LegacyRigidVertices[VertIdx].ConvertToSoftVert(S.SoftVertices[VertIdx]);
				}
			}
		}

		// If loading content newer than CombineSectionWithChunk but older than SaveNumVertices, update NumVertices here
		if (Ar.IsLoading() && Ar.CustomVer(FSkeletalMeshCustomVersion::GUID) < FSkeletalMeshCustomVersion::SaveNumVertices)
		{
			if (!StripFlags.IsDataStrippedForServer())
			{
				S.NumVertices = S.SoftVertices.Num();
			}
			else
			{
				UE_LOG(LogSkeletalMesh, Warning, TEXT("Cannot set FSkelMeshSection::NumVertices for older content, loading in non-editor build."));
				S.NumVertices = 0;
			}
		}

		Ar << S.BoneMap;

		if (Ar.CustomVer(FSkeletalMeshCustomVersion::GUID) >= FSkeletalMeshCustomVersion::SaveNumVertices)
		{
			Ar << S.NumVertices;
		}

		// Removed NumRigidVertices and NumSoftVertices
		if (Ar.CustomVer(FSkeletalMeshCustomVersion::GUID) < FSkeletalMeshCustomVersion::CombineSoftAndRigidVerts)
		{
			int32 DummyNumRigidVerts, DummyNumSoftVerts;
			Ar << DummyNumRigidVerts;
			Ar << DummyNumSoftVerts;

			if (DummyNumRigidVerts + DummyNumSoftVerts != S.SoftVertices.Num())
			{
				UE_LOG(LogSkeletalMesh, Error, TEXT("Legacy NumSoftVerts + NumRigidVerts != SoftVertices.Num()"));
			}
		}

		Ar << S.MaxBoneInfluences;

#if WITH_EDITOR
		// If loading content where we need to recalc 'max bone influences' instead of using loaded version, do that now
		if (!StripFlags.IsEditorDataStripped() && Ar.IsLoading() && Ar.CustomVer(FSkeletalMeshCustomVersion::GUID) < FSkeletalMeshCustomVersion::RecalcMaxBoneInfluences)
		{
			S.CalcMaxBoneInfluences();
		}
#endif

		Ar << S.ClothMappingData;

		// We no longer need the positions and normals for a clothing sim mesh to be stored in sections, so throw that data out
		if(Ar.CustomVer(FSkeletalMeshCustomVersion::GUID) < FSkeletalMeshCustomVersion::RemoveDuplicatedClothingSections)
		{
			TArray<FVector> DummyArray;
			Ar << DummyArray;
			Ar << DummyArray;
		}

		Ar << S.CorrespondClothAssetIndex;

		if (Ar.CustomVer(FSkeletalMeshCustomVersion::GUID) < FSkeletalMeshCustomVersion::NewClothingSystemAdded)
		{
			int16 DummyClothAssetSubmeshIndex;
			Ar << DummyClothAssetSubmeshIndex;
		}
		else
		{
			Ar << S.ClothingData;
		}

		Ar.UsingCustomVersion(FOverlappingVerticesCustomVersion::GUID);
		
		if (Ar.CustomVer(FOverlappingVerticesCustomVersion::GUID) >= FOverlappingVerticesCustomVersion::DetectOVerlappingVertices)
		{
			Ar << S.OverlappingVertices;
		}

		if(Ar.CustomVer(FReleaseObjectVersion::GUID) >= FReleaseObjectVersion::AddSkeletalMeshSectionDisable)
		{
			Ar << S.bDisabled;
		}

		if (Ar.CustomVer(FSkeletalMeshCustomVersion::GUID) >= FSkeletalMeshCustomVersion::SectionIgnoreByReduceAdded)
		{
			Ar << S.GenerateUpToLodIndex;
		}
		else if(Ar.IsLoading())
		{
			S.GenerateUpToLodIndex = -1;
		}

		if (Ar.CustomVer(FEditorObjectVersion::GUID) >= FEditorObjectVersion::SkeletalMeshBuildRefactor)
		{
			Ar << S.OriginalDataSectionIndex;
			Ar << S.ChunkedParentSectionIndex;
		}
		else if (Ar.IsLoading())
		{
			S.OriginalDataSectionIndex = INDEX_NONE;
			S.ChunkedParentSectionIndex = INDEX_NONE;
		}
		return Ar;
	}

	return Ar;
}

// Serialization.
FArchive& operator<<(FArchive& Ar, FSkelMeshSourceSectionUserData& S)
{
	FStripDataFlags StripFlags(Ar);
	// When data is cooked we do not serialize anything
	//This is for editor only editing
	if (StripFlags.IsEditorDataStripped())
	{
		return Ar;
	}

	Ar << S.bRecomputeTangent;
	Ar << S.bCastShadow;
	Ar << S.bDisabled;
	Ar << S.GenerateUpToLodIndex;
	Ar << S.CorrespondClothAssetIndex;
	Ar << S.ClothingData;

	return Ar;
}

//////////////////////////////////////////////////////////////////////////

/** Legacy Chunk struct, now merged with FSkelMeshSection */
struct FLegacySkelMeshChunk
{
	uint32 BaseVertexIndex;
	TArray<FSoftSkinVertex> SoftVertices;
	TArray<FMeshToMeshVertData> ApexClothMappingData;
	TArray<FVector> PhysicalMeshVertices;
	TArray<FVector> PhysicalMeshNormals;
	TArray<FBoneIndexType> BoneMap;
	int32 MaxBoneInfluences;

	int16 CorrespondClothAssetIndex;
	int16 ClothAssetSubmeshIndex;

	FLegacySkelMeshChunk()
		: BaseVertexIndex(0)
		, MaxBoneInfluences(4)
		, CorrespondClothAssetIndex(INDEX_NONE)
		, ClothAssetSubmeshIndex(INDEX_NONE)
	{}

	void CopyToSection(FSkelMeshSection& Section)
	{
		Section.BaseVertexIndex = BaseVertexIndex;
		Section.SoftVertices = SoftVertices;
		Section.ClothMappingData = ApexClothMappingData;
		Section.BoneMap = BoneMap;
		Section.MaxBoneInfluences = MaxBoneInfluences;
		Section.CorrespondClothAssetIndex = CorrespondClothAssetIndex;
	}


	friend FArchive& operator<<(FArchive& Ar, FLegacySkelMeshChunk& C)
	{
		FStripDataFlags StripFlags(Ar);

		if (!StripFlags.IsDataStrippedForServer())
		{
			// This is so that BaseVertexIndex is never set to anything else that 0 (for safety)
			Ar << C.BaseVertexIndex;
		}
		if (!StripFlags.IsEditorDataStripped())
		{
			// For backwards compat, read rigid vert array into array
			TArray<FLegacyRigidSkinVertex> LegacyRigidVertices;
			if (Ar.IsLoading() && Ar.CustomVer(FSkeletalMeshCustomVersion::GUID) < FSkeletalMeshCustomVersion::CombineSoftAndRigidVerts)
			{
				Ar << LegacyRigidVertices;
			}

			Ar << C.SoftVertices;

			// Once we have read in SoftVertices, convert and insert legacy rigid verts (if present) at start
			const int32 NumRigidVerts = LegacyRigidVertices.Num();
			if (NumRigidVerts > 0 && Ar.CustomVer(FSkeletalMeshCustomVersion::GUID) < FSkeletalMeshCustomVersion::CombineSoftAndRigidVerts)
			{
				C.SoftVertices.InsertUninitialized(0, NumRigidVerts);

				for (int32 VertIdx = 0; VertIdx < NumRigidVerts; VertIdx++)
				{
					LegacyRigidVertices[VertIdx].ConvertToSoftVert(C.SoftVertices[VertIdx]);
				}
			}
		}
		Ar << C.BoneMap;

		// Removed NumRigidVertices and NumSoftVertices, just use array size
		if (Ar.CustomVer(FSkeletalMeshCustomVersion::GUID) < FSkeletalMeshCustomVersion::CombineSoftAndRigidVerts)
		{
			int32 DummyNumRigidVerts, DummyNumSoftVerts;
			Ar << DummyNumRigidVerts;
			Ar << DummyNumSoftVerts;

			if (DummyNumRigidVerts + DummyNumSoftVerts != C.SoftVertices.Num())
			{
				UE_LOG(LogSkeletalMesh, Error, TEXT("Legacy NumSoftVerts + NumRigidVerts != SoftVertices.Num()"));
			}
		}

		Ar << C.MaxBoneInfluences;


		if (Ar.UE4Ver() >= VER_UE4_APEX_CLOTH)
		{
			Ar << C.ApexClothMappingData;
			Ar << C.PhysicalMeshVertices;
			Ar << C.PhysicalMeshNormals;
			Ar << C.CorrespondClothAssetIndex;
			Ar << C.ClothAssetSubmeshIndex;
		}

		return Ar;
	}
};

void FSkeletalMeshLODModel::Serialize(FArchive& Ar, UObject* Owner, int32 Idx)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FSkeletalMeshLODModel::Serialize"), STAT_SkeletalMeshLODModel_Serialize, STATGROUP_LoadTime);

	const uint8 LodAdjacencyStripFlag = 1;
	FStripDataFlags StripFlags(Ar, Ar.IsCooking() && !Ar.CookingTarget()->SupportsFeature(ETargetPlatformFeatures::Tessellation) ? LodAdjacencyStripFlag : 0);

	Ar.UsingCustomVersion(FSkeletalMeshCustomVersion::GUID);
	Ar.UsingCustomVersion(FFortniteMainBranchObjectVersion::GUID);
	Ar.UsingCustomVersion(FEditorObjectVersion::GUID);

	if (StripFlags.IsDataStrippedForServer())
	{
		TArray<FSkelMeshSection> TempSections;
		Ar << TempSections;

		if (Ar.CustomVer(FEditorObjectVersion::GUID) >= FEditorObjectVersion::SkeletalMeshBuildRefactor)
		{
			TMap<int32, FSkelMeshSourceSectionUserData> TempUserSectionsData;
			Ar << TempUserSectionsData;
		}

		// For old content, load as a multi-size container
		if (Ar.IsLoading() && Ar.CustomVer(FSkeletalMeshCustomVersion::GUID) < FSkeletalMeshCustomVersion::SplitModelAndRenderData)
		{
			FMultiSizeIndexContainer TempMultiSizeIndexContainer;
			TempMultiSizeIndexContainer.Serialize(Ar, false);
		}
		else
		{
			TArray<int32> DummyIndexBuffer;
			Ar << DummyIndexBuffer;
		}

		TArray<FBoneIndexType> TempActiveBoneIndices;
		Ar << TempActiveBoneIndices;
	}
	else
	{
		Ar << Sections;
		
		if (!StripFlags.IsEditorDataStripped() && Ar.CustomVer(FEditorObjectVersion::GUID) >= FEditorObjectVersion::SkeletalMeshBuildRefactor)
		{
			//Editor builds only
			Ar << UserSectionsData;
		}

		// For old content, load as a multi-size container, but convert into regular array
		if (Ar.IsLoading() && Ar.CustomVer(FSkeletalMeshCustomVersion::GUID) < FSkeletalMeshCustomVersion::SplitModelAndRenderData)
		{
			FMultiSizeIndexContainer TempMultiSizeIndexContainer;
			TempMultiSizeIndexContainer.Serialize(Ar, false);

			// Only save index buffer data in editor builds
			if (!StripFlags.IsEditorDataStripped())
			{
				TempMultiSizeIndexContainer.GetIndexBuffer(IndexBuffer);
			}
		}
		// Only load index buffer data in editor builds
		else if(!StripFlags.IsEditorDataStripped())
		{
			Ar << IndexBuffer;
		}

		Ar << ActiveBoneIndices;
	}

	// Array of Sections for backwards compat
	if (Ar.IsLoading() && Ar.CustomVer(FSkeletalMeshCustomVersion::GUID) < FSkeletalMeshCustomVersion::CombineSectionWithChunk)
	{
		TArray<FLegacySkelMeshChunk> LegacyChunks;

		Ar << LegacyChunks;

		check(LegacyChunks.Num() == Sections.Num());
		for (int32 ChunkIdx = 0; ChunkIdx < LegacyChunks.Num(); ChunkIdx++)
		{
			FSkelMeshSection& Section = Sections[ChunkIdx];

			LegacyChunks[ChunkIdx].CopyToSection(Section);

			// Set NumVertices for older content on load
			if (!StripFlags.IsDataStrippedForServer())
			{
				Section.NumVertices = Section.SoftVertices.Num();
			}
			else
			{
				UE_LOG(LogSkeletalMesh, Warning, TEXT("Cannot set FSkelMeshSection::NumVertices for older content, loading in non-editor build."));
				Section.NumVertices = 0;
			}
		}
	}

	// no longer in use
	{
		uint32 LegacySize = 0;
		Ar << LegacySize;
	}

	if (!StripFlags.IsDataStrippedForServer())
	{
		Ar << NumVertices;
	}
	Ar << RequiredBones;

	if (!StripFlags.IsEditorDataStripped())
	{
		RawPointIndices.Serialize(Ar, Owner);
		if (Ar.IsLoading()
			&& (Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID) >= FFortniteMainBranchObjectVersion::NewSkeletalMeshImporterWorkflow)
			&& (Ar.CustomVer(FEditorObjectVersion::GUID) < FEditorObjectVersion::SkeletalMeshMoveEditorSourceDataToPrivateAsset))
		{
			RawSkeletalMeshBulkData_DEPRECATED.Serialize(Ar, Owner);
			RawSkeletalMeshBulkDataID = RawSkeletalMeshBulkData_DEPRECATED.GetIdString();
			bIsBuildDataAvailable = RawSkeletalMeshBulkData_DEPRECATED.IsBuildDataAvailable();
			bIsRawSkeletalMeshBulkDataEmpty = RawSkeletalMeshBulkData_DEPRECATED.IsEmpty();
		}
		if (Ar.CustomVer(FEditorObjectVersion::GUID) >= FEditorObjectVersion::SkeletalMeshMoveEditorSourceDataToPrivateAsset)
		{
			Ar << RawSkeletalMeshBulkDataID;
			Ar << bIsBuildDataAvailable;
			Ar << bIsRawSkeletalMeshBulkDataEmpty;
		}
	}

	if (StripFlags.IsDataStrippedForServer())
	{
		TArray<int32> TempMeshToImportVertexMap;
		Ar << TempMeshToImportVertexMap;

		int32 TempMaxImportVertex;
		Ar << TempMaxImportVertex;
	}
	else
	{
		Ar << MeshToImportVertexMap;
		Ar << MaxImportVertex;
	}

	if (!StripFlags.IsDataStrippedForServer())
	{
		Ar << NumTexCoords;

		// All this data has now moved to derived data, but need to handle loading older LOD Models where it was serialized with asset
		if (Ar.IsLoading() && Ar.CustomVer(FSkeletalMeshCustomVersion::GUID) < FSkeletalMeshCustomVersion::SplitModelAndRenderData)
		{
			FDummySkeletalMeshVertexBuffer DummyVertexBuffer;
			Ar << DummyVertexBuffer;

			if (Ar.CustomVer(FSkeletalMeshCustomVersion::GUID) >= FSkeletalMeshCustomVersion::UseSeparateSkinWeightBuffer)
			{
				FSkinWeightVertexBuffer DummyWeightBuffer;
				Ar << DummyWeightBuffer;
			}

			USkeletalMesh* SkelMeshOwner = CastChecked<USkeletalMesh>(Owner);
			if (SkelMeshOwner->bHasVertexColors)
			{
				// Handling for old color buffer data
				if (Ar.IsLoading() && Ar.CustomVer(FSkeletalMeshCustomVersion::GUID) < FSkeletalMeshCustomVersion::UseSharedColorBufferFormat)
				{
					TArray<FColor> OldColors;
					FStripDataFlags LegacyColourStripFlags(Ar, 0, VER_UE4_STATIC_SKELETAL_MESH_SERIALIZATION_FIX);
					OldColors.BulkSerialize(Ar);
				}
				else
				{
					FColorVertexBuffer DummyColorBuffer;
					DummyColorBuffer.Serialize(Ar, false);
					//Copy the data to the softVertices
					int32 VertexColorCount = DummyColorBuffer.GetNumVertices();
					if (NumVertices == VertexColorCount)
					{
						TArray<FColor> OutColors;
						DummyColorBuffer.GetVertexColors(OutColors);
						int32 DummyVertexColorIndex = 0;
						for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
						{
							int32 SectionVertexCount = Sections[SectionIndex].GetNumVertices();
							TArray<FSoftSkinVertex>& SoftVertices = Sections[SectionIndex].SoftVertices;
							for (int32 SectionVertexIndex = 0; SectionVertexIndex < SectionVertexCount; ++SectionVertexIndex)
							{
								SoftVertices[SectionVertexIndex].Color = OutColors[DummyVertexColorIndex++];
							}
						}
					}
				}
			}

			if (!StripFlags.IsClassDataStripped(LodAdjacencyStripFlag))
			{
				// For old content, load as a multi-size container, but convert into regular array
				{
					// Serialize and discard the adjacency data, it's now build for the DDC
					FMultiSizeIndexContainer TempMultiSizeAdjacencyIndexContainer;
					TempMultiSizeAdjacencyIndexContainer.Serialize(Ar, false);
				}
			}

			if (Ar.UE4Ver() >= VER_UE4_APEX_CLOTH && HasClothData())
			{
				FStripDataFlags StripFlags2(Ar, 0, VER_UE4_STATIC_SKELETAL_MESH_SERIALIZATION_FIX);
				TSkeletalMeshVertexData<FMeshToMeshVertData> DummyClothData(true);

				if (!StripFlags2.IsDataStrippedForServer() || Ar.IsCountingMemory())
				{
					DummyClothData.Serialize(Ar);
			
					if (Ar.CustomVer(FSkeletalMeshCustomVersion::GUID) >= FSkeletalMeshCustomVersion::CompactClothVertexBuffer)
					{
						TArray<uint64> DummyIndexMapping;
						Ar << DummyIndexMapping;
					}
				}
			}
		}
	}

	if (Ar.CustomVer(FSkeletalMeshCustomVersion::GUID) >= FSkeletalMeshCustomVersion::SkinWeightProfiles)
	{
		Ar << SkinWeightProfiles;
	}
}


void FSkeletalMeshLODModel::GetSectionFromVertexIndex(int32 InVertIndex, int32& OutSectionIndex, int32& OutVertIndex) const
{
	OutSectionIndex = 0;
	OutVertIndex = 0;

	int32 VertCount = 0;

	// Iterate over each chunk
	for (int32 SectionCount = 0; SectionCount < Sections.Num(); SectionCount++)
	{
		const FSkelMeshSection& Section = Sections[SectionCount];
		OutSectionIndex = SectionCount;

		// Is it in Soft vertex range?
		if (InVertIndex < VertCount + Section.GetNumVertices())
		{
			OutVertIndex = InVertIndex - VertCount;
			return;
		}
		VertCount += Section.GetNumVertices();
	}

	// InVertIndex should always be in some chunk!
	//check(false);
}

void FSkeletalMeshLODModel::GetVertices(TArray<FSoftSkinVertex>& Vertices) const
{
	Vertices.Empty(NumVertices);
	Vertices.AddUninitialized(NumVertices);

	// Initialize the vertex data
	// All chunks are combined into one (rigid first, soft next)
	FSoftSkinVertex* DestVertex = (FSoftSkinVertex*)Vertices.GetData();
	for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); SectionIndex++)
	{
		const FSkelMeshSection& Section = Sections[SectionIndex];
		FMemory::Memcpy(DestVertex, Section.SoftVertices.GetData(), Section.SoftVertices.Num() * sizeof(FSoftSkinVertex));
		DestVertex += Section.SoftVertices.Num();
	}
}

void FSkeletalMeshLODModel::GetClothMappingData(TArray<FMeshToMeshVertData>& MappingData, TArray<uint64>& OutClothIndexMapping) const
{
	for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); SectionIndex++)
	{
		const FSkelMeshSection& Section = Sections[SectionIndex];
		if (Section.ClothMappingData.Num())
		{
			uint64 KeyValue = ((uint64)Section.BaseVertexIndex << (uint32)32) | (uint64)MappingData.Num();
			OutClothIndexMapping.Add(KeyValue);
			MappingData += Section.ClothMappingData;
		}
		else
		{
			OutClothIndexMapping.Add(0);
		}
	}
}

void FSkeletalMeshLODModel::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize) const
{
	CumulativeResourceSize.AddUnknownMemoryBytes(Sections.GetAllocatedSize());
	CumulativeResourceSize.AddUnknownMemoryBytes(ActiveBoneIndices.GetAllocatedSize());
	CumulativeResourceSize.AddUnknownMemoryBytes(RequiredBones.GetAllocatedSize());
	CumulativeResourceSize.AddUnknownMemoryBytes(IndexBuffer.GetAllocatedSize());

	CumulativeResourceSize.AddUnknownMemoryBytes(RawPointIndices.GetBulkDataSize());
	CumulativeResourceSize.AddUnknownMemoryBytes(LegacyRawPointIndices.GetBulkDataSize());
	CumulativeResourceSize.AddUnknownMemoryBytes(MeshToImportVertexMap.GetAllocatedSize());
}

bool FSkeletalMeshLODModel::HasClothData() const
{
	for (int32 SectionIdx = 0; SectionIdx < Sections.Num(); SectionIdx++)
	{
		if (Sections[SectionIdx].HasClothingData())
		{
			return true;
		}
	}
	return false;
}

int32 FSkeletalMeshLODModel::NumNonClothingSections() const
{
	const int32 NumSections = Sections.Num();
	int32 Count = 0;

	for (int32 SectionIndex = 0; SectionIndex < NumSections; SectionIndex++)
	{
		if(!Sections[SectionIndex].HasClothingData())
		{
			Count++;
		}
	}

	return Count;
}

int32 FSkeletalMeshLODModel::GetNumNonClothingVertices() const
{
	int32 NumVerts = 0;
	int32 NumSections = Sections.Num();

	for (int32 i = 0; i < NumSections; i++)
	{
		const FSkelMeshSection& Section = Sections[i];

		// Stop when we hit clothing sections
		if (Section.ClothingData.AssetGuid.IsValid())
		{
			continue;
		}

		NumVerts += Section.SoftVertices.Num();
	}

	return NumVerts;
}

void FSkeletalMeshLODModel::GetNonClothVertices(TArray<FSoftSkinVertex>& OutVertices) const
{
	// Get the number of sections to copy
	int32 NumSections = Sections.Num();

	// Count number of verts
	int32 NumVertsToCopy = GetNumNonClothingVertices();

	OutVertices.Empty(NumVertsToCopy);
	OutVertices.AddUninitialized(NumVertsToCopy);

	// Initialize the vertex data
	// All chunks are combined into one (rigid first, soft next)
	FSoftSkinVertex* DestVertex = (FSoftSkinVertex*)OutVertices.GetData();
	for (int32 SectionIndex = 0; SectionIndex < NumSections; SectionIndex++)
	{
		const FSkelMeshSection& Section = Sections[SectionIndex];
		if (Section.ClothingData.AssetGuid.IsValid())
		{
			continue;
		}
		FMemory::Memcpy(DestVertex, Section.SoftVertices.GetData(), Section.SoftVertices.Num() * sizeof(FSoftSkinVertex));
		DestVertex += Section.SoftVertices.Num();
	}
}

bool FSkeletalMeshLODModel::DoSectionsNeedExtraBoneInfluences() const
{
	for (int32 SectionIdx = 0; SectionIdx < Sections.Num(); ++SectionIdx)
	{
		if (Sections[SectionIdx].HasExtraBoneInfluences())
		{
			return true;
		}
	}

	return false;
}

void FSkeletalMeshLODModel::SyncronizeUserSectionsDataArray(bool bResetNonUsedSection /*= false*/)
{
	int32 SectionNum = Sections.Num();
	for (int32 SectionIndex = 0; SectionIndex < SectionNum; ++SectionIndex)
	{
		FSkelMeshSection& Section = Sections[SectionIndex];
		FSkelMeshSourceSectionUserData& SectionUserData = UserSectionsData.FindOrAdd(Section.OriginalDataSectionIndex);
		Section.bCastShadow					= SectionUserData.bCastShadow;
		Section.bRecomputeTangent			= SectionUserData.bRecomputeTangent;
		Section.bDisabled					= SectionUserData.bDisabled;
		Section.GenerateUpToLodIndex		= SectionUserData.GenerateUpToLodIndex;
		Section.CorrespondClothAssetIndex	= SectionUserData.CorrespondClothAssetIndex;
		Section.ClothingData.AssetGuid		= SectionUserData.ClothingData.AssetGuid;
		Section.ClothingData.AssetLodIndex	= SectionUserData.ClothingData.AssetLodIndex;
	}

	//Reset normally happen when we re-import a skeletalmesh, we never want to reset this when we build the skeletalmesh (reduce can remove section, but we need to keep the original section data)
	if (bResetNonUsedSection)
	{
		//Make sure we have the correct amount of UserSectionData we delete all the entries and recreate them with the previously sync Sections
		UserSectionsData.Reset();
		for (int32 SectionIndex = 0; SectionIndex < SectionNum; ++SectionIndex)
		{
			FSkelMeshSection& Section = Sections[SectionIndex];
			//We only need parent section, no need to iterate bone chunked sections
			if (Section.ChunkedParentSectionIndex != INDEX_NONE)
			{
				continue;
			}
			FSkelMeshSourceSectionUserData& SectionUserData = UserSectionsData.FindOrAdd(Section.OriginalDataSectionIndex);
			SectionUserData.bCastShadow = Section.bCastShadow;
			SectionUserData.bRecomputeTangent = Section.bRecomputeTangent;
			SectionUserData.bDisabled = Section.bDisabled;
			SectionUserData.GenerateUpToLodIndex = Section.GenerateUpToLodIndex;
			SectionUserData.CorrespondClothAssetIndex = Section.CorrespondClothAssetIndex;
			SectionUserData.ClothingData.AssetGuid = Section.ClothingData.AssetGuid;
			SectionUserData.ClothingData.AssetLodIndex = Section.ClothingData.AssetLodIndex;
		}
	}
}

FString FSkeletalMeshLODModel::GetLODModelDeriveDataKey() const
{
	FString KeySuffix = TEXT("LODMODEL");

	TArray<uint8> ByteData;
	FMemoryWriter Ar(ByteData, true);

	//Add the bulk data ID (if someone modify the original imported data, this ID will change)
	FString BulkDatIDString = RawSkeletalMeshBulkDataID; //Need to re-assign to tmp var because function is const
	Ar << BulkDatIDString;
	int32 UserSectionCount = UserSectionsData.Num();
	Ar << UserSectionCount;
	for (auto Kvp : UserSectionsData)
	{
		Ar << Kvp.Key;
		Ar << Kvp.Value;
	}

	FSHA1 Sha;
	Sha.Update(ByteData.GetData(), ByteData.Num() * ByteData.GetTypeSize());
	Sha.Final();
	// Retrieve the hash and use it to construct a pseudo-GUID.
	uint32 Hash[5];
	Sha.GetHash((uint8*)Hash);
	KeySuffix += FGuid(Hash[0] ^ Hash[4], Hash[1], Hash[2], Hash[3]).ToString(EGuidFormats::Digits);

	return KeySuffix;
}

void FSkeletalMeshLODModel::UpdateChunkedSectionInfo(const FString& SkeletalMeshName, TArray<int32>& LODMaterialMap)
{
	int32 LODModelSectionNum = Sections.Num();
	//Fill the ChunkedParentSectionIndex data, we assume that every section using the same material are chunked
	int32 LastMaterialIndex = INDEX_NONE;
	uint32 LastBoneCount = 0;
	int32 CurrentParentChunkIndex = INDEX_NONE;
	int32 OriginalIndex = 0;
	const uint32 MaxGPUSkinBones = FGPUBaseSkinVertexFactory::GetMaxGPUSkinBones();
	check(MaxGPUSkinBones <= FGPUBaseSkinVertexFactory::GHardwareMaxGPUSkinBones);

	for (int32 LODModelSectionIndex = 0; LODModelSectionIndex < LODModelSectionNum; ++LODModelSectionIndex)
	{
		FSkelMeshSection& Section = Sections[LODModelSectionIndex];
		
		//If we have cloth on a chunked section we treat the chunked section has a parent section (this is to get the same result has before the refactor)
		if (LastBoneCount >= MaxGPUSkinBones && Section.MaterialIndex == LastMaterialIndex && !Section.ClothingData.AssetGuid.IsValid())
		{
			Section.ChunkedParentSectionIndex = CurrentParentChunkIndex;
			Section.OriginalDataSectionIndex = Sections[CurrentParentChunkIndex].OriginalDataSectionIndex;
			//In case of a child section that was BONE chunked ensure it has the same setting has the original section
			FSkelMeshSourceSectionUserData& SectionUserData = UserSectionsData.FindOrAdd(Section.OriginalDataSectionIndex);
			Section.bDisabled = SectionUserData.bDisabled;
			Section.bCastShadow = SectionUserData.bCastShadow;
			Section.bRecomputeTangent = SectionUserData.bRecomputeTangent;
			Section.GenerateUpToLodIndex = SectionUserData.GenerateUpToLodIndex;
			//Chunked section cannot have cloth, a cloth section will be a parent section
			Section.CorrespondClothAssetIndex = INDEX_NONE;
			Section.ClothingData.AssetGuid = FGuid();
			Section.ClothingData.AssetLodIndex = INDEX_NONE;
		}
		else
		{
			CurrentParentChunkIndex = LODModelSectionIndex;
			FSkelMeshSourceSectionUserData& SectionUserData = UserSectionsData.FindOrAdd(OriginalIndex);
			SectionUserData.bDisabled = Section.bDisabled;
			SectionUserData.bCastShadow = Section.bCastShadow;
			SectionUserData.bRecomputeTangent = Section.bRecomputeTangent;
			SectionUserData.GenerateUpToLodIndex = Section.GenerateUpToLodIndex;
			SectionUserData.CorrespondClothAssetIndex = Section.CorrespondClothAssetIndex;
			SectionUserData.ClothingData.AssetGuid = Section.ClothingData.AssetGuid;
			SectionUserData.ClothingData.AssetLodIndex = Section.ClothingData.AssetLodIndex;

			Section.OriginalDataSectionIndex = OriginalIndex++;
			Section.ChunkedParentSectionIndex = INDEX_NONE;
		}

		LastMaterialIndex = Section.MaterialIndex;
		//Set the last bone count
		LastBoneCount = (uint32)Sections[LODModelSectionIndex].BoneMap.Num();
		//its impossible to have more bone then the maximum allowed
		ensureMsgf(LastBoneCount <= MaxGPUSkinBones, TEXT("Skeletalmesh(%s) section %d have more bones then its alowed (MaxGPUSkinBones: %d)."), *SkeletalMeshName, LODModelSectionIndex, LastBoneCount);
	}
}

bool FSkeletalMeshLODModel::CopyStructure(FSkeletalMeshLODModel* Destination, FSkeletalMeshLODModel* Source)
{
	if (Source->RawPointIndices.IsLocked() || Source->LegacyRawPointIndices.IsLocked() || Source->RawSkeletalMeshBulkData_DEPRECATED.GetBulkData().IsLocked())
	{
		return false;
	}
	// Bulk data arrays need to be locked before a copy can be made.
	Source->RawPointIndices.Lock(LOCK_READ_ONLY);
	Source->LegacyRawPointIndices.Lock(LOCK_READ_ONLY);
	Source->RawSkeletalMeshBulkData_DEPRECATED.GetBulkData().Lock(LOCK_READ_ONLY);
	*Destination = *Source;
	Source->RawSkeletalMeshBulkData_DEPRECATED.GetBulkData().Unlock();
	Source->RawPointIndices.Unlock();
	Source->LegacyRawPointIndices.Unlock();

	return true;
}

#endif // WITH_EDITOR
