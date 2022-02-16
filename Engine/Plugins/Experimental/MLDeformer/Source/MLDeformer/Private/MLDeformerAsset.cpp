// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformerAsset.h"
#include "MLDeformer.h"
#include "MLDeformerVizSettings.h"
#include "MLDeformerInputInfo.h"
#include "GeometryCache.h"
#include "GeometryCacheMeshData.h"
#include "GeometryCacheTrack.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "NeuralNetwork.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/AnimDataModel.h"
#include "CurveReference.h"

#define LOCTEXT_NAMESPACE "MLDeformerAsset"

void FVertexMapBuffer::InitRHI()
{
	if (VertexMap.Num() > 0)
	{
		FRHIResourceCreateInfo CreateInfo(TEXT("FVertexMapBuffer"));

		VertexBufferRHI = RHICreateVertexBuffer(VertexMap.Num() * sizeof(uint32), BUF_Static | BUF_ShaderResource, CreateInfo);
		uint32* Data = reinterpret_cast<uint32*>(RHILockBuffer(VertexBufferRHI, 0, VertexMap.Num() * sizeof(uint32), RLM_WriteOnly));
		for (int32 Index = 0; Index < VertexMap.Num(); ++Index)
		{
			Data[Index] = static_cast<uint32>(VertexMap[Index]);
		}
		RHIUnlockBuffer(VertexBufferRHI);
		VertexMap.Empty();

		ShaderResourceViewRHI = RHICreateShaderResourceView(VertexBufferRHI, 4, PF_R32_UINT);
	}
	else
	{
		VertexBufferRHI = nullptr;
		ShaderResourceViewRHI = nullptr;
	}
}

UMLDeformerAsset::UMLDeformerAsset(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	VizSettings = ObjectInitializer.CreateEditorOnlyDefaultSubobject<UMLDeformerVizSettings>(this, TEXT("VizSettings"));
#endif
}

void UMLDeformerAsset::Serialize(FArchive& Archive)
{
#if WITH_EDITOR
	if (Archive.IsSaving() && Archive.IsPersistent())
	{
		InitVertexMap();
		UpdateCachedNumVertices();
	}
#endif

	Super::Serialize(Archive);
}

void UMLDeformerAsset::PostLoad()
{
	Super::PostLoad();
	if (NeuralNetwork)
	{
		NeuralNetwork->SetDeviceType(/*DeviceType*/ENeuralDeviceType::GPU, /*InputDeviceType*/ENeuralDeviceType::CPU, /*OutputDeviceType*/ENeuralDeviceType::GPU);
	}

#if WITH_EDITOR
	UpdateCachedNumVertices();

	// Initialize the input info if we didn't store any yet.
	// This is just for backward compatibility. Normally this data is always there.
	if (InputInfo.GetNumBones() == 0 && InputInfo.GetNumCurves() == 0)
	{
		InputInfo = CreateInputInfo();
	}
	else
#endif
	{
		InputInfo.UpdateFNames();
	}

	InitGPUData();
}

// Used for the FBoenReference, so it knows what skeleton to pick bones from.
USkeleton* UMLDeformerAsset::GetSkeleton(bool& bInvalidSkeletonIsError, const IPropertyHandle* PropertyHandle)
{
#if WITH_EDITOR
	bInvalidSkeletonIsError = false;
	if (SkeletalMesh)
	{
		return SkeletalMesh->GetSkeleton();
	}
#endif
	return nullptr;
}

#if WITH_EDITOR
// A fuzzy name match.
// There is a match when the track name starts with the mesh name.
bool IsPotentialMatch(const FString& TrackName, const FString& MeshName)
{
	return (TrackName.Find(MeshName) == 0);
}

