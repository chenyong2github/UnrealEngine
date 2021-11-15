// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformerFrameCache.h"

#include "Animation/DebugSkelMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"

#include "GeometryCacheComponent.h"
#include "GeometryCache.h"
#include "GeometryCacheMeshData.h"
#include "GeometryCacheTrack.h"

#include "Math/NumericLimits.h"

void FMLDeformerTrainingFrame::Clear()
{
	VertexDeltas.Empty();
	BoneRotations.Empty();
	CurveValues.Empty();
	AnimFrameIndex = -1;
}

void FMLDeformerTrainingFrame::InitFromSamplerItem(int32 InAnimFrameIndex, const FMLDeformerSamplerData& InSamplerData)
{
	AnimFrameIndex = InAnimFrameIndex;
	VertexDeltas = InSamplerData.GetVertexDeltas();
	BoneRotations = InSamplerData.GetBoneRotations();
	CurveValues = InSamplerData.GetCurveValues();
}

SIZE_T FMLDeformerTrainingFrame::CalcMemUsageInBytes() const
{
	SIZE_T NumBytes = 0;
	NumBytes += VertexDeltas.GetAllocatedSize();
	NumBytes += BoneRotations.GetAllocatedSize();
	NumBytes += CurveValues.GetAllocatedSize();
	return NumBytes;
}

//------------------------------------------------------------

// Initializer a sampler item.
void FMLDeformerSamplerData::Init(const FInitSettings& InitSettings)
{
	check(InitSettings.Sampler);
	check(InitSettings.GeometryCacheComponent);
	check(InitSettings.SkeletalMeshComponent);
	check(InitSettings.SkeletalMeshComponent->SkeletalMesh);
	check(InitSettings.GeometryCacheComponent->GetGeometryCache());
	check(InitSettings.NumImportedVertices > 0);

	NumImportedVertices = InitSettings.NumImportedVertices;
	Sampler = InitSettings.Sampler;

	SkeletalMeshComponent = InitSettings.SkeletalMeshComponent;
	GeometryCacheComponent = InitSettings.GeometryCacheComponent;

	// Resize arrays.
	SkinnedVertexPositions.Empty();
	SkinnedVertexPositions.AddUninitialized(NumImportedVertices);

	VertexDeltas.Empty(); // Multiply by 3 as we store this as an array of floats instead of an FVector3f.

	BoneMatrices.Empty();
	BoneRotations.Empty();
	CurveValues.Empty();
}

void FMLDeformerSamplerData::Update(int32 InAnimFrameIndex)
{
	const int32 LODIndex = 0;

	AnimFrameIndex = InAnimFrameIndex;

	// Sample/tick the components at the exact frame time.
	const UMLDeformerAsset& DeformerAsset = Sampler->GetDeformerAsset();
	const float SampleTime = GeometryCacheComponent->GetTimeAtFrame(AnimFrameIndex);
	if (SkeletalMeshComponent && SkeletalMeshComponent->SkeletalMesh)
	{
		// Sample the transforms at the frame time and extract the bone rotations.
		SkeletalMeshComponent->SetPosition(SampleTime);
		SkeletalMeshComponent->bPauseAnims = true;
		SkeletalMeshComponent->RefreshBoneTransforms();
		const FMLDeformerInputInfo& InputInfo = DeformerAsset.GetInputInfo();
		InputInfo.ExtractBoneRotations(SkeletalMeshComponent, BoneRotations);

		// Get the bone transform matrices used during skinning, then extract the skinned positions.
		SkeletalMeshComponent->CacheRefToLocalMatrices(BoneMatrices);
		ExtractSkinnedPositions(LODIndex, BoneMatrices, TempVertexPositions, SkinnedVertexPositions);

		InputInfo.ExtractCurveValues(SkeletalMeshComponent, CurveValues);
	}

	// Sample the geometry cache vertices.
	if (GeometryCacheComponent && GeometryCacheComponent->GetGeometryCache())
	{
		// Tick the component.
		GeometryCacheComponent->SetManualTick(true);
		GeometryCacheComponent->TickAtThisTime(SampleTime, false, false, false);
	}

	// Calculate the vertex deltas.
	const float DeltaCutoffLength = DeformerAsset.GetDeltaCutoffLength();
	CalculateVertexDeltas(SkinnedVertexPositions, DeltaCutoffLength, VertexDeltas);
}

