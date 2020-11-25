// Copyright Epic Games, Inc. All Rights Reserved.

#include "DNAToSkelMeshMap.h"

#include "DNAAsset.h"
#include "SkelMeshDNAReader.h"

#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "UObject/UObjectGlobals.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Animation/SmartName.h"
#include "Engine/SkeletalMesh.h"

#include "riglogic/RigLogic.h"

DEFINE_LOG_CATEGORY(LogDNAToSkelMeshMap);

#if WITH_EDITORONLY_DATA
/** Creates mappings between source DNA and target SkelMesh. */
void FDNAToSkelMeshMap::InitBaseMesh(IDNAReader* SourceDNAReader, USkeletalMesh* TargetSkeletalMesh)
{
	TargetSkelMesh = TargetSkeletalMesh; //maps are created for specific SkelMesh, so we can memorize it for clarity

	InitVertexMap(SourceDNAReader);

	//NOTE: this is just init for updating base mesh
	// before updating joints and morph targets, MapJoints and MapMorphTarget must be called respectively
}
#endif


/** Creates and initiates DNAReader and creates mapping between DNA and provided SkelMesh. */
bool FDNAToSkelMeshMap::InitFromDNAAsset(USkeletalMesh* SkelMesh)
{
#if WITH_EDITORONLY_DATA
	// Fetch DNAAsset from SkelMesh and use its StreamReader
	UAssetUserData* UserData = SkelMesh->GetAssetUserDataOfClass(UDNAAsset::StaticClass());
	if (UserData == nullptr)
	{
		FString PathName = GetPathNameSafe(SkelMesh);
		UE_LOG(LogDNAToSkelMeshMap, Warning, TEXT("Could not find DNAAsset user data for %s"), *PathName);
		return false;
	}
	UDNAAsset* DNAAsset = Cast<UDNAAsset>(UserData);
	FSkelMeshDNAReader SkelMeshDNAReader{ DNAAsset };
	InitBaseMesh(&SkelMeshDNAReader, SkelMesh);

	return true;
#else
	return false;
#endif //WITH_EDITORONLY_DATA
}