void UMLDeformerAsset::GenerateMeshMappings(USkeletalMesh* SkelMesh, UGeometryCache* GeomCache, TArray<FMLDeformerMeshMapping>& OutMeshMappings, TArray<FString>& OutFailedImportedMeshNames)
{
	OutMeshMappings.Empty();
	OutFailedImportedMeshNames.Empty();
	if (SkelMesh == nullptr || GeomCache == nullptr)
	{
		return;
	}

	// If we haven't got any imported mesh infos then the asset needs to be reimported first.
	// We show an error for this in the editor UI already.
	FSkeletalMeshModel* ImportedModel = SkelMesh->GetImportedModel();
	check(ImportedModel);
	const TArray<FSkelMeshImportedMeshInfo>& SkelMeshInfos = ImportedModel->LODModels[0].ImportedMeshInfos;
	if (SkelMeshInfos.IsEmpty())
	{
		return;
	}

	// For all meshes in the skeletal mesh.
	const float SampleTime = 0.0f;
	FString SkelMeshName;
	for (int32 SkelMeshIndex = 0; SkelMeshIndex < SkelMeshInfos.Num(); ++SkelMeshIndex)
	{
		const FSkelMeshImportedMeshInfo& MeshInfo = SkelMeshInfos[SkelMeshIndex];
		SkelMeshName = MeshInfo.Name.ToString();

		// Find the matching one in the geom cache.
		bool bFoundMatch = false;
		for (int32 TrackIndex = 0; TrackIndex < GeomCache->Tracks.Num(); ++TrackIndex)
		{
			// Check if this is a candidate based on the mesh and track name.
			UGeometryCacheTrack* Track = GeomCache->Tracks[TrackIndex];
			const bool bIsSoloMesh = (GeomCache->Tracks.Num() == 1 && SkelMeshInfos.Num() == 1);	// Do we just have one mesh and one track?
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
					continue;
				}

				// Get the number of geometry cache mesh imported verts.
				int32 NumGeomMeshVerts = 0;
				for (int32 GeomVertIndex = 0; GeomVertIndex < GeomCacheMeshData.ImportedVertexNumbers.Num(); ++GeomVertIndex)
				{
					NumGeomMeshVerts = FMath::Max(NumGeomMeshVerts, (int32)GeomCacheMeshData.ImportedVertexNumbers[GeomVertIndex]);
				}
				NumGeomMeshVerts += 1;	// +1 Because we use indices, so a cube's max index is 7, while there are 8 vertices.

				// Make sure the vertex counts match.
				const int32 NumSkelMeshVerts = MeshInfo.NumVertices;
				if (NumSkelMeshVerts != NumGeomMeshVerts)
				{
					continue;
				}

				// Create a new mesh mapping entry.
				OutMeshMappings.AddDefaulted();
				FMLDeformerMeshMapping& Mapping = OutMeshMappings.Last();
				Mapping.MeshIndex = SkelMeshIndex;
				Mapping.TrackIndex = TrackIndex;
				Mapping.SkelMeshToTrackVertexMap.AddUninitialized(NumSkelMeshVerts);
				Mapping.ImportedVertexToRenderVertexMap.AddUninitialized(NumSkelMeshVerts);

				// For all vertices (both skel mesh and geom cache mesh have the same number of verts here).
				for (int32 VertexIndex = 0; VertexIndex < NumSkelMeshVerts; ++VertexIndex)
				{
					// Find the first vertex with the same dcc vertex in the geom cache mesh.
					// When there are multiple vertices with the same vertex number here, they are duplicates with different normals or uvs etc.
					// However they all share the same vertex position, so we can just find the first hit, as we only need the position later on.
					const int32 GeomCacheVertexIndex = GeomCacheMeshData.ImportedVertexNumbers.Find(VertexIndex);
					Mapping.SkelMeshToTrackVertexMap[VertexIndex] = GeomCacheVertexIndex;
					
					// Map the source asset vertex number to a render vertex. This is the first duplicate of that vertex.
					const int32 RenderVertexIndex = ImportedModel->LODModels[0].MeshToImportVertexMap.Find(MeshInfo.StartImportedVertex + VertexIndex);
					Mapping.ImportedVertexToRenderVertexMap[VertexIndex] = RenderVertexIndex;
				}

				// We found a match, no need to iterate over more Tracks.
				bFoundMatch = true;
				break;
			} // If the track name matches the skeletal meshes internal mesh name.
		} // For all tracks.

		if (!bFoundMatch)
		{
			OutFailedImportedMeshNames.Add(SkelMeshName);
			UE_LOG(LogMLDeformer, Warning, TEXT("Imported mesh '%s' cannot be matched with a geometry cache track."), *SkelMeshName);
		}
	} // For all meshes inside the skeletal mesh.
}