void FMLDeformerSamplerData::CalculateVertexDeltas(const TArray<FVector3f>& SkinnedPositions, float DeltaCutoffLength, TArray<float>& OutVertexDeltas) const
{
	const int32 NumVerts = SkinnedPositions.Num();
	OutVertexDeltas.Reset(NumVerts * 3);
	OutVertexDeltas.AddUninitialized(NumVerts * 3);

	UMLDeformerAsset* DeformerAsset = Sampler->GetInitSettings().DeformerAsset;
	USkeletalMesh* SkelMesh = DeformerAsset->GetSkeletalMesh();
	UGeometryCache* GeomCache = DeformerAsset->GetGeometryCache();

	const FTransform AlignmentTransform = DeformerAsset->GetAlignmentTransform();

	FSkeletalMeshModel* ImportedModel = SkelMesh->GetImportedModel();
	check(SkelMesh);
	check(GeomCache);
	check(ImportedModel);

	// Reset all deltas to zero.
	for (int32 Index = 0; Index < OutVertexDeltas.Num(); ++Index)
	{
		OutVertexDeltas[Index] = 0.0f;
	}

	// For all mesh mappings we found.
	const float SampleTime = GeometryCacheComponent->GetTimeAtFrame(AnimFrameIndex);
	const TArray<FSkelMeshImportedMeshInfo>& SkelMeshInfos = ImportedModel->LODModels[0].ImportedMeshInfos;
	for (int32 MeshMappingIndex = 0; MeshMappingIndex < Sampler->GetNumMeshMappings(); ++MeshMappingIndex)
	{
		const FMLDeformerMeshMapping& MeshMapping = Sampler->GetMeshMapping(MeshMappingIndex);
		const FSkelMeshImportedMeshInfo& MeshInfo = SkelMeshInfos[MeshMapping.MeshIndex];
		UGeometryCacheTrack* Track = GeomCache->Tracks[MeshMapping.TrackIndex];

		// Sample the mesh data of the geom cache.
		FGeometryCacheMeshData GeomCacheMeshData;
		if (!Track->GetMeshDataAtTime(SampleTime, GeomCacheMeshData))
		{
			continue;
		}

		// Calculate the vertex deltas.
		for (int32 VertexIndex = 0; VertexIndex < MeshInfo.NumVertices; ++VertexIndex)
		{
			const int32 SkinnedVertexIndex = MeshInfo.StartImportedVertex + VertexIndex;
			const int32 GeomCacheVertexIndex = MeshMapping.SkelMeshToTrackVertexMap[VertexIndex];
			if (GeomCacheVertexIndex != INDEX_NONE)
			{
				const FVector3f SkinnedVertexPos = SkinnedPositions[SkinnedVertexIndex];
				const FVector3f GeomCacheVertexPos = AlignmentTransform.TransformPosition(GeomCacheMeshData.Positions[GeomCacheVertexIndex]);
				const FVector3f Delta = GeomCacheVertexPos - SkinnedVertexPos;
				if (Delta.Length() < DeltaCutoffLength)
				{
					const int32 ArrayIndex = 3 * SkinnedVertexIndex;
					OutVertexDeltas[ArrayIndex] = Delta.X;
					OutVertexDeltas[ArrayIndex + 1] = Delta.Y;
					OutVertexDeltas[ArrayIndex + 2] = Delta.Z;
				}
			}
		}
	}
}