#if WITH_EDITORONLY_DATA
/** Maps vertices and meshes from SkelMesh to DNA. */
void FDNAToSkelMeshMap::InitVertexMap(IDNAReader* DNAReader)
{
	double StartTime = FPlatformTime::Seconds();

	ImportVtxToDNAMeshIndex.Empty();
	ImportVtxToDNAVtxIndex.Empty();

	FSkeletalMeshModel* ImportedModel = TargetSkelMesh->GetImportedModel();
	int32 NumEngineLods = ImportedModel->LODModels.Num();
	ImportVtxToDNAMeshIndex.AddZeroed(NumEngineLods);
	ImportVtxToDNAVtxIndex.AddZeroed(NumEngineLods);
	for (int32 i = 0; i < NumEngineLods; i++)
	{
		FSkeletalMeshLODModel& LODModel = ImportedModel->LODModels[i];
		ImportVtxToDNAMeshIndex[i].AddZeroed(LODModel.MeshToImportVertexMap.Num());
		ImportVtxToDNAVtxIndex[i].AddZeroed(LODModel.MeshToImportVertexMap.Num());
	}

	TArray<TArray<int32>> MeshStartIndices; // For each LOD, store the start index offset of each mesh.
	TArray<int32> TotalVertices; // Counts total indices for each mesh in DNA, per LOD.
	uint16 NumLODs = DNAReader->GetLODCount();
	MeshStartIndices.AddZeroed(NumLODs);
	TotalVertices.AddZeroed(NumLODs);
	ImportDNAVtxToUEVtxIndex.AddZeroed(NumLODs);
	// First find start indices for each mesh and for each LOD in DNA.
	for (uint16 LODIndex = 0; LODIndex < NumLODs; LODIndex++)
	{
		const TArrayView<const uint16> LODMeshIndices = DNAReader->GetMeshIndicesForLOD(LODIndex);

		for (size_t MeshIndex = 0; MeshIndex < LODMeshIndices.Num(); MeshIndex++)
		{
			MeshStartIndices[LODIndex].Add(TotalVertices[LODIndex]);
			const int32 VertexCount = DNAReader->GetVertexPositionCount(LODMeshIndices[MeshIndex]);
			TotalVertices[LODIndex] += VertexCount;
		}
		MeshStartIndices[LODIndex].Add(TotalVertices[LODIndex]);
		ImportDNAVtxToUEVtxIndex[LODIndex].Init(INDEX_NONE, TotalVertices[LODIndex]);
	}
	// Then for each LOD and each Vertex in LODModel Vertex Map find corresponding DNA Vertex.
	int32 ImportedVertexCount = 0;
	for (int32 LODIndex = 0; LODIndex < ImportedModel->LODModels.Num(); LODIndex++)
	{
		const FSkeletalMeshLODModel& LODModel = ImportedModel->LODModels[LODIndex];
		const TArrayView<const uint16> LODMeshIndices = DNAReader->GetMeshIndicesForLOD(LODIndex);
		const int32 StartIndicesCount = MeshStartIndices[LODIndex].Num();

		int32 LODMeshVtxCount = LODModel.MeshToImportVertexMap.Num();
		for (int32 LODMeshVtxIndex = 0; LODMeshVtxIndex < LODMeshVtxCount; LODMeshVtxIndex++)
		{
			int32 FbxVertexIndex = LODModel.MeshToImportVertexMap[LODMeshVtxIndex];
			bool bFound = false;
			// For each LODModel Vertex try to find the corresponding DNA Vertex Index by checking prepared list of Start Indices.
			for (int k = 1; k < StartIndicesCount; k++)
			{
				if (FbxVertexIndex < MeshStartIndices[LODIndex][k])
				{
					bFound = true;
					ImportVtxToDNAMeshIndex[LODIndex][LODMeshVtxIndex] = LODMeshIndices[k - 1];
					const int32 DNAVertexIndex = FbxVertexIndex - MeshStartIndices[LODIndex][k - 1];
					ImportVtxToDNAVtxIndex[LODIndex][LODMeshVtxIndex] = DNAVertexIndex;
					ImportDNAVtxToUEVtxIndex[LODIndex][DNAVertexIndex] = LODMeshVtxIndex;
					break;
				}
			}
			// If no matching DNA Vertex Index is found mark LODModel Vertex Index with -1.
			if (!bFound)
			{
#ifdef DEBUG
				UE_LOG(LogDNAToSkelMeshMap, Warning, TEXT("Not sorted fbx vertex found %d"), FbxVertexIndex);
#endif
				ImportVtxToDNAMeshIndex[LODIndex][LODMeshVtxIndex] = INDEX_NONE;
				ImportVtxToDNAVtxIndex[LODIndex][LODMeshVtxIndex] = INDEX_NONE;
			}
		}
	}

	// Find and map overlapping indices.
	OverlappingVertices.AddZeroed(NumEngineLods);
	for (int32 i = 0; i < NumEngineLods; i++)
	{
		FSkeletalMeshLODModel& LODModel = ImportedModel->LODModels[i];
		const int32 NumSections = LODModel.Sections.Num();
		OverlappingVertices[i].AddZeroed(NumSections);
		for (int32 SectionIdx = 0; SectionIdx < NumSections; SectionIdx++)
		{
			const FSkelMeshSection& Section = LODModel.Sections[SectionIdx];
			const int32 NumVertices = Section.GetNumVertices();

			TArray<bool> VerticesCovered;
			VerticesCovered.AddZeroed(NumVertices);

			OverlappingVertices[i][SectionIdx].AddZeroed(NumVertices);
			for (int32 VertexIndex = 0; VertexIndex < NumVertices; VertexIndex++)
			{
				const int32 VertexBufferIndex = Section.GetVertexBufferIndex() + VertexIndex;
				const int32 DNAVertexIndex = ImportVtxToDNAVtxIndex[i][VertexBufferIndex];
				if (DNAVertexIndex >= 0)
				{
					VerticesCovered[VertexIndex] = true;

					if (Section.OverlappingVertices.Contains(VertexIndex))
					{
						const int32 OverlappingCount = Section.OverlappingVertices[VertexIndex].Num();
						for (int32 OverlappingIndex = 0; OverlappingIndex < OverlappingCount; ++OverlappingIndex)
						{
							const int32 OverlappingVertexIndex = Section.OverlappingVertices[VertexIndex][OverlappingIndex];
							if (!VerticesCovered[OverlappingVertexIndex]) // No need to update the same vertex twice.
							{
								OverlappingVertices[i][SectionIdx][VertexIndex].Add(OverlappingVertexIndex);
							}
						}
					}
				}
			}
		}
	}

	double TimeToMap = FPlatformTime::Seconds();
	TimeToMap -= StartTime;

	UE_LOG(LogDNAToSkelMeshMap, Log, TEXT("	InitVertexMap:	%.6f"), TimeToMap * 1000.0f);
}

/** Makes a map of all Joints from DNA to Bones in Reference Skeleton.
 * Additionally makes a map of all Skinned joints. */