FText UMLDeformerAsset::GetBaseAssetChangedErrorText() const
{
	FText Result;

	if (SkeletalMesh)
	{
		if (NumSkeletalMeshVerts != GetInputInfo().GetNumBaseMeshVertices() &&
			NumSkeletalMeshVerts > 0 && GetInputInfo().GetNumBaseMeshVertices() > 0)
		{
			Result = FText::Format(LOCTEXT("BaseMeshMismatch", "Number of vertices in base mesh has changed from {0} to {1} vertices since this ML Deformer Asset was saved! {2}"),
				GetInputInfo().GetNumBaseMeshVertices(),
				NumSkeletalMeshVerts,
				NeuralNetwork ? LOCTEXT("BaseMeshMismatchNN", "Neural network needs to be retrained!") : FText());
		}
	}

	return Result;
}

FText UMLDeformerAsset::GetTargetAssetChangedErrorText() const
{
	FText Result;

	if (GeometryCache)
	{
		if (NumGeomCacheVerts != GetInputInfo().GetNumTargetMeshVertices() &&
			NumGeomCacheVerts > 0 && GetInputInfo().GetNumTargetMeshVertices() > 0)
		{
			Result = FText::Format(LOCTEXT("TargetMeshMismatch", "Number of vertices in target mesh has changed from {0} to {1} vertices since this ML Deformer Asset was saved! {2}"),
				GetInputInfo().GetNumTargetMeshVertices(),
				NumGeomCacheVerts,
				NeuralNetwork ? LOCTEXT("BaseMeshMismatchNN", "Neural network needs to be retrained!") : FText());
		}
	}

	return Result;
}

FText UMLDeformerAsset::GetSkeletalMeshNeedsReimportErrorText() const
{
	FText Result;

	if (SkeletalMesh)
	{
		FSkeletalMeshModel* ImportedModel = SkeletalMesh->GetImportedModel();
		check(ImportedModel);

		const TArray<FSkelMeshImportedMeshInfo>& SkelMeshInfos = ImportedModel->LODModels[0].ImportedMeshInfos;
		if (SkelMeshInfos.IsEmpty())
		{
			Result = LOCTEXT("SkelMeshNeedsReimport", "Skeletal Mesh asset needs to be reimported.");
		}
	}

	return Result;
}

void UMLDeformerAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	static const FName SkeletalMeshName = GET_MEMBER_NAME_CHECKED(UMLDeformerAsset, SkeletalMesh);
	const FName PropertyName = PropertyChangedEvent.Property->GetFName();
	if (PropertyName == SkeletalMeshName)
	{
		InitVertexMap();
		InitGPUData();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

int32 UMLDeformerAsset::GetNumFrames() const
{
	if (GeometryCache == nullptr)
	{
		return 0;
	}

	const int32 StartFrame = GeometryCache->GetStartFrame();
	const int32 EndFrame = GeometryCache->GetEndFrame();
	check(EndFrame >= StartFrame);
	return (EndFrame - StartFrame) + 1;
}
#endif

void UMLDeformerAsset::BeginDestroy()
{
	Super::BeginDestroy();
	BeginReleaseResource(&VertexMapBuffer);
	RenderResourceDestroyFence.BeginFence();
}

bool UMLDeformerAsset::IsReadyForFinishDestroy()
{
	return Super::IsReadyForFinishDestroy() && RenderResourceDestroyFence.IsFenceComplete();
}

void UMLDeformerAsset::SetInferenceNeuralNetwork(UNeuralNetwork* InNeuralNetwork)
{
	NeuralNetworkModifyDelegate.Broadcast();
	NeuralNetwork = InNeuralNetwork;
}

#if WITH_EDITOR
void UMLDeformerAsset::InitVertexMap()
{
	VertexMap.Empty();
	if (SkeletalMesh)
	{
		FSkeletalMeshModel* SkeletalMeshModel = SkeletalMesh->GetImportedModel();
		if (SkeletalMeshModel)
		{
			VertexMap = SkeletalMeshModel->LODModels[0].MeshToImportVertexMap;
		}
	}
}

void UMLDeformerAsset::InitBoneIncludeListToAnimatedBonesOnly()
{
	if (!AnimSequence)
	{
		UE_LOG(LogMLDeformer, Warning, TEXT("Cannot initialize bone list as no Anim Sequence has been picked."));
		return;
	}

	const UAnimDataModel* DataModel = AnimSequence->GetDataModel();
	if (!DataModel)
	{
		UE_LOG(LogMLDeformer, Warning, TEXT("Anim sequence has no data model."));
		return;
	}

	if (!SkeletalMesh)
	{
		UE_LOG(LogMLDeformer, Warning, TEXT("Skeletal Mesh has not been set."));
		return;
	}

	USkeleton* Skeleton = SkeletalMesh->GetSkeleton();
	if (!Skeleton)
	{
		UE_LOG(LogMLDeformer, Warning, TEXT("Skeletal Mesh has no skeleton."));
		return;
	}

	// Iterate over all bones that are both in the skeleton and the animation.
	TArray<FName> AnimatedBoneList;
	const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
	const int32 NumBones = RefSkeleton.GetNum();
	for (int32 Index = 0; Index < NumBones; ++Index)
	{
		const FName BoneName = RefSkeleton.GetBoneName(Index);
		const int32 BoneTrackIndex = DataModel->GetBoneTrackIndexByName(BoneName);
		if (BoneTrackIndex == INDEX_NONE)
		{
			continue;
		}

		// Check if there is actually animation data.
		const FBoneAnimationTrack& BoneAnimTrack = DataModel->GetBoneTrackByIndex(BoneTrackIndex);
		const TArray<FQuat4f>& Rotations = BoneAnimTrack.InternalTrackData.RotKeys;
		bool bIsAnimated = false;
		if (!Rotations.IsEmpty())
		{
			const FQuat4f FirstQuat = Rotations[0];
			for (const FQuat4f KeyValue : Rotations)
			{
				if (!KeyValue.Equals(FirstQuat))
				{
					bIsAnimated = true;
					break;
				}
			}

			if (!bIsAnimated)
			{
				UE_LOG(LogMLDeformer, Display, TEXT("Bone '%s' has keyframes but isn't animated."), *BoneName.ToString());
			}
		}

		if (bIsAnimated)
		{
			AnimatedBoneList.Add(BoneName);
		}
	}

	// Init the bone include list using the animated bones.
	if (!AnimatedBoneList.IsEmpty())
	{
		BoneIncludeList.Empty();
		BoneIncludeList.Reserve(AnimatedBoneList.Num());
		for (FName BoneName : AnimatedBoneList)
		{
			BoneIncludeList.AddDefaulted();
			FBoneReference& BoneRef = BoneIncludeList.Last();
			BoneRef.BoneName = BoneName;
		}
	}
	else
	{
		BoneIncludeList.Empty();
		UE_LOG(LogMLDeformer, Warning, TEXT("There are no animated bone rotations in Anim Sequence '%s'."), *AnimSequence->GetName());
	}
}

void UMLDeformerAsset::InitCurveIncludeListToAnimatedCurvesOnly()
{
	if (!AnimSequence)
	{
		UE_LOG(LogMLDeformer, Warning, TEXT("Cannot initialize curve list as no Anim Sequence has been picked."));
		return;
	}

	const UAnimDataModel* DataModel = AnimSequence->GetDataModel();
	if (!DataModel)
	{
		UE_LOG(LogMLDeformer, Warning, TEXT("Anim sequence has no data model."));
		return;
	}

	if (!SkeletalMesh)
	{
		UE_LOG(LogMLDeformer, Warning, TEXT("Skeletal Mesh has not been set."));
		return;
	}

	USkeleton* Skeleton = SkeletalMesh->GetSkeleton();
	if (!Skeleton)
	{
		UE_LOG(LogMLDeformer, Warning, TEXT("Skeletal Mesh has no skeleton."));
		return;
	}

	// Iterate over all curves that are both in the skeleton and the animation.
	TArray<FName> AnimatedCurveList;
	const FSmartNameMapping* Mapping = Skeleton->GetSmartNameContainer(USkeleton::AnimCurveMappingName);
	if (Mapping)
	{
		TArray<FName> SkeletonCurveNames;
		Mapping->FillNameArray(SkeletonCurveNames);
		for (const FName& SkeletonCurveName : SkeletonCurveNames)
		{
			const TArray<FFloatCurve>& AnimCurves = DataModel->GetFloatCurves();
			for (const FFloatCurve& AnimCurve : AnimCurves)
			{
				if (AnimCurve.Name.IsValid() && AnimCurve.Name.DisplayName == SkeletonCurveName)
				{
					TArray<float> TimeValues;
					TArray<float> KeyValues;
					AnimCurve.GetKeys(TimeValues, KeyValues);
					if (KeyValues.Num() > 0)
					{
						const float FirstKeyValue = KeyValues[0];					
						for (float CurKeyValue : KeyValues)
						{
							if (CurKeyValue != FirstKeyValue)
							{
								AnimatedCurveList.Add(SkeletonCurveName);
								break;
							}
						}
					}
					break;
				}
			}
		}
	}

	// Init the bone include list using the animated bones.
	if (!AnimatedCurveList.IsEmpty())
	{
		CurveIncludeList.Empty();
		CurveIncludeList.Reserve(AnimatedCurveList.Num());
		for (FName CurveName : AnimatedCurveList)
		{
			CurveIncludeList.AddDefaulted();
			FCurveReference& CurveRef = CurveIncludeList.Last();
			CurveRef.CurveName = CurveName;
		}
	}
	else
	{
		CurveIncludeList.Empty();
		UE_LOG(LogMLDeformer, Warning, TEXT("There are no animated curves in Anim Sequence '%s'."), *AnimSequence->GetName());
	}
}

int32 UMLDeformerAsset::GetNumFramesForTraining() const 
{ 
	return FMath::Min(GetNumFrames(), GetTrainingFrameLimit()); 
}

FText UMLDeformerAsset::GetGeomCacheErrorText(UGeometryCache* InGeomCache) const
{
	FText Result;
	if (InGeomCache)
	{
		FString ErrorString;

		// Verify that we have imported vertex numbers enabled.
		TArray<FGeometryCacheMeshData> MeshData;
		InGeomCache->GetMeshDataAtTime(0.0f, MeshData);
		if (MeshData.Num() == 0)
		{
			ErrorString = FText(LOCTEXT("TargetMeshNoMeshData", "No geometry data is present.")).ToString();
		}
		else
		{
			if (MeshData[0].ImportedVertexNumbers.Num() == 0)
			{
				ErrorString = FText(LOCTEXT("TargetMeshNoImportedVertexNumbers", "Please import Geometry Cache with option 'Store Imported Vertex Numbers' enabled!")).ToString();
			}
		}

		// Check if we flattened the tracks.
		if (InGeomCache->Tracks.Num() == 1 && InGeomCache->Tracks[0]->GetName() == TEXT("Flattened_Track"))
		{
			int32 NumSkelMeshes = 0;
			if (SkeletalMesh)
			{
				FSkeletalMeshModel* Model = SkeletalMesh->GetImportedModel();
				if (Model)
				{
					NumSkelMeshes = Model->LODModels[0].ImportedMeshInfos.Num();		
				}
			}

			if (NumSkelMeshes > 1)
			{
				if (!ErrorString.IsEmpty())
				{
					ErrorString += TEXT("\n\n");
				}
				ErrorString += FText(LOCTEXT("TargetMeshFlattened", "Please import Geometry Cache with option 'Flatten Tracks' disabled!")).ToString();
			}
		}

		Result = FText::FromString(ErrorString);
	}

	return Result;
}

FText UMLDeformerAsset::GetMeshMappingErrorText() const
{
	FText Result;
	if (GeometryCache && SkeletalMesh)
	{
		// Check for failed mesh mappings.
		TArray<FMLDeformerMeshMapping> MeshMappings;
		TArray<FString> FailedNames;
		UMLDeformerAsset::GenerateMeshMappings(SkeletalMesh, GeometryCache, MeshMappings, FailedNames);

		// List all mesh names that have issues.
		FString ErrorString;
		for (int32 Index = 0; Index < FailedNames.Num(); ++Index)
		{
			ErrorString += FailedNames[Index];
			if (Index < FailedNames.Num() - 1)
			{
				ErrorString += TEXT("\n");
			}
		}

		Result = FText::FromString(ErrorString);
	}
	return Result;
}

FText UMLDeformerAsset::GetVertexErrorText(USkeletalMesh* InSkelMesh, UGeometryCache* InGeomCache, const FText& SkelName, const FText& GeomCacheName) const
{
	FText Result;

	if (InSkelMesh && InGeomCache)
	{
		const int32 SkelVertCount = UMLDeformerAsset::ExtractNumImportedSkinnedVertices(InSkelMesh);
		const int32 GeomCacheVertCount = UMLDeformerAsset::ExtractNumImportedGeomCacheVertices(InGeomCache);
		const bool bHasGeomCacheError = !UMLDeformerAsset::GetGeomCacheErrorText(InGeomCache).IsEmpty();
		if (SkelVertCount != GeomCacheVertCount && !bHasGeomCacheError)
		{
			Result = FText::Format(
				LOCTEXT("MeshVertexNumVertsMismatch", "Vertex count of {0} doesn't match with {1}!\n\n{2} has {3} verts, while {4} has {5} verts."),
				SkelName,
				GeomCacheName,
				SkelName,
				SkelVertCount,
				GeomCacheName,
				GeomCacheVertCount);
		}
	}

	return Result;
}

FText UMLDeformerAsset::GetAnimSequenceErrorText(UGeometryCache* InGeomCache, UAnimSequence* InAnimSequence) const
{
	FText Result;
	if (InAnimSequence && InGeomCache)
	{
		const float AnimSeqDuration = InAnimSequence->GetPlayLength();
		const float GeomCacheDuration = InGeomCache->CalculateDuration();
		if (FMath::Abs(AnimSeqDuration - GeomCacheDuration) > 0.001f)
		{
			FNumberFormattingOptions Options;
			Options.SetUseGrouping(false);
			Options.SetMaximumFractionalDigits(4);
			Result = FText::Format(
				LOCTEXT("AnimSeqNumFramesMismatch", "Anim sequence and Geometry Cache durations don't match!\n\nAnimSeq has a duration of {0} seconds, while GeomCache has a duration of {1} seconds.\n\nThis can produce incorrect results."),
				FText::AsNumber(AnimSeqDuration, &Options),
				FText::AsNumber(GeomCacheDuration, &Options));
		}
	}
	return Result;
}

FText UMLDeformerAsset::GetInputsErrorText() const
{
	if (SkeletalMesh && CreateInputInfo().IsEmpty())
	{
		switch (TrainingInputs)			
		{
			case ETrainingInputs::BonesOnly:		return FText(LOCTEXT("InputsEmptyBonesErrorText", "Your base mesh has no bones to train on."));
			case ETrainingInputs::CurvesOnly:		return FText(LOCTEXT("InputsEmptyCurvesErrorText", "Your base mesh has no curves to train on."));
			case ETrainingInputs::BonesAndCurves:	return FText(LOCTEXT("InputsEmptyBonesCurvesErrorText", "Your base mesh has no bones or curves to train on."));
			default: return FText(LOCTEXT("InputsEmptyDefaultErrorText", "There are no inputs to train on. There are no bones, curves or other inputs we can use."));
		}
	}

	return FText();
}

FText UMLDeformerAsset::GetIncompatibleSkeletonErrorText(USkeletalMesh* InSkelMesh, UAnimSequence* InAnimSeq) const
{
	FText Result;
	if (InSkelMesh && InAnimSeq)
	{
		if (!InSkelMesh->GetSkeleton()->IsCompatible(InAnimSeq->GetSkeleton()))
		{
			Result = LOCTEXT("SkeletonMismatch", "The base skeletal mesh and anim sequence use different skeletons. The animation might not play correctly.");
		}
	}
	return Result;
}

bool UMLDeformerAsset::IsCompatibleWithNeuralNet() const
{	
	if (SkeletalMesh == nullptr)
	{
		return true;
	}

	return GetInputInfo().IsCompatible(SkeletalMesh);
}

void UMLDeformerAsset::UpdateCachedNumVertices()
{
	NumSkeletalMeshVerts = UMLDeformerAsset::ExtractNumImportedSkinnedVertices(SkeletalMesh);
	NumGeomCacheVerts = UMLDeformerAsset::ExtractNumImportedGeomCacheVertices(GeometryCache);
}

FMLDeformerInputInfo UMLDeformerAsset::CreateInputInfo() const
{
	FMLDeformerInputInfo Result;

	// Init the inputs.
	FMLDeformerInputInfoInitSettings Settings;
	Settings.SkeletalMesh = SkeletalMesh;
	Settings.TargetMesh = GeometryCache;
	Settings.bIncludeBones = (TrainingInputs == ETrainingInputs::BonesAndCurves || TrainingInputs == ETrainingInputs::BonesOnly);
	Settings.bIncludeCurves = (TrainingInputs == ETrainingInputs::BonesAndCurves || TrainingInputs == ETrainingInputs::CurvesOnly);

	const USkeleton* Skeleton = SkeletalMesh.Get() ? SkeletalMesh->GetSkeleton() : nullptr;

	// Set the list of bones to use, from the bone references.
	if (!BoneIncludeList.IsEmpty() && Skeleton)
	{
		FString BoneName;
		for (const FBoneReference& BoneReference : BoneIncludeList)
		{
			if (BoneReference.BoneName.IsValid())
			{
				BoneName = BoneReference.BoneName.ToString();
				const int32 BoneId = Skeleton->GetReferenceSkeleton().FindRawBoneIndex(BoneReference.BoneName);
				if (BoneId != INDEX_NONE && !Settings.BoneNamesToInclude.Contains(BoneName))
				{
					Settings.BoneNamesToInclude.Add(BoneName);
					UE_LOG(LogMLDeformer, Display, TEXT("Including bone '%s' in training."), *BoneName);
				}
			}
		}
	}
	else
	{
		UE_LOG(LogMLDeformer, Display, TEXT("Including ALL bones of skeleton in training."));
	}

	// Set the list of curves to use, from the curve references.
	if (!CurveIncludeList.IsEmpty() && Skeleton)
	{
		const FSmartNameMapping* Mapping = Skeleton->GetSmartNameContainer(USkeleton::AnimCurveMappingName);
		if (Mapping)
		{
			// Get the list of curve names in the skeleton.
			TArray<FName> SkeletonCurveNames;
			Mapping->FillNameArray(SkeletonCurveNames);

			// Add only curves that are also in the skeleton.
			FString CurveName;
			for (const FCurveReference& CurveReference : CurveIncludeList)
			{
				if (CurveReference.CurveName.IsValid())
				{
					CurveName = CurveReference.CurveName.ToString();
					if (SkeletonCurveNames.Contains(CurveReference.CurveName))
					{
						UE_LOG(LogMLDeformer, Display, TEXT("Including curve '%s' in training."), *CurveName);
						Settings.CurveNamesToInclude.Add(CurveName);
					}
				}
			}
		}
	}
	else
	{
		UE_LOG(LogMLDeformer, Display, TEXT("Including ALL curves of skeleton in training."));
	}

	Result.Init(Settings);
	return Result;
}

int32 UMLDeformerAsset::ExtractNumImportedSkinnedVertices(USkeletalMesh* SkeletalMesh)
{
	return SkeletalMesh ? SkeletalMesh->GetNumImportedVertices() : 0;
}

int32 UMLDeformerAsset::ExtractNumImportedGeomCacheVertices(UGeometryCache* GeomCache)
{
	if (GeomCache == nullptr)
	{
		return 0;
	}

	int32 NumGeomCacheImportedVerts = 0;

	// Extract the geom cache number of imported vertices.
	TArray<FGeometryCacheMeshData> MeshDatas;
	GeomCache->GetMeshDataAtTime(0.0f, MeshDatas);
	for (const FGeometryCacheMeshData& MeshData : MeshDatas)
	{
		const TArray<uint32>& ImportedVertexNumbers = MeshData.ImportedVertexNumbers;
		if (ImportedVertexNumbers.Num() > 0)
		{
			// Find the maximum value.
			int32 MaxIndex = -1;
			for (int32 Index = 0; Index < ImportedVertexNumbers.Num(); ++Index)
			{
				MaxIndex = FMath::Max(static_cast<int32>(ImportedVertexNumbers[Index]), MaxIndex);
			}
			check(MaxIndex > -1);

			NumGeomCacheImportedVerts += MaxIndex + 1;
		}
	}

	return NumGeomCacheImportedVerts;
}

#endif

void UMLDeformerAsset::InitGPUData()
{
	BeginReleaseResource(&VertexMapBuffer);
	VertexMapBuffer.Init(VertexMap);
	BeginInitResource(&VertexMapBuffer);
}

#undef LOCTEXT_NAMESPACE