void FMLDeformerSamplerData::ExtractSkinnedPositions(int32 LODIndex, TArray<FMatrix44f>& InBoneMatrices, TArray<FVector3f>& TempPositions, TArray<FVector3f>& OutPositions) const
{
	OutPositions.Reset();
	TempPositions.Reset();

	if (SkeletalMeshComponent == nullptr)
	{
		return;
	}

	USkeletalMesh* Mesh = SkeletalMeshComponent->SkeletalMesh;
	if (Mesh == nullptr)
	{
		return;
	}

	FSkeletalMeshLODRenderData& SkelMeshLODData = Mesh->GetResourceForRendering()->LODRenderData[LODIndex];
	FSkinWeightVertexBuffer* SkinWeightBuffer = SkeletalMeshComponent->GetSkinWeightBuffer(LODIndex);
	USkeletalMeshComponent::ComputeSkinnedPositions(SkeletalMeshComponent, TempPositions, InBoneMatrices, SkelMeshLODData, *SkinWeightBuffer);

	// Get the originally imported vertex numbers from the DCC.
	const FSkeletalMeshModel* SkeletalMeshModel = Mesh->GetImportedModel();
	const TArray<int32>& ImportedVertexNumbers = SkeletalMeshModel->LODModels[LODIndex].MeshToImportVertexMap;
	if (ImportedVertexNumbers.Num() > 0)
	{
		// Store the vertex positions for the original imported vertices (8 vertices for a cube).
		OutPositions.AddZeroed(NumImportedVertices);
		for (int32 Index = 0; Index < TempPositions.Num(); ++Index)
		{
			const int32 ImportedVertex = ImportedVertexNumbers[Index];
			OutPositions[ImportedVertex] = TempPositions[Index];
		}
	}
}

int32 FMLDeformerSamplerData::GetNumBones() const
{
	const UMLDeformerAsset& DeformerAsset = Sampler->GetDeformerAsset();
	const FMLDeformerInputInfo& InputInfo = DeformerAsset.GetInputInfo();
	return InputInfo.GetNumBones();
}

SIZE_T FMLDeformerSamplerData::CalcMemUsageInBytes() const
{
	SIZE_T NumBytes = 0;
	NumBytes += SkinnedVertexPositions.GetAllocatedSize();
	NumBytes += TempVertexPositions.GetAllocatedSize();
	NumBytes += BoneMatrices.GetAllocatedSize();
	NumBytes += VertexDeltas.GetAllocatedSize();
	NumBytes += BoneRotations.GetAllocatedSize();
	NumBytes += CurveValues.GetAllocatedSize();
	return NumBytes;
}

//--------------------------------------------------------------------------------------------------------------

FMLDeformerSampler::~FMLDeformerSampler()
{
	if (SkelMeshActor)
	{
		SkelMeshActor->Destroy();
		SkelMeshActor = nullptr;
	}

	if (GeomCacheActor)
	{
		GeomCacheActor->Destroy();
		GeomCacheActor = nullptr;
	}
}