void FDNAToSkelMeshMap::MapJoints(IDNAReader* DNAReader)
{

	double StartTime = FPlatformTime::Seconds();

	FReferenceSkeleton* RefSkeleton = &TargetSkelMesh->GetRefSkeleton();
	uint32 JointCount = DNAReader->GetJointCount();

	// Map Joints to Bones.
	RLJointToUEBoneIndices.Empty();
	RLJointToUEBoneIndices.Reserve(JointCount);
	for (uint32 JntIndex = 0; JntIndex < JointCount; ++JntIndex)
	{
		const FString boneNameFStr = DNAReader->GetJointName(JntIndex);
		const FName BoneName(*boneNameFStr, 0, FNAME_Add, false);
		const int32 BoneIndex = RefSkeleton->FindBoneIndex(BoneName);

		// BoneIndex can be INDEX_NONE;
		// We can safely put it into the map with other indices, it will be handled in the Evaluate method.
		RLJointToUEBoneIndices.Add(BoneIndex);
	}


	double TimeToMap = FPlatformTime::Seconds();
	TimeToMap -= StartTime;

	UE_LOG(LogDNAToSkelMeshMap, Log, TEXT("	Map joints:	%.6f"), TimeToMap * 1000.0f);
}

void FDNAToSkelMeshMap::MapMorphTargets(IDNAReader* DNAReader)
{
	double StartTime = FPlatformTime::Seconds();

	uint16 MeshCount = DNAReader->GetMeshCount();
	//Initialize map which stores DeltaIndex for each Blend Shape Target, for each Vertex in each Mesh.
	BlendShapeVertexDeltaIndices.Empty();
	BlendShapeVertexDeltaIndices.AddZeroed(MeshCount);
	// Assemble the reverse map, for each MorphTarget index save corresponding FDNABlendShapeTarget Index
	// Keeping MeshIndex and TargetIndex for each MorphTarget.
	MeshBlendShapeTargets.Empty();
	MeshBlendShapeTargets.Init(FDNABlendShapeTarget(), TargetSkelMesh->GetMorphTargets().Num());

	for (int16 MeshIndex = 0; MeshIndex < MeshCount; MeshIndex++)
	{
		uint16 TargetCount = DNAReader->GetBlendShapeTargetCount(MeshIndex);
		for (int16 TargetIndex = 0; TargetIndex < TargetCount; TargetIndex++)
		{
			if (BlendShapeVertexDeltaIndices[MeshIndex].Num() == 0)
			{
				const int32 VertexCount = DNAReader->GetVertexPositionCount(MeshIndex);
				BlendShapeVertexDeltaIndices[MeshIndex].AddZeroed(VertexCount);
				for (int32 VertexIndex = 0; VertexIndex < VertexCount; VertexIndex++)
				{
					BlendShapeVertexDeltaIndices[MeshIndex][VertexIndex].Init(INDEX_NONE, TargetCount);
				}
			}

			// Construct MorphTarget name by combining Blend Shape Channel name and Mesh name from DNA.
			const uint16 ChannelIndex = DNAReader->GetBlendShapeChannelIndex(MeshIndex, TargetIndex);
			const FString BlendShapeStr = DNAReader->GetBlendShapeChannelName(ChannelIndex);
			const FString MeshStr = DNAReader->GetMeshName(MeshIndex);
			const FString MorphTargetStr = MeshStr + TEXT("__") + BlendShapeStr;
			const FName MorphTargetName(*MorphTargetStr, 0, FNAME_Add, false);

			// Find MorphTarget from SkelMesh by previously constructed name.
			int32 MorphTargetIndex;
			UMorphTarget* MorphTarget = TargetSkelMesh->FindMorphTargetAndIndex(MorphTargetName, MorphTargetIndex);
			if (MorphTarget != nullptr)
			{
				// Store MeshIndex and TargetIndex for found MorphTarget.
				MeshBlendShapeTargets[MorphTargetIndex].MeshIndex = MeshIndex;
				MeshBlendShapeTargets[MorphTargetIndex].TargetIndex = TargetIndex;

				const TArrayView<const uint32> BlendShapeTargetVertices = DNAReader->GetBlendShapeTargetVertexIndices(MeshIndex, TargetIndex);
				for (int32 DeltaIndex = 0; DeltaIndex < BlendShapeTargetVertices.Num(); DeltaIndex++)
				{
					// For each BlendShape Target Delta find corresponding Vertex and store Delta Index for this Vertex and Target in the map.
					const int32 DNAVertex = BlendShapeTargetVertices[DeltaIndex];
					BlendShapeVertexDeltaIndices[MeshIndex][DNAVertex][TargetIndex] = DeltaIndex;
				}
			}
			else
			{
#ifdef DEBUG
				UE_LOG(LogDNAToSkelMeshMap, Error, TEXT("Could not find morph target %s (probably below threshold) Channel %d, Target %d"), *MorphTargetStr, ChannelIndex, TargetIndex);
#endif
			}
		}
	}

	double TimeToMap = FPlatformTime::Seconds();
	TimeToMap -= StartTime;

	//UE_LOG(LogDNAToSkelMeshMap, Log, TEXT("	MapMorphTargets:	%.6f"), TimeToMap * 1000.0f);
}
#endif // WITH_EDITORONLY_DATA