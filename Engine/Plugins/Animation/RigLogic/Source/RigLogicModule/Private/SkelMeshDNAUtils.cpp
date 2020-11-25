// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkelMeshDNAUtils.h"
#include "DNAToSkelMeshMap.h"

#include "RenderResource.h"
#include "RHICommandList.h"
#include "Async/ParallelFor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/ScopedTimers.h"
#include "Misc/FileHelper.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/SkeletalMesh.h"
#include "ComponentReregisterContext.h"
#include "ReferenceSkeleton.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"

#include "AnimationRuntime.h"

#include "riglogic/RigLogic.h"

DEFINE_LOG_CATEGORY(LogDNAUtils);

USkelMeshDNAUtils::USkelMeshDNAUtils(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FDNAToSkelMeshMap* USkelMeshDNAUtils::CreateMapForUpdatingNeutralMesh(IDNAReader* DNAReader, USkeletalMesh* SkelMesh)
{
#if WITH_EDITORONLY_DATA
	FDNAToSkelMeshMap* DNAToSkelMeshMap = new FDNAToSkelMeshMap();

	//only vertex map is initialized in this pass so we can mix neutral meshes fast (e.g. on slider move);
	//playing animation on a such a mesh requires updating joints and skin weights
	//getting full quality animation requires mixing morph targets too
	DNAToSkelMeshMap->InitBaseMesh(DNAReader, SkelMesh);
	return DNAToSkelMeshMap;
#else
	return nullptr;
#endif // WITH_EDITORONLY_DATA
}

#if WITH_EDITORONLY_DATA
/** Updates bind pose using joint positions from DNA. */
void USkelMeshDNAUtils::UpdateJoints(USkeletalMesh* SkelMesh, IDNAReader* DNAReader, FDNAToSkelMeshMap* DNAToSkelMeshMap)
{
	{	// Scoping of RefSkelModifier
		FReferenceSkeletonModifier RefSkelModifier(SkelMesh->GetRefSkeleton(), SkelMesh->GetSkeleton());

		// copy here
		TArray<FTransform> RawBonePose = SkelMesh->GetRefSkeleton().GetRawRefBonePose();

		// When we are mounting the head to different bodies than female average, we need to use
		// component space, as the joint to which the head root is snapped to will be on a different
		// position than in the head rig!

		// calculate component space ahead of current transform
		TArray<FTransform> ComponentTransforms;
		FAnimationRuntime::FillUpComponentSpaceTransforms(SkelMesh->GetRefSkeleton(), RawBonePose, ComponentTransforms);

		const TArray<FMeshBoneInfo>& RawBoneInfo = SkelMesh->GetRefSkeleton().GetRawRefBoneInfo();

		// Skipping root joint (index 0) to avoid blinking of the mesh due to bounding box issue
		for (uint16 JointIndex = 0; JointIndex < DNAReader->GetJointCount(); JointIndex++)
		{
			int32 BoneIndex = DNAToSkelMeshMap->GetUEBoneIndex(JointIndex);

			FTransform DNATransform = FTransform::Identity;

			// Updating bind pose affects just translations.
			FVector Translate = DNAReader->GetNeutralJointTranslation(JointIndex);
			DNATransform.SetTranslation(Translate);
			FVector RotationVector = DNAReader->GetNeutralJointRotation(JointIndex);
			FRotator Rotation(RotationVector.X, RotationVector.Y, RotationVector.Z);

			// Joint 0 (spine_04) is a root of GeneSplicer joint hierarchy, and is a special case
			// 1) it is parent to itself
			// 2) it is in DNA space, so we need to rotate it 90 degs on x axis to UE4 space
			// 3) the head joints below it in the skeletal mesh are not spliced, as they are not in DNA,
			//    so they will retain female average transforms

			if (DNAReader->GetJointParentIndex( JointIndex) == JointIndex )  //parent to itself
			{
				Rotation.Pitch += 90;
				DNATransform.SetRotation(Rotation.Quaternion());

				DNATransform.SetTranslation(FVector(Translate.X, Translate.Z, -Translate.Y));

				ComponentTransforms[BoneIndex] = DNATransform;

			}
			else
			{
				DNATransform.SetRotation(Rotation.Quaternion());

				if (ensure(RawBoneInfo[BoneIndex].ParentIndex != INDEX_NONE))
				{
					ComponentTransforms[BoneIndex] = DNATransform * ComponentTransforms[RawBoneInfo[BoneIndex].ParentIndex];
				}
			}

			ComponentTransforms[BoneIndex].NormalizeRotation();
		}

		for (uint16 BoneIndex = 0; BoneIndex < RawBoneInfo.Num(); BoneIndex++)
		{
			FTransform LocalTransform;
			        
			if (BoneIndex == 0)
			{
				LocalTransform = ComponentTransforms[BoneIndex];
			}
			else
			{
				LocalTransform = ComponentTransforms[BoneIndex].GetRelativeTransform(ComponentTransforms[RawBoneInfo[BoneIndex].ParentIndex]);
			}

			LocalTransform.NormalizeRotation();

			RefSkelModifier.UpdateRefPoseTransform(BoneIndex, LocalTransform);
		}
	}

	SkelMesh->GetRefBasesInvMatrix().Reset();
	SkelMesh->CalculateInvRefMatrices(); // Needs to be called after RefSkelModifier is destroyed
}

/** Updates base mesh vertices using data from DNA. */
void USkelMeshDNAUtils::UpdateBaseMesh(USkeletalMesh* SkelMesh, IDNAReader* DNAReader, FDNAToSkelMeshMap* DNAToSkelMeshMap, ELodUpdateOption UpdateOption)
{

	FSkeletalMeshModel* ImportedModel = SkelMesh->GetImportedModel();
	// Expects vertex map to be initialized beforehand	
	int32 LODStart;
	int32 LODEnd;
	GetLODRange(UpdateOption, ImportedModel->LODModels.Num(), LODStart, LODEnd);
	for (int32 LODIndex = LODStart; LODIndex < LODEnd; LODIndex++)
	{
		FSkeletalMeshLODModel& LODModel = ImportedModel->LODModels[LODIndex];
		int32 SectionIndex = 0;
		for (FSkelMeshSection& Section : LODModel.Sections)
		{
			int32& DNAMeshIndex = DNAToSkelMeshMap->ImportVtxToDNAMeshIndex[LODIndex][Section.GetVertexBufferIndex()];

			const int32 NumSoftVertices = Section.GetNumVertices();
			auto& OverlappingMap = DNAToSkelMeshMap->OverlappingVertices[LODIndex][SectionIndex];
			int32 VertexBufferIndex = Section.GetVertexBufferIndex();
			for (int32 VertexIndex = 0; VertexIndex < NumSoftVertices; VertexIndex++)
			{				
				int32& DNAVertexIndex = DNAToSkelMeshMap->ImportVtxToDNAVtxIndex[LODIndex][VertexBufferIndex];

				if (DNAVertexIndex >= 0)
				{
					const FVector Position = DNAReader->GetVertexPosition(DNAMeshIndex, DNAVertexIndex);
					FSoftSkinVertex& Vertex = Section.SoftVertices[VertexIndex];
					Vertex.Position = Position;

					// Check if the current vertex has overlapping vertices, and then update them as well.
					TArray<int32>& OverlappedIndices = OverlappingMap[VertexIndex];
					int32 OverlappingCount = OverlappedIndices.Num();
					for (int32 OverlappingIndex = 0; OverlappingIndex < OverlappingCount; ++OverlappingIndex)
					{
						int32 OverlappingVertexIndex = OverlappedIndices[OverlappingIndex];
						FSoftSkinVertex& OverlappingVertex = Section.SoftVertices[OverlappingVertexIndex];
						OverlappingVertex.Position = Position;
					}
				}
				VertexBufferIndex++;
			}
			SectionIndex++;
		}
	}
}

/** Updates Morph Targets using Blend Shapes from DNA.  */
void USkelMeshDNAUtils::UpdateMorphTargets(USkeletalMesh* SkelMesh, IDNAReader* DNAReader, FDNAToSkelMeshMap* DNAToSkelMeshMap, ELodUpdateOption UpdateOption)
{
	TArray<FDNABlendShapeTarget>& MeshBlendShapeTargets = DNAToSkelMeshMap->GetMeshBlendShapeTargets();
	if (MeshBlendShapeTargets.Num() == 0)
	{
		UE_LOG(LogDNAUtils, Warning, TEXT("No morph targets updated!"));
		return;
	}
	TArray<TArray<TArray<int32>>>& BlendShapeVertexDeltas = DNAToSkelMeshMap->GetBlendShapeVertexDeltaIndices();
	ParallelFor(SkelMesh->GetMorphTargets().Num(), [&](int32 MorphIndex)
	{
		UMorphTarget* MorphTarget = SkelMesh->GetMorphTargets()[MorphIndex];
		const FDNABlendShapeTarget& MeshTarget = MeshBlendShapeTargets[MorphIndex];
		// First get all DNA deltas for current Morph Target.
		TArrayView<const uint32> BlendShapeVertexIndices = DNAReader->GetBlendShapeTargetVertexIndices(MeshTarget.MeshIndex, MeshTarget.TargetIndex);
		if (BlendShapeVertexIndices.Num() > 0)
		{
			int32 LODStart;
			int32 LODEnd;
			GetLODRange(UpdateOption, MorphTarget->MorphLODModels.Num(), LODStart, LODEnd);
			for (int32 LODIndex = 0; LODIndex < LODEnd; LODIndex++)
			{
				// MorphTarget vertex indices refer to full vertex index buffer of imported mesh.
				FMorphTargetLODModel& MorphLODModel = MorphTarget->MorphLODModels[LODIndex];
				int32 NumDeltas = MorphLODModel.Vertices.Num();
				FMorphTargetDelta* Deltas = MorphLODModel.Vertices.GetData();
				for (int32 DeltaIndex = 0; DeltaIndex < NumDeltas; DeltaIndex++)
				{
					uint32 DeltaSourceIndex = Deltas[DeltaIndex].SourceIdx;
					int32 DNAVertexIndex = DNAToSkelMeshMap->ImportVtxToDNAVtxIndex[LODIndex][DeltaSourceIndex];
					if (DNAVertexIndex >= 0)
					{
						// Retrieve delta index from the mapping.
						int32 DNADeltaIndex = BlendShapeVertexDeltas[MeshTarget.MeshIndex][DNAVertexIndex][MeshTarget.TargetIndex];
						if (DNADeltaIndex != INDEX_NONE)
						{
							Deltas[DeltaIndex].PositionDelta = DNAReader->GetBlendShapeTargetDelta(MeshTarget.MeshIndex, MeshTarget.TargetIndex, DNADeltaIndex);
						}
						else
						{
							#ifdef DEBUG
							UE_LOG(LogDNAUtils, Error, TEXT(" Deltas not found for DNA Vertex %d (UE Vertex %d), Mesh %d and Target Index %d"), DNAVertexIndex, DeltaSourceIndex, MeshTarget.MeshIndex, MeshTarget.TargetIndex);
							#endif 
							// Delta not found. Reset it to (0, 0, 0).
							Deltas[DeltaIndex].PositionDelta = FVector(0, 0, 0);
						}
					}
					else
					{
						#ifdef DEBUG
						UE_LOG(LogDNAUtils, Error, TEXT(" UE Vertex %d not found in DNA for Mesh %d and TargetIndex %d"), DeltaSourceIndex, MeshTarget.MeshIndex, MeshTarget.TargetIndex);
						#endif 
					}
				}
			}
		}
		else
		{
			#ifdef DEBUG
			UE_LOG(LogDNAUtils, Warning, TEXT(" 0 deltas found for mesh %d and blend shape target %d"), MeshTarget.MeshIndex, MeshTarget.TargetIndex);
			#endif 
		}
	});	
}

/* Updates Bone influences using Skin Weights from DNA. */
void USkelMeshDNAUtils::UpdateSkinWeights(USkeletalMesh* SkelMesh, IDNAReader* DNAReader, FDNAToSkelMeshMap* DNAToSkelMeshMap, ELodUpdateOption UpdateOption)
{
	FSkeletalMeshModel* ImportedModel = SkelMesh->GetImportedModel();

	bool InfluenceMismatch = false;

	int32 LODStart;
	int32 LODEnd;
	GetLODRange(UpdateOption, ImportedModel->LODModels.Num(), LODStart, LODEnd);
	for (int32 LODIndex = LODStart; LODIndex < LODEnd; ++LODIndex)
	{
		FSkeletalMeshLODModel& LODModel = ImportedModel->LODModels[LODIndex];
		for (int32 SectionIndex = 0; SectionIndex < LODModel.Sections.Num(); SectionIndex++)
		{
			FSkelMeshSection& Section = LODModel.Sections[SectionIndex];
			int32 DNAMeshIndex = DNAToSkelMeshMap->ImportVtxToDNAMeshIndex[LODIndex][Section.GetVertexBufferIndex()];
			/*
			if (UpdateOption == ELodUpdateOption::LOD0Only && !DNAToSkelMeshMap->LOD0MeshIndices.Contains(DNAMeshIndex)) continue;
			*/

			//const uint16 DNAMaxInfluencePerVertex = DNAReader->getMaximumInfluencePerVertex(DNAMeshIndex);

			const int32 NumEngineVertices = Section.GetNumVertices();			
			for (int32 VertexIndex = 0; VertexIndex < NumEngineVertices; VertexIndex++)
			{
				const int32 VertexBufferIndex = VertexIndex + Section.GetVertexBufferIndex();
				int32 DNAVertexIndex = DNAToSkelMeshMap->ImportVtxToDNAVtxIndex[LODIndex][VertexBufferIndex];

				if (DNAVertexIndex < 0) continue; // Skip vertex not in DNA.

				TArrayView<const float> DNASkinWeights = DNAReader->GetSkinWeightsValues(DNAMeshIndex, DNAVertexIndex);
				TArrayView<const uint16> DNASkinJoints = DNAReader->GetSkinWeightsJointIndices(DNAMeshIndex, DNAVertexIndex);
				uint16 SkinJointNum = DNASkinJoints.Num();

				uint16 WeightsSum = 0;  // store all influences to vertex to ensure they add up to 255 (fix rounding errors)
				uint16 MaxInfluenceIndex = 0;
				uint16 MaxInfluenceWeight = 0;

				FSoftSkinVertex& Vertex = Section.SoftVertices[VertexIndex];

				// First we have to reset all influences that are not covered by DNA data.
				for (uint16 i = SkinJointNum; i < MAX_TOTAL_INFLUENCES; i++)
				{
					Vertex.InfluenceBones[i] = INDEX_NONE;
					Vertex.InfluenceWeights[i] = 0;
				}

				for (uint16 InfluenceIndex = 0; InfluenceIndex < FMath::Min(SkinJointNum, static_cast<uint16>(MAX_TOTAL_INFLUENCES)); ++InfluenceIndex)
				{
					uint16 EngineWeight = 0;
					// Find Engine bone for corresponding DNAJoint for the same influence.
					int32 UpdatedBoneId = DNAToSkelMeshMap->GetUEBoneIndex(DNASkinJoints[InfluenceIndex]);
					// BoneMap holds subset of bones belonging to current section.
					int32 BoneMapIndex = Section.BoneMap.Find(UpdatedBoneId);
					
					// Update which bone in the subset influences this vertex.
					Vertex.InfluenceBones[InfluenceIndex] = BoneMapIndex;
					if (BoneMapIndex != INDEX_NONE)
					{
						// Update influence weight.
						float PreRoundValue = 255.0f * DNASkinWeights[InfluenceIndex];
						EngineWeight = static_cast<uint16>(FMath::Min(255.0f, round(PreRoundValue)));  // convert RL float weight to 0-255 range
					}
					Vertex.InfluenceWeights[InfluenceIndex] = static_cast<uint8>(EngineWeight);
					WeightsSum += EngineWeight;

					if (EngineWeight > MaxInfluenceWeight)
					{
						MaxInfluenceIndex = InfluenceIndex;
						MaxInfluenceWeight = EngineWeight;
					}
				}
				// Add missing fraction to fill up to 255.
				int32 ValueToAdd = 255 - WeightsSum;
				uint8 OldValue = Vertex.InfluenceWeights[MaxInfluenceIndex];
				int32 NewValue = OldValue + ValueToAdd;
				Vertex.InfluenceWeights[MaxInfluenceIndex] = static_cast<uint8>(NewValue);
			}
		}
	}
}

/** Rebuilds render data from LODModel and inits resources. */
void USkelMeshDNAUtils::RebuildRenderData(USkeletalMesh* SkelMesh)
{
	FPlatformTime::InitTiming();

	double StartTime = FPlatformTime::Seconds();
	{
		SkelMesh->FlushRenderState();
	}
	double TimeToFlush = FPlatformTime::Seconds();
	{
		FSkeletalMeshRenderData* RenderData = SkelMesh->GetResourceForRendering();
		int32 LODIndex = 0;

		for (FSkeletalMeshLODRenderData& LODRenderData : RenderData->LODRenderData)
		{
			FSkeletalMeshLODModel& LODModelRef = SkelMesh->GetImportedModel()->LODModels[LODIndex];
			for (int32 i = 0; i < LODModelRef.Sections.Num(); i++)
			{
				FSkelMeshSection& ModelSection = LODModelRef.Sections[i];
				ModelSection.CalcMaxBoneInfluences();
				ModelSection.CalcUse16BitBoneIndex();
			}

			const FSkeletalMeshLODModel* LODModelPtr = &LODModelRef;
			LODRenderData.BuildFromLODModel(LODModelPtr, 0);
			LODIndex++;
		}
	}
	double TimeToRebuildModel = FPlatformTime::Seconds();
	{
		if (FApp::CanEverRender())
		{
			// Reinitialize the static mesh's resources.
			SkelMesh->InitResources();
		}
	}
	double TimeToInitResources = FPlatformTime::Seconds();
	{
		// Re-register scope
		TArray<UActorComponent*> ComponentsToReregister;
		for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
		{
			USkeletalMeshComponent* MeshComponent = *It;
			if (MeshComponent && !MeshComponent->IsTemplate() && MeshComponent->SkeletalMesh == SkelMesh)
			{
				ComponentsToReregister.Add(*It);
			}
		}
		FMultiComponentReregisterContext ReregisterContext(ComponentsToReregister);
	}
}

void USkelMeshDNAUtils::RebuildRenderData_VertexPosition(USkeletalMesh* SkelMesh)
{
	if (!FApp::CanEverRender())
	{
		return;
	}

	{
		FSkeletalMeshModel* MeshModel = SkelMesh->GetImportedModel();
		FSkeletalMeshRenderData* RenderData = SkelMesh->GetResourceForRendering();

		for (int32 LODIdx = 0; LODIdx < RenderData->LODRenderData.Num(); ++LODIdx)
		{
			FSkeletalMeshLODModel& LODModel = MeshModel->LODModels[LODIdx];
			FSkeletalMeshLODRenderData& LODRenderData = RenderData->LODRenderData[LODIdx];

			ENQUEUE_RENDER_COMMAND(FSkelMeshDNAUpdatePositions)
				([&LODModel, &LODRenderData](FRHICommandListImmediate& RHICmdList)
			{
				TArray<FSoftSkinVertex> Vertices;
				LODModel.GetVertices(Vertices);
				check(Vertices.Num() == LODRenderData.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices());
				LODRenderData.StaticVertexBuffers.PositionVertexBuffer.Init(Vertices.Num());
				for (int32 i = 0; i < Vertices.Num(); i++)
				{
					LODRenderData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(i) = Vertices[i].Position;
				}

				auto& VertexBuffer = LODRenderData.StaticVertexBuffers.PositionVertexBuffer;
				void* VertexBufferData = RHILockVertexBuffer(VertexBuffer.VertexBufferRHI, 0, VertexBuffer.GetNumVertices() * VertexBuffer.GetStride(), RLM_WriteOnly);
				FMemory::Memcpy(VertexBufferData, VertexBuffer.GetVertexData(), VertexBuffer.GetNumVertices() * VertexBuffer.GetStride());
				RHIUnlockVertexBuffer(VertexBuffer.VertexBufferRHI);
			});
		}
	}
}

void USkelMeshDNAUtils::UpdateJointBehavior(USkeletalMeshComponent* SkelMeshComponent)
{
    //DNAAsset->SetBehaviorReader is called before calling this method
    //it is not here to avoid having DNAAsset in the API, as in future we might want
	//to generalize SkelMeshUpdate to be dna-independent

	//the rig behavior has changed, we need to force re-initializing of RigLogic
	//this will set RigLogic RigUnit to initial state
	SkelMeshComponent->InitAnim(true);
}

#endif // WITH_EDITORONLY_DATA