// Initialize the sampler.
void FMLDeformerSampler::Init(const FInitSettings& InInitSettings)
{
	check(InInitSettings.DeformerAsset);
	check(InInitSettings.DeformerAsset->SkeletalMesh);
	check(InInitSettings.DeformerAsset->GeometryCache);
	check(InInitSettings.DeformerAsset->AnimSequence);
	check(InInitSettings.World);

	// Copy the init settings.
	InitSettings = InInitSettings;

	// Create the skeletal mesh Actor.
	if (SkelMeshActor == nullptr)
	{
		SkelMeshActor = CreateActor(InitSettings.World, "SkelMeshSamplerActor");
		SkelMeshActor->SetActorTransform(FTransform::Identity);
	}

	// Create the geom cache actor.
	if (GeomCacheActor == nullptr)
	{
		GeomCacheActor = CreateActor(InitSettings.World, "GeomCacheSamplerActor");
		GeomCacheActor->SetActorTransform(InitSettings.DeformerAsset->GetAlignmentTransform());
	}

	// Extract the number vertices from the skeletal mesh and geometry cache, and make sure they match.
	UMLDeformerAsset* DeformerAsset = InitSettings.DeformerAsset;
	UGeometryCache* GeomCache = DeformerAsset->GetGeometryCache();
	USkeletalMesh* SkeletalMesh = DeformerAsset->GetSkeletalMesh();
	const int32 NumSkeletalMeshImportedVerts = UMLDeformerAsset::ExtractNumImportedSkinnedVertices(DeformerAsset->GetSkeletalMesh());
	const int32 NumGeomCacheImportedVerts = UMLDeformerAsset::ExtractNumImportedGeomCacheVertices(GeomCache);

	// Create and init the items.
	FMLDeformerSamplerData::FInitSettings DataInitSettings;
	DataInitSettings.Sampler = this;
	DataInitSettings.NumImportedVertices = NumSkeletalMeshImportedVerts;

	// Create the skeletal mesh component.
	UDebugSkelMeshComponent* SkelMeshComponent = SamplerData.GetSkeletalMeshComponent() ? SamplerData.GetSkeletalMeshComponent() : NewObject<UDebugSkelMeshComponent>(SkelMeshActor);
	UAnimSequence* TrainingAnimSequence = DeformerAsset->GetAnimSequence();
	SkelMeshComponent->SetSkeletalMesh(SkeletalMesh);
	SkelMeshComponent->RegisterComponent();
	DataInitSettings.SkeletalMeshComponent = SkelMeshComponent;
	SkelMeshComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	SkelMeshComponent->SetAnimation(TrainingAnimSequence);
	SkelMeshComponent->SetPosition(0.0f);
	SkelMeshComponent->SetPlayRate(1.0f);
	SkelMeshComponent->Play(false);
	SkelMeshComponent->SetVisibility(false);
	SkelMeshComponent->RefreshBoneTransforms();

	// Create the geometry cache component.
	UGeometryCacheComponent* GeomCacheComponent = SamplerData.GetGeometryCacheComponent() ? SamplerData.GetGeometryCacheComponent() : NewObject<UGeometryCacheComponent>(GeomCacheActor);
	GeomCacheComponent->SetGeometryCache(GeomCache);
	GeomCacheComponent->SetManualTick(true);
	GeomCacheComponent->RegisterComponent();
	GeomCacheComponent->SetVisibility(false);
	DataInitSettings.GeometryCacheComponent = GeomCacheComponent;

	SamplerData.Init(DataInitSettings);

	// Set the actor root components.
	SkelMeshActor->SetRootComponent(SkelMeshComponent);
	GeomCacheActor->SetRootComponent(GeomCacheComponent);

	UMLDeformerAsset::GenerateMeshMappings(InitSettings.DeformerAsset, MeshMappings, FailedImportedMeshNames);
}

// Spawn an actor.
AActor* FMLDeformerSampler::CreateActor(UWorld* InWorld, const FName& Name) const
{
	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = NAME_None;
	AActor* Actor = InWorld->SpawnActor<AActor>(SpawnParams);
	Actor->SetFlags(RF_Transient);
	return Actor;
}

int32 FMLDeformerSampler::GetNumVertices() const
{
	return SamplerData.GetNumImportedVertices();
}

int32 FMLDeformerSampler::GetNumBones() const
{
	return SamplerData.GetNumBones();
}

int32 FMLDeformerSampler::GetNumCurves() const
{
	return SamplerData.GetCurveValues().Num();
}

int32 FMLDeformerSampler::GetNumFrames() const
{
	return InitSettings.DeformerAsset->GetNumFrames();
}

SIZE_T FMLDeformerSampler::CalcMemUsageInBytes() const
{
	SIZE_T Result = 0;
	Result += SamplerData.CalcMemUsageInBytes();
	Result += MeshMappings.GetAllocatedSize();
	return Result;
}

//----------------------------------------------------------------------------------------------------------

void FMLDeformerFrameCache::Init(const FInitSettings& InitSettings)
{
	check(InitSettings.CacheSizeInBytes >= 0);	// 0 is allowed, as we then simply have a 1 frame cache.
	check(InitSettings.DeformerAsset);
	check(InitSettings.World);

	DeformerAsset = InitSettings.DeformerAsset;

	if (DeformerAsset->GetSkeletalMesh() == nullptr || DeformerAsset->GetGeometryCache() == nullptr || DeformerAsset->GetAnimSequence() == nullptr)
	{
		FrameMap.Empty();
		CachedTrainingFrames.Empty();
		NextFreeCacheIndex = 0;
		return;
	}

	// Initialize the sampler.
	FMLDeformerSampler::FInitSettings SamplerInitSettings;
	SamplerInitSettings.DeformerAsset = InitSettings.DeformerAsset;
	SamplerInitSettings.World = InitSettings.World;
	Sampler.Init(SamplerInitSettings);

	// Create the first training frame, and update it already so we know how much memory one frame takes.
	CachedTrainingFrames.Empty();
	CachedTrainingFrames.AddDefaulted();
	Sampler.GetSamplerData().Update(0);
	CachedTrainingFrames[0].InitFromSamplerItem(0, Sampler.GetSamplerData());
	const SIZE_T NumBytesPerTrainingFrame = CachedTrainingFrames[0].CalcMemUsageInBytes();

	// Calculate how many frames fit inside our budget.
	const int32 MaxCachedFrames = static_cast<int32>(InitSettings.CacheSizeInBytes / NumBytesPerTrainingFrame) + 1;
	const int32 NumCachedFrames = FMath::Min(DeformerAsset->GetNumFrames(), MaxCachedFrames);	// Never use more than the number of frames in the training anim.

	if (InitSettings.bLogCacheStats)
	{
		const SIZE_T EstimatedCacheSize = NumCachedFrames * NumBytesPerTrainingFrame;
		UE_LOG(LogMLDeformer, Display, TEXT("ML Deformer cache will use %d frames in %jd bytes (%.2f Mb)"), NumCachedFrames, EstimatedCacheSize, EstimatedCacheSize / (float)(1024 * 1024));
	}

	// Allocate the cached frames ahead of time.
	CachedTrainingFrames.AddDefaulted(NumCachedFrames - 1);	// -1 Because we already added one.

	// Init the frame mapping table.
	const int32 NumFrames = Sampler.GetNumFrames();
	FrameMap.Empty();
	FrameMap.AddUninitialized(NumFrames);
	for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
	{
		FrameMap[FrameIndex] = -1;	// -1 Means that this frame is not inside the list of cached frames.
	}

	NextFreeCacheIndex = 0;
}

int32 FMLDeformerFrameCache::GetNumVertices() const
{
	return Sampler.GetNumVertices();
}

int32 FMLDeformerFrameCache::GetNumBones() const
{
	return Sampler.GetNumBones();
}

int32 FMLDeformerFrameCache::GetNumCurves() const
{
	return Sampler.GetNumCurves();
}

int32 FMLDeformerFrameCache::GetCachedTrainingFrameIndex(int32 AnimFrameIndex) const
{
	return FrameMap.IsValidIndex(AnimFrameIndex) ? FrameMap[AnimFrameIndex] : -1;
}

void FMLDeformerFrameCache::Prefetch(int32 StartFrameIndex, int32 EndFrameIndex)
{
	check(IsValid());

	int32 NumPrefetched = 0;
	for (int32 AnimFrameIndex = StartFrameIndex; AnimFrameIndex <= EndFrameIndex; ++AnimFrameIndex)
	{
		// Generate the frame if we haven't got it cached already.
		const int32 CachedFrameIndex = GetCachedTrainingFrameIndex(AnimFrameIndex);
		if (CachedFrameIndex == -1)
		{
			GenerateFrame(AnimFrameIndex);
		}

		// We shouldn't prefetch more frames than the cache size.
		if (++NumPrefetched >= CachedTrainingFrames.Num())
		{
			break;
		}
	}
}

const FMLDeformerTrainingFrame& FMLDeformerFrameCache::GetTrainingFrameForAnimFrame(int32 AnimFrameIndex)
{
	check(IsValid());

	// Check if we already have this frame cached, and if not, generate it and insert it inside the cache.
	int32 CachedFrameIndex = GetCachedTrainingFrameIndex(AnimFrameIndex);
	if (CachedFrameIndex == -1)
	{
		CachedFrameIndex = GenerateFrame(AnimFrameIndex);	// This does heavy calculations and inserts the new frame in the cache, possibly replacing an older frame.
	}
	
	return CachedTrainingFrames[CachedFrameIndex];
}

int32 FMLDeformerFrameCache::GetNextCacheFrameIndex()
{
	const int32 Result = NextFreeCacheIndex;

	// Move the index to the next frame, wrap to the start of the cache buffer if needed.
	NextFreeCacheIndex++;
	if (NextFreeCacheIndex >= CachedTrainingFrames.Num())
	{
		// This prevents only caches the first calculated items, until the cache is full.
		// It will keep overwriting the last item and keep the first ones in tact.
		return CachedTrainingFrames.Num() - 1;	
	}

	return Result;
}

void FMLDeformerFrameCache::UpdateFrameMap()
{
	// First clear the values in the frame map.
	// Basically set them to a value that tells that this frame hasn't been cached yet.
	ResetFrameMap();

	// For all cached frames, update the frame map to point to that cache item.
	for (int32 Index = 0; Index < CachedTrainingFrames.Num(); ++Index)
	{
		const FMLDeformerTrainingFrame& TrainingFrame = CachedTrainingFrames[Index];
		const int32 AnimFrameIndex = TrainingFrame.GetAnimFrameIndex();
		if (AnimFrameIndex != -1)
		{
			FrameMap[AnimFrameIndex] = Index;
		}
	}
}

int32 FMLDeformerFrameCache::GenerateFrame(int32 AnimFrameIndex)
{
	check(AnimFrameIndex >= 0);
	check(!FrameMap.IsEmpty());

	// Make sure we're in a valid range.
	AnimFrameIndex = FMath::Clamp(AnimFrameIndex, 0, FrameMap.Num() - 1);

	// Let the sampler item generate the deltas.
	FMLDeformerSamplerData& SamplerData = Sampler.GetSamplerData();
	SamplerData.Update(AnimFrameIndex);

	// Extract the training data from the sampler item.
	// Find a spot in the cache that we should modify, and update the frame map.
	const int32 CachedFrameIndex = GetNextCacheFrameIndex();
	FMLDeformerTrainingFrame& TrainingFrame = CachedTrainingFrames[CachedFrameIndex];
	TrainingFrame.InitFromSamplerItem(AnimFrameIndex, SamplerData);
	UpdateFrameMap();

	return CachedFrameIndex;
}

SIZE_T FMLDeformerFrameCache::CalcMemUsageInBytes() const
{
	SIZE_T NumBytes = 0;
	for (const FMLDeformerTrainingFrame& Frame : CachedTrainingFrames)
	{
		NumBytes += Frame.CalcMemUsageInBytes();
	}
	NumBytes += Sampler.CalcMemUsageInBytes();
	return NumBytes;
}

void FMLDeformerFrameCache::ResetFrameMap()
{
	for (int32& Value : FrameMap)
	{
		Value = -1;
	}
}

void FMLDeformerFrameCache::Clear()
{
	ResetFrameMap();
	NextFreeCacheIndex = 0;
	for (FMLDeformerTrainingFrame& Frame : CachedTrainingFrames)
	{
		Frame.Clear();
	}
}

bool FMLDeformerFrameCache::IsValid() const
{
	return (!FrameMap.IsEmpty() && !CachedTrainingFrames.IsEmpty());
}
