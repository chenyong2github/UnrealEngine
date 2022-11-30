// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	UnSkeletalComponent.cpp: Actor component implementation.
=============================================================================*/

#include "Components/SkinnedMeshComponent.h"
#include "Misc/App.h"
#include "RenderingThread.h"
#include "GameFramework/PlayerController.h"
#include "ContentStreaming.h"
#include "DrawDebugHelpers.h"
#include "UnrealEngine.h"
#include "SkeletalRenderPublic.h"
#include "SkeletalRenderCPUSkin.h"
#include "SkeletalRenderGPUSkin.h"
#include "SkeletalRenderStatic.h"
#include "Animation/AnimStats.h"
#include "Engine/SkeletalMeshSocket.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "EngineGlobals.h"
#include "PrimitiveSceneProxy.h"
#include "Engine/CollisionProfile.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "SkeletalMeshTypes.h"
#include "Animation/MorphTarget.h"
#include "AnimationRuntime.h"
#include "Animation/SkinWeightProfileManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogSkinnedMeshComp, Log, All);

int32 GSkeletalMeshLODBias = 0;
FAutoConsoleVariableRef CVarSkeletalMeshLODBias(
	TEXT("r.SkeletalMeshLODBias"),
	GSkeletalMeshLODBias,
	TEXT("LOD bias for skeletal meshes (does not affect animation editor viewports)."),
	ECVF_Scalability
	);

static TAutoConsoleVariable<int32> CVarEnableAnimRateOptimization(
	TEXT("a.URO.Enable"),
	1,
	TEXT("True to anim rate optimization."));

static TAutoConsoleVariable<int32> CVarDrawAnimRateOptimization(
	TEXT("a.URO.Draw"),
	0,
	TEXT("True to draw color coded boxes for anim rate."));

static TAutoConsoleVariable<int32> CVarEnableMorphTargets(TEXT("r.EnableMorphTargets"), 1, TEXT("Enable Morph Targets"));

static TAutoConsoleVariable<int32> CVarAnimVisualizeLODs(
	TEXT("a.VisualizeLODs"),
	0,
	TEXT("Visualize SkelMesh LODs"));

namespace FAnimUpdateRateManager
{
	static float TargetFrameTimeForUpdateRate = 1.f / 30.f; //Target frame rate for lookahead URO

	// Bucketed group counters to stagged update an eval, used to initialise AnimUpdateRateShiftTag
	// for mesh params in the same shift group.
	struct FShiftBucketParameters
	{
		void SetFriendlyName(const EUpdateRateShiftBucket& InShiftBucket, const FName& InFriendlyName)
		{
			ShiftTagFriendlyNames[(uint8)InShiftBucket] = InFriendlyName;
		}

		const FName& GetFriendlyName(const EUpdateRateShiftBucket& InShiftBucket)
		{
			return ShiftTagFriendlyNames[(uint8)InShiftBucket];
		}

		static uint8 ShiftTagBuckets[(uint8)EUpdateRateShiftBucket::ShiftBucketMax];

	private:
		static FName ShiftTagFriendlyNames[(uint8)EUpdateRateShiftBucket::ShiftBucketMax];
	};
	uint8 FShiftBucketParameters::ShiftTagBuckets[] = {};
	FName FShiftBucketParameters::ShiftTagFriendlyNames[] = {};

	struct FAnimUpdateRateParametersTracker
	{
		FAnimUpdateRateParameters UpdateRateParameters;

		/** Frame counter to call AnimUpdateRateTick() just once per frame. */
		uint32 AnimUpdateRateFrameCount;

		/** Counter to stagger update and evaluation across skinned mesh components */
		uint8 AnimUpdateRateShiftTag;

		/** List of all USkinnedMeshComponents that use this set of parameters */
		TArray<USkinnedMeshComponent*> RegisteredComponents;

		FAnimUpdateRateParametersTracker() : AnimUpdateRateFrameCount(0), AnimUpdateRateShiftTag(0) {}

		uint8 GetAnimUpdateRateShiftTag(const EUpdateRateShiftBucket& ShiftBucket)
		{
			// If hasn't been initialized yet, pick a unique ID, to spread population over frames.
			if (AnimUpdateRateShiftTag == 0)
			{
				AnimUpdateRateShiftTag = ++FShiftBucketParameters::ShiftTagBuckets[(uint8)ShiftBucket];
			}

			return AnimUpdateRateShiftTag;
		}

		bool IsHumanControlled() const
		{
			AActor* Owner = RegisteredComponents[0]->GetOwner();
			APlayerController* Controller = Owner ? Owner->GetInstigatorController<APlayerController>() : nullptr;
			return Controller != nullptr;
		}
	};

	TMap<UObject*, FAnimUpdateRateParametersTracker*> ActorToUpdateRateParams;

	UObject* GetMapIndexForComponent(USkinnedMeshComponent* SkinnedComponent)
	{
		UObject* TrackerIndex = SkinnedComponent->GetOwner();
		if (TrackerIndex == nullptr)
		{
			TrackerIndex = SkinnedComponent;
		}
		return TrackerIndex;
	}

	FAnimUpdateRateParameters* GetUpdateRateParameters(USkinnedMeshComponent* SkinnedComponent)
	{
		if (!SkinnedComponent)
		{
			return NULL;
		}
		UObject* TrackerIndex = GetMapIndexForComponent(SkinnedComponent);

		FAnimUpdateRateParametersTracker** ExistingTrackerPtr = ActorToUpdateRateParams.Find(TrackerIndex);
		if (!ExistingTrackerPtr)
		{
			ExistingTrackerPtr = &ActorToUpdateRateParams.Add(TrackerIndex);
			(*ExistingTrackerPtr) = new FAnimUpdateRateParametersTracker();
		}
		
		check(ExistingTrackerPtr);
		FAnimUpdateRateParametersTracker* ExistingTracker = *ExistingTrackerPtr;
		check(ExistingTracker);
		checkSlow(!ExistingTracker->RegisteredComponents.Contains(SkinnedComponent)); // We have already been registered? Something has gone very wrong!

		ExistingTracker->RegisteredComponents.Add(SkinnedComponent);
		FAnimUpdateRateParameters* UpdateRateParams = &ExistingTracker->UpdateRateParameters;
		SkinnedComponent->OnAnimUpdateRateParamsCreated.ExecuteIfBound(UpdateRateParams);

		return UpdateRateParams;
	}

	void CleanupUpdateRateParametersRef(USkinnedMeshComponent* SkinnedComponent)
	{
		UObject* TrackerIndex = GetMapIndexForComponent(SkinnedComponent);

		FAnimUpdateRateParametersTracker* Tracker = ActorToUpdateRateParams.FindChecked(TrackerIndex);
		Tracker->RegisteredComponents.Remove(SkinnedComponent);
		if (Tracker->RegisteredComponents.Num() == 0)
		{
			ActorToUpdateRateParams.Remove(TrackerIndex);
			delete Tracker;
		}
	}

	static TAutoConsoleVariable<int32> CVarForceAnimRate(
		TEXT("a.URO.ForceAnimRate"),
		0,
		TEXT("Non-zero to force anim rate. 10 = eval anim every ten frames for those meshes that can do it. In some cases a frame is considered to be 30fps."));

	static TAutoConsoleVariable<int32> CVarForceInterpolation(
		TEXT("a.URO.ForceInterpolation"),
		0,
		TEXT("Set to 1 to force interpolation"));

	static TAutoConsoleVariable<int32> CVarURODisableInterpolation(
		TEXT("a.URO.DisableInterpolation"),
		0,
		TEXT("Set to 1 to disable interpolation"));

	void AnimUpdateRateSetParams(FAnimUpdateRateParametersTracker* Tracker, float DeltaTime, bool bRecentlyRendered, float MaxDistanceFactor, int32 MinLod, bool bNeedsValidRootMotion, bool bUsingRootMotionFromEverything)
	{
		// default rules for setting update rates

		// Human controlled characters should be ticked always fully to minimize latency w/ game play events triggered by animation.
		const bool bHumanControlled = Tracker->IsHumanControlled();

		bool bNeedsEveryFrame = bNeedsValidRootMotion && !bUsingRootMotionFromEverything;

		// Not rendered, including dedicated servers. we can skip the Evaluation part.
		if (!bRecentlyRendered)
		{
			const int32 NewUpdateRate = ((bHumanControlled || bNeedsEveryFrame) ? 1 : Tracker->UpdateRateParameters.BaseNonRenderedUpdateRate);
			const int32 NewEvaluationRate = Tracker->UpdateRateParameters.BaseNonRenderedUpdateRate;
			Tracker->UpdateRateParameters.SetTrailMode(DeltaTime, Tracker->GetAnimUpdateRateShiftTag(Tracker->UpdateRateParameters.ShiftBucket), NewUpdateRate, NewEvaluationRate, false);
		}
		// Visible controlled characters or playing root motion. Need evaluation and ticking done every frame.
		else  if (bHumanControlled || bNeedsEveryFrame)
		{
			Tracker->UpdateRateParameters.SetTrailMode(DeltaTime, Tracker->GetAnimUpdateRateShiftTag(Tracker->UpdateRateParameters.ShiftBucket), 1, 1, false);
		}
		else
		{
			int32 DesiredEvaluationRate = 1;

			if(!Tracker->UpdateRateParameters.bShouldUseLodMap)
			{
				DesiredEvaluationRate = Tracker->UpdateRateParameters.BaseVisibleDistanceFactorThesholds.Num() + 1;
				for(int32 Index = 0; Index < Tracker->UpdateRateParameters.BaseVisibleDistanceFactorThesholds.Num(); Index++)
				{
					const float& DistanceFactorThreadhold = Tracker->UpdateRateParameters.BaseVisibleDistanceFactorThesholds[Index];
					if(MaxDistanceFactor > DistanceFactorThreadhold)
					{
						DesiredEvaluationRate = Index + 1;
						break;
					}
				}
			}
			else
			{
				// Using LOD map which should have been set along with flag in custom delegate on creation.
				// if the map is empty don't throttle
				if(int32* FrameSkip = Tracker->UpdateRateParameters.LODToFrameSkipMap.Find(MinLod))
				{
					// Add 1 as an eval rate of 1 is 0 frameskip
					DesiredEvaluationRate = (*FrameSkip) + 1;
				}
				// We haven't found our LOD number into our array. :(
				// Default to matching settings of previous highest LOD number we've found.
				// For example if we're missing LOD 3, and we have settings for LOD 2, then match that.
				// Having no settings means we default to evaluating every frame, which is highest quality setting we have.
				// This is not what we want to higher LOD numbers.
				else if (Tracker->UpdateRateParameters.LODToFrameSkipMap.Num() > 0)
				{
					TMap<int32, int32>& LODToFrameSkipMap = Tracker->UpdateRateParameters.LODToFrameSkipMap;
					for (auto Iter = LODToFrameSkipMap.CreateConstIterator(); Iter; ++Iter)
					{
						if (Iter.Key() < MinLod)
						{
							DesiredEvaluationRate = FMath::Max(Iter.Value(), DesiredEvaluationRate);
						}
					}

					// Cache result back into TMap, so we don't have to do this every frame.
					LODToFrameSkipMap.Add(MinLod, DesiredEvaluationRate);

					// Add 1 as an eval rate of 1 is 0 frameskip
					DesiredEvaluationRate++;
				}
			}

			int32 ForceAnimRate = CVarForceAnimRate.GetValueOnGameThread();
			if (ForceAnimRate)
			{
				DesiredEvaluationRate = ForceAnimRate;
			}

			if (bUsingRootMotionFromEverything && DesiredEvaluationRate > 1)
			{
				//Use look ahead mode that allows us to rate limit updates even when using root motion
				Tracker->UpdateRateParameters.SetLookAheadMode(DeltaTime, Tracker->GetAnimUpdateRateShiftTag(Tracker->UpdateRateParameters.ShiftBucket), TargetFrameTimeForUpdateRate*DesiredEvaluationRate);
			}
			else
			{
				Tracker->UpdateRateParameters.SetTrailMode(DeltaTime, Tracker->GetAnimUpdateRateShiftTag(Tracker->UpdateRateParameters.ShiftBucket), DesiredEvaluationRate, DesiredEvaluationRate, true);
			}
		}
	}

	void AnimUpdateRateTick(FAnimUpdateRateParametersTracker* Tracker, float DeltaTime, bool bNeedsValidRootMotion)
	{
		// Go through components and figure out if they've been recently rendered, and the biggest MaxDistanceFactor
		bool bRecentlyRendered = false;
		bool bPlayingNetworkedRootMotionMontage = false;
		bool bUsingRootMotionFromEverything = true;
		float MaxDistanceFactor = 0.f;
		int32 MinLod = MAX_int32;

		const TArray<USkinnedMeshComponent*>& SkinnedComponents = Tracker->RegisteredComponents;
		for (USkinnedMeshComponent* Component : SkinnedComponents)
		{
			bRecentlyRendered |= Component->bRecentlyRendered;
			MaxDistanceFactor = FMath::Max(MaxDistanceFactor, Component->MaxDistanceFactor);
			bPlayingNetworkedRootMotionMontage |= Component->IsPlayingNetworkedRootMotionMontage();
			bUsingRootMotionFromEverything &= Component->IsPlayingRootMotionFromEverything();
			MinLod = FMath::Min(MinLod, Tracker->UpdateRateParameters.bShouldUseMinLod ? Component->MinLodModel : Component->PredictedLODLevel);
		}

		bNeedsValidRootMotion &= bPlayingNetworkedRootMotionMontage;

		// Figure out which update rate should be used.
		AnimUpdateRateSetParams(Tracker, DeltaTime, bRecentlyRendered, MaxDistanceFactor, MinLod, bNeedsValidRootMotion, bUsingRootMotionFromEverything);
	}

	const TCHAR* B(bool b)
	{
		return b ? TEXT("true") : TEXT("false");
	}

	void TickUpdateRateParameters(USkinnedMeshComponent* SkinnedComponent, float DeltaTime, bool bNeedsValidRootMotion)
	{
		// Convert current frame counter from 64 to 32 bits.
		const uint32 CurrentFrame32 = uint32(GFrameCounter % MAX_uint32);

		UObject* TrackerIndex = GetMapIndexForComponent(SkinnedComponent);
		FAnimUpdateRateParametersTracker* Tracker = ActorToUpdateRateParams.FindChecked(TrackerIndex);

		if (CurrentFrame32 != Tracker->AnimUpdateRateFrameCount)
		{
			Tracker->AnimUpdateRateFrameCount = CurrentFrame32;
			AnimUpdateRateTick(Tracker, DeltaTime, bNeedsValidRootMotion);
		}
	}
}

//////////////////////////////////////////////////////////////////////////

USkinnedMeshComponent::USkinnedMeshComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, AnimUpdateRateParams(nullptr)
{
	bAutoActivate = true;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;

	VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);

	StreamingDistanceMultiplier = 1.0f;
	bCanHighlightSelectedSections = false;
	CanCharacterStepUpOn = ECB_Owner;
#if WITH_EDITORONLY_DATA
	SectionIndexPreview = -1;
	MaterialIndexPreview = -1;

	SelectedEditorSection = INDEX_NONE;
	SelectedEditorMaterial = INDEX_NONE;
#endif // WITH_EDITORONLY_DATA
	bPerBoneMotionBlur = true;
	bCastCapsuleDirectShadow = false;
	bCastCapsuleIndirectShadow = false;
	CapsuleIndirectShadowMinVisibility = .1f;

	bDoubleBufferedComponentSpaceTransforms = true;
	CurrentEditableComponentTransforms = 0;
	CurrentReadComponentTransforms = 1;
	bNeedToFlipSpaceBaseBuffers = false;
	bBoneVisibilityDirty = false;

	bCanEverAffectNavigation = false;
	MasterBoneMapCacheCount = 0;
	bSyncAttachParentLOD = true;
	bIgnoreMasterPoseComponentLOD = false;

	CurrentBoneTransformRevisionNumber = 0;

	ExternalInterpolationAlpha = 0.0f;
	ExternalDeltaTime = 0.0f;
	ExternalTickRate = 1;
	bExternalInterpolate = false;
	bExternalUpdate = false;
	bExternalEvaluationRateLimited = false;
	bExternalTickRateControlled = false;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	bDrawDebugSkeleton = false;
#endif

	CurrentSkinWeightProfileName = NAME_None;
}


void USkinnedMeshComponent::UpdateMorphMaterialUsageOnProxy()
{
	// update morph material usage
	if (SceneProxy)
	{
		if (ActiveMorphTargets.Num() > 0 && SkeletalMesh->MorphTargets.Num() > 0)
		{
			TArray<UMaterialInterface*> MaterialUsingMorphTarget;
			for (UMorphTarget* MorphTarget : SkeletalMesh->MorphTargets)
			{
				if (!MorphTarget)
				{
					continue;
				}
				for (const FMorphTargetLODModel& MorphTargetLODModel : MorphTarget->MorphLODModels)
				{
					for (int32 SectionIndex : MorphTargetLODModel.SectionIndices)
					{
						for (int32 LodIdx = 0; LodIdx < SkeletalMesh->GetResourceForRendering()->LODRenderData.Num(); LodIdx++)
						{
							const FSkeletalMeshLODRenderData& LODModel = SkeletalMesh->GetResourceForRendering()->LODRenderData[LodIdx];
							if (LODModel.RenderSections.IsValidIndex(SectionIndex))
							{
								MaterialUsingMorphTarget.AddUnique(GetMaterial(LODModel.RenderSections[SectionIndex].MaterialIndex));
								MaterialUsingMorphTarget.AddUnique(GetSecondaryMaterial(LODModel.RenderSections[SectionIndex].MaterialIndex));
							}
						}
					}
				}
			}
			((FSkeletalMeshSceneProxy*)SceneProxy)->UpdateMorphMaterialUsage_GameThread(MaterialUsingMorphTarget);
		}
	}
}

void USkinnedMeshComponent::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
	Super::GetResourceSizeEx(CumulativeResourceSize);

	// Get Mesh Object's memory
	if (MeshObject)
	{
		MeshObject->GetResourceSizeEx(CumulativeResourceSize);
	}
}

FPrimitiveSceneProxy* USkinnedMeshComponent::CreateSceneProxy()
{
	LLM_SCOPE(ELLMTag::SkeletalMesh);
	ERHIFeatureLevel::Type SceneFeatureLevel = GetWorld()->FeatureLevel;
	FSkeletalMeshSceneProxy* Result = nullptr;
	FSkeletalMeshRenderData* SkelMeshRenderData = GetSkeletalMeshRenderData();

	// Only create a scene proxy for rendering if properly initialized
	if (SkelMeshRenderData &&
		SkelMeshRenderData->LODRenderData.IsValidIndex(PredictedLODLevel) &&
		!bHideSkin &&
		MeshObject)
	{
		// Only create a scene proxy if the bone count being used is supported, or if we don't have a skeleton (this is the case with destructibles)
		int32 MinLODIndex = ComputeMinLOD();
		int32 MaxBonesPerChunk = SkelMeshRenderData->GetMaxBonesPerSection(MinLODIndex);
		int32 MaxSupportedNumBones = MeshObject->IsCPUSkinned() ? MAX_int32 : FGPUBaseSkinVertexFactory::GetMaxGPUSkinBones();
		if (MaxBonesPerChunk <= MaxSupportedNumBones)
		{
			Result = ::new FSkeletalMeshSceneProxy(this, SkelMeshRenderData);
		}
	}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	SendRenderDebugPhysics(Result);
#endif

	return Result;
}

// UObject interface
// Override to have counting working better
void USkinnedMeshComponent::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	if (Ar.IsCountingMemory())
	{
		// add all native variables - mostly bigger chunks 
		ComponentSpaceTransformsArray[0].CountBytes(Ar);
		ComponentSpaceTransformsArray[1].CountBytes(Ar);
		MasterBoneMap.CountBytes(Ar);
	}
}

void USkinnedMeshComponent::OnRegister()
{
	LLM_SCOPE(ELLMTag::SkeletalMesh);

	// The reason this happens before register
	// is so that any transform update (or children transform update)
	// won't result in any issues of accessing SpaceBases
	// This isn't really ideal solution because these transform won't have 
	// any valid data yet. 

	AnimUpdateRateParams = FAnimUpdateRateManager::GetUpdateRateParameters(this);

	if (MasterPoseComponent.IsValid())
	{
		// we have to make sure it updates the mastesr pose
		SetMasterPoseComponent(MasterPoseComponent.Get(), true);
	}
	else
	{
		AllocateTransformData();
	}

	Super::OnRegister();

	if(FSceneInterface* Scene = GetScene())
	{
		CachedSceneFeatureLevel = Scene->GetFeatureLevel();
	}
	else
	{
		CachedSceneFeatureLevel = ERHIFeatureLevel::Num;
	}

	UpdateLODStatus();
	InvalidateCachedBounds();
}

void USkinnedMeshComponent::OnUnregister()
{
	DeallocateTransformData();
	Super::OnUnregister();

	if (AnimUpdateRateParams)
	{
		FAnimUpdateRateManager::CleanupUpdateRateParametersRef(this);
		AnimUpdateRateParams = nullptr;
	}
}

void USkinnedMeshComponent::CreateRenderState_Concurrent(FRegisterComponentContext* Context)
{
	LLM_SCOPE(ELLMTag::SkeletalMesh);

	if( SkeletalMesh )
	{
		// Attempting to track down UE-45505, where it looks as if somehow a skeletal mesh component's mesh has only been partially loaded, causing a mismatch in the LOD arrays
		checkf(!SkeletalMesh->HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad | RF_NeedPostLoadSubobjects | RF_WillBeLoaded), TEXT("Attempting to create render state for a skeletal mesh that is is not fully loaded. Mesh: %s"), *SkeletalMesh->GetName());

		// Initialize the alternate weight tracks if present BEFORE creating the new mesh object
		InitLODInfos();

		// No need to create the mesh object if we aren't actually rendering anything (see UPrimitiveComponent::Attach)
		if ( FApp::CanEverRender() && ShouldComponentAddToScene() )
		{
			ERHIFeatureLevel::Type SceneFeatureLevel = GetWorld()->FeatureLevel;
			FSkeletalMeshRenderData* SkelMeshRenderData = SkeletalMesh->GetResourceForRendering();
			int32 MinLODIndex = ComputeMinLOD();
			
#if DO_CHECK
			for (int LODIndex = MinLODIndex; LODIndex < SkelMeshRenderData->LODRenderData.Num(); LODIndex++)
			{
				FSkeletalMeshLODRenderData& LODData = SkelMeshRenderData->LODRenderData[LODIndex];
				const FPositionVertexBuffer* PositionVertexBufferPtr = &LODData.StaticVertexBuffers.PositionVertexBuffer;
				if (!PositionVertexBufferPtr || (PositionVertexBufferPtr->GetNumVertices() <= 0))
				{
					UE_LOG(LogSkinnedMeshComp, Warning, TEXT("Invalid Lod %i for Rendering Asset: %s"), LODIndex, *SkeletalMesh->GetFullName());
				}
			}
#endif
	
			// Also check if skeletal mesh has too many bones/chunk for GPU skinning.
			if (bRenderStatic)
			{
				// GPU skin vertex buffer + LocalVertexFactory
				MeshObject = ::new FSkeletalMeshObjectStatic(this, SkelMeshRenderData, SceneFeatureLevel); 
			}
			else if(ShouldCPUSkin())
			{
				MeshObject = ::new FSkeletalMeshObjectCPUSkin(this, SkelMeshRenderData, SceneFeatureLevel);
			}
			// don't silently enable CPU skinning for unsupported meshes, just do not render them, so their absence can be noticed and fixed
			else if (!SkelMeshRenderData->RequiresCPUSkinning(SceneFeatureLevel, MinLODIndex)) 
			{
				MeshObject = ::new FSkeletalMeshObjectGPUSkin(this, SkelMeshRenderData, SceneFeatureLevel);
			}
			else
			{
				int32 MaxBonesPerChunk = SkelMeshRenderData->GetMaxBonesPerSection(MinLODIndex);
				int32 MaxSupportedGPUSkinBones = FGPUBaseSkinVertexFactory::GetMaxGPUSkinBones();
				int32 NumBoneInfluences = SkelMeshRenderData->GetNumBoneInfluences(MinLODIndex);
				FString FeatureLevelName; GetFeatureLevelName(SceneFeatureLevel, FeatureLevelName);

				UE_LOG(LogSkinnedMeshComp, Warning, TEXT("SkeletalMesh %s, is not supported for current feature level (%s) and will not be rendered. MinLOD %d, NumBones %d (supported %d), NumBoneInfluences: %d"), 
					*GetNameSafe(SkeletalMesh), *FeatureLevelName, MinLODIndex, MaxBonesPerChunk, MaxSupportedGPUSkinBones, NumBoneInfluences);
			}

			//Allow the editor a chance to manipulate it before its added to the scene
			PostInitMeshObject(MeshObject);
		}
	}

	Super::CreateRenderState_Concurrent(Context);

	if (SkeletalMesh)
	{
		// Update dynamic data

		if(MeshObject)
		{
			// Clamp LOD within the VALID range
			// This is just to re-verify if LOD is WITHIN the valid range
			// Do not replace this with UpdateLODStatus, which could change the LOD 
			//	without animated, causing random skinning issues
			// This can happen if your MinLOD is not valid anymore after loading
			// which causes meshes to be invisible
			int32 ModifiedLODLevel = PredictedLODLevel;
			{
				int32 MinLodIndex = ComputeMinLOD();
				int32 MaxLODIndex = MeshObject->GetSkeletalMeshRenderData().LODRenderData.Num() - 1;
				ModifiedLODLevel = FMath::Clamp(ModifiedLODLevel, MinLodIndex, MaxLODIndex);
			}

			// Clamp to loaded streaming data if available
			if (SkeletalMesh->IsStreamable() && MeshObject)
			{
				ModifiedLODLevel = FMath::Max<int32>(ModifiedLODLevel, MeshObject->GetSkeletalMeshRenderData().PendingFirstLODIdx);
			}

			// If we have a valid LOD, set up required data, during reimport we may try to create data before we have all the LODs
			// imported, in that case we skip until we have all the LODs
			if(SkeletalMesh->IsValidLODIndex(ModifiedLODLevel))
			{
				const bool bMorphTargetsAllowed = CVarEnableMorphTargets.GetValueOnAnyThread(true) != 0;

				// Are morph targets disabled for this LOD?
				if (bDisableMorphTarget || !bMorphTargetsAllowed)
				{
					ActiveMorphTargets.Empty();
				}

				MeshObject->Update(ModifiedLODLevel, this, ActiveMorphTargets, MorphTargetWeights, EPreviousBoneTransformUpdateMode::UpdatePrevious);  // send to rendering thread
			}
		}

		// scene proxy update of material usage based on active morphs
		UpdateMorphMaterialUsageOnProxy();
	}
}

void USkinnedMeshComponent::DestroyRenderState_Concurrent()
{
	Super::DestroyRenderState_Concurrent();

	if(MeshObject)
	{
		// Begin releasing the RHI resources used by this skeletal mesh component.
		// This doesn't immediately destroy anything, since the rendering thread may still be using the resources.
		MeshObject->ReleaseResources();

		// Begin a deferred delete of MeshObject.  BeginCleanup will call MeshObject->FinishDestroy after the above release resource
		// commands execute in the rendering thread.
		BeginCleanup(MeshObject);
		MeshObject = NULL;
	}
}

bool USkinnedMeshComponent::RequiresGameThreadEndOfFrameRecreate() const
{
	// When we are a master/slave, we cannot recreate render state in parallel as this could 
	// happen concurrently with our dependent component(s)
	return MasterPoseComponent.Get() != nullptr || SlavePoseComponents.Num() > 0;
}

FString USkinnedMeshComponent::GetDetailedInfoInternal() const
{
	FString Result;  

	if( SkeletalMesh != NULL )
	{
		Result = SkeletalMesh->GetDetailedInfoInternal();
	}
	else
	{
		Result = TEXT("No_SkeletalMesh");
	}

	return Result;  
}

void USkinnedMeshComponent::SendRenderDynamicData_Concurrent()
{
	SCOPE_CYCLE_COUNTER(STAT_SkelCompUpdateTransform);

	Super::SendRenderDynamicData_Concurrent();

	// if we have not updated the transforms then no need to send them to the rendering thread
	// @todo GIsEditor used to be bUpdateSkelWhenNotRendered. Look into it further to find out why it doesn't update animations in the AnimSetViewer, when a level is loaded in UED (like POC_Cover.gear).
	if( MeshObject && SkeletalMesh && (bForceMeshObjectUpdate || (bRecentlyRendered || VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones || GIsEditor || MeshObject->bHasBeenUpdatedAtLeastOnce == false)) )
	{
		SCOPE_CYCLE_COUNTER(STAT_MeshObjectUpdate);

		const int32 UseLOD = PredictedLODLevel;

		const bool bMorphTargetsAllowed = CVarEnableMorphTargets.GetValueOnAnyThread(true) != 0;

		// Are morph targets disabled for this LOD?
		if (bDisableMorphTarget || !bMorphTargetsAllowed)
		{
			ActiveMorphTargets.Empty();
		}

		check (UseLOD < MeshObject->GetSkeletalMeshRenderData().LODRenderData.Num());
		MeshObject->Update(UseLOD,this,ActiveMorphTargets, MorphTargetWeights, bExternalEvaluationRateLimited && !bExternalInterpolate ? EPreviousBoneTransformUpdateMode::DuplicateCurrentToPrevious : EPreviousBoneTransformUpdateMode::None);  // send to rendering thread
		MeshObject->bHasBeenUpdatedAtLeastOnce = true;
		bForceMeshObjectUpdate = false; 
		
		// scene proxy update of material usage based on active morphs
		UpdateMorphMaterialUsageOnProxy();
	}
}

void USkinnedMeshComponent::ClearMotionVector()
{
	const int32 UseLOD = PredictedLODLevel;

	if (MeshObject)
	{
		// rendering bone velocity is updated by revision number
		// if you have situation where you want to clear the bone velocity (that causes temporal AA or motion blur)
		// use this function to clear it
		// this function updates renderer twice using increasing of revision number, so that renderer updates previous/new transform correctly
		++CurrentBoneTransformRevisionNumber;
		MeshObject->Update(UseLOD, this, ActiveMorphTargets, MorphTargetWeights, EPreviousBoneTransformUpdateMode::None);  // send to rendering thread

		++CurrentBoneTransformRevisionNumber;
		MeshObject->Update(UseLOD, this, ActiveMorphTargets, MorphTargetWeights, EPreviousBoneTransformUpdateMode::None);  // send to rendering thread
	}
}

void USkinnedMeshComponent::ForceMotionVector()
{
	if (MeshObject)
	{
		++CurrentBoneTransformRevisionNumber;
		MeshObject->Update(PredictedLODLevel, this, ActiveMorphTargets, MorphTargetWeights, EPreviousBoneTransformUpdateMode::None);
	}
}

#if WITH_EDITOR

bool USkinnedMeshComponent::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty)
	{
		FString PropertyName = InProperty->GetName();

		if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(USkinnedMeshComponent, bCastCapsuleIndirectShadow))
		{
			return CastShadow && bCastDynamicShadow;
		}

		if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(USkinnedMeshComponent, CapsuleIndirectShadowMinVisibility))
		{
			return bCastCapsuleIndirectShadow && CastShadow && bCastDynamicShadow;
		}
	}

	return Super::CanEditChange(InProperty);
}

#endif // WITH_EDITOR

void USkinnedMeshComponent::InitLODInfos()
{
	if (SkeletalMesh != NULL)
	{
		if (SkeletalMesh->GetLODNum() != LODInfo.Num())
		{
			LODInfo.Empty(SkeletalMesh->GetLODNum());
			for (int32 Idx=0; Idx < SkeletalMesh->GetLODNum(); Idx++)
			{
				new(LODInfo) FSkelMeshComponentLODInfo();
			}
		}
	}	
}


bool USkinnedMeshComponent::ShouldTickPose() const
{
	return ((VisibilityBasedAnimTickOption < EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered) || bRecentlyRendered);
}

bool USkinnedMeshComponent::ShouldUpdateTransform(bool bLODHasChanged) const
{
	return (bRecentlyRendered || (VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones));
}

bool USkinnedMeshComponent::ShouldUseUpdateRateOptimizations() const
{
	return bEnableUpdateRateOptimizations && CVarEnableAnimRateOptimization.GetValueOnAnyThread() > 0;
}

void USkinnedMeshComponent::TickUpdateRate(float DeltaTime, bool bNeedsValidRootMotion)
{
	SCOPE_CYCLE_COUNTER(STAT_TickUpdateRate);
	if (ShouldUseUpdateRateOptimizations())
	{
		if (GetOwner())
		{
			// Tick Owner once per frame. All attached SkinnedMeshComponents will share the same settings.
			FAnimUpdateRateManager::TickUpdateRateParameters(this, DeltaTime, bNeedsValidRootMotion);

#if ENABLE_DRAW_DEBUG
			if ((CVarDrawAnimRateOptimization.GetValueOnGameThread() > 0) || bDisplayDebugUpdateRateOptimizations)
			{
				FColor DrawColor = AnimUpdateRateParams->GetUpdateRateDebugColor();
				DrawDebugBox(GetWorld(), Bounds.Origin, Bounds.BoxExtent, FQuat::Identity, DrawColor, false);

				FString DebugString = FString::Printf(TEXT("%s UpdateRate(%d) EvaluationRate(%d) ShouldInterpolateSkippedFrames(%d) ShouldSkipUpdate(%d) Interp(%d) AdditionalTime(%f)"),
					*GetNameSafe(SkeletalMesh), AnimUpdateRateParams->UpdateRate, AnimUpdateRateParams->EvaluationRate, 
					AnimUpdateRateParams->ShouldInterpolateSkippedFrames(), AnimUpdateRateParams->ShouldSkipUpdate(), AnimUpdateRateParams->AdditionalTime);

				GEngine->AddOnScreenDebugMessage(INDEX_NONE, 0.f, FColor::Red, DebugString, false);
			}
#endif // ENABLE_DRAW_DEBUG
		}
	}
}

void USkinnedMeshComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	SCOPED_NAMED_EVENT(USkinnedMeshComponent_TickComponent, FColor::Yellow);
	SCOPE_CYCLE_COUNTER(STAT_SkinnedMeshCompTick);

	// Tick ActorComponent first.
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// See if this mesh was rendered recently. This has to happen first because other data will rely on this
	bRecentlyRendered = (GetLastRenderTime() > GetWorld()->TimeSeconds - 1.0f);

	// Update component's LOD settings
	// This must be done BEFORE animation Update and Evaluate (TickPose and RefreshBoneTransforms respectively)
	const bool bLODHasChanged = UpdateLODStatus();

	// Tick Pose first
	if (ShouldTickPose())
	{
		TickPose(DeltaTime, false);
	}

	// If we have been recently rendered, and bForceRefPose has been on for at least a frame, or the LOD changed, update bone matrices.
	if( ShouldUpdateTransform(bLODHasChanged) )
	{
		// Do not update bones if we are taking bone transforms from another SkelMeshComp
		if( MasterPoseComponent.IsValid() )
		{
			UpdateSlaveComponent();
		}
		else 
		{
			RefreshBoneTransforms(ThisTickFunction);
		}
	}
	else if(VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::AlwaysTickPose)
	{
		// We are not refreshing bone transforms, but we do want to tick pose. We may need to kick off a parallel task
		DispatchParallelTickPose(ThisTickFunction);
	}
#if WITH_EDITOR
	else 
	{
		// only do this for level viewport actors
		UWorld* World = GetWorld();
		if (World && World->WorldType == EWorldType::Editor)
		{
			RefreshMorphTargets();
		}
	}
#endif // WITH_EDITOR
}

UObject const* USkinnedMeshComponent::AdditionalStatObject() const
{
	return SkeletalMesh;
}


void USkinnedMeshComponent::UpdateSlaveComponent()
{
	MarkRenderDynamicDataDirty();
}

// this has to be skeletalmesh material. You can't have more than what SkeletalMesh materials have
int32 USkinnedMeshComponent::GetNumMaterials() const
{
	if (SkeletalMesh)
	{
		return SkeletalMesh->Materials.Num();
	}

	return 0;
}

UMaterialInterface* USkinnedMeshComponent::GetMaterial(int32 MaterialIndex) const
{
	if(OverrideMaterials.IsValidIndex(MaterialIndex) && OverrideMaterials[MaterialIndex])
	{
		return OverrideMaterials[MaterialIndex];
	}
	else if (SkeletalMesh && SkeletalMesh->Materials.IsValidIndex(MaterialIndex) && SkeletalMesh->Materials[MaterialIndex].MaterialInterface)
	{
		return SkeletalMesh->Materials[MaterialIndex].MaterialInterface;
	}

	return nullptr;
}

int32 USkinnedMeshComponent::GetMaterialIndex(FName MaterialSlotName) const
{
	if (SkeletalMesh != nullptr)
	{
		for (int32 MaterialIndex = 0; MaterialIndex < SkeletalMesh->Materials.Num(); ++MaterialIndex)
		{
			const FSkeletalMaterial &SkeletalMaterial = SkeletalMesh->Materials[MaterialIndex];
			if (SkeletalMaterial.MaterialSlotName == MaterialSlotName)
			{
				return MaterialIndex;
			}
		}
	}
	return INDEX_NONE;
}

TArray<FName> USkinnedMeshComponent::GetMaterialSlotNames() const
{
	TArray<FName> MaterialNames;
	if (SkeletalMesh != nullptr)
	{
		for (int32 MaterialIndex = 0; MaterialIndex < SkeletalMesh->Materials.Num(); ++MaterialIndex)
		{
			const FSkeletalMaterial &SkeletalMaterial = SkeletalMesh->Materials[MaterialIndex];
			MaterialNames.Add(SkeletalMaterial.MaterialSlotName);
		}
	}
	return MaterialNames;
}

bool USkinnedMeshComponent::IsMaterialSlotNameValid(FName MaterialSlotName) const
{
	return GetMaterialIndex(MaterialSlotName) >= 0;
}

bool USkinnedMeshComponent::ShouldCPUSkin()
{
	return GetCPUSkinningEnabled();
}

bool USkinnedMeshComponent::GetCPUSkinningEnabled() const
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return bCPUSkinning;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void USkinnedMeshComponent::SetCPUSkinningEnabled(bool bEnable, bool bRecreateRenderStateImmediately)
{
	checkSlow(IsInGameThread());
	check(SkeletalMesh && SkeletalMesh->GetResourceForRendering());

	if (GetCPUSkinningEnabled() == bEnable)
	{
		return;
	}

	if (bEnable && IStreamingManager::Get().IsRenderAssetStreamingEnabled(EStreamableRenderAssetType::SkeletalMesh))
	{
		UE_LOG(LogSkinnedMeshComp, Warning, TEXT("It is expensive to enable CPU skinning with LOD streaming on."));

		IRenderAssetStreamingManager& Manager = IStreamingManager::Get().GetRenderAssetStreamingManager();
		Manager.BlockTillAllRequestsFinished();

		const bool bOriginalForcedFullyLoad = SkeletalMesh->bForceMiplevelsToBeResident;
		SkeletalMesh->bForceMiplevelsToBeResident = true;
		Manager.UpdateIndividualRenderAsset(SkeletalMesh);

		SkeletalMesh->WaitForPendingInitOrStreaming();

		check(SkeletalMesh->GetResourceForRendering()->CurrentFirstLODIdx <= SkeletalMesh->MinLod.Default);

		SkeletalMesh->UnlinkStreaming();
		SkeletalMesh->bForceMiplevelsToBeResident = bOriginalForcedFullyLoad;
	}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	bCPUSkinning = bEnable;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	
	if (IsRegistered())
	{
		if (bRecreateRenderStateImmediately)
		{
			RecreateRenderState_Concurrent();
			FlushRenderingCommands();
		}
		else
		{
			MarkRenderStateDirty();
		}
	}
}

bool USkinnedMeshComponent::GetMaterialStreamingData(int32 MaterialIndex, FPrimitiveMaterialInfo& MaterialData) const
{
	if (SkeletalMesh)
	{
		MaterialData.Material = GetMaterial(MaterialIndex);
		MaterialData.UVChannelData = SkeletalMesh->GetUVChannelData(MaterialIndex);
		MaterialData.PackedRelativeBox = PackedRelativeBox_Identity;
	}
	return MaterialData.IsValid();
}

void USkinnedMeshComponent::GetStreamingRenderAssetInfo(FStreamingTextureLevelContext& LevelContext, TArray<FStreamingRenderAssetPrimitiveInfo>& OutStreamingRenderAssets) const
{
	GetStreamingTextureInfoInner(LevelContext, nullptr, GetComponentTransform().GetMaximumAxisScale() * StreamingDistanceMultiplier, OutStreamingRenderAssets);

	if (SkeletalMesh && SkeletalMesh->IsStreamable())
	{
		const int32 LocalForcedLodModel = GetForcedLOD();
		const float TexelFactor = LocalForcedLodModel > 0 ? -(SkeletalMesh->GetLODNum() - LocalForcedLodModel + 1) : Bounds.SphereRadius * 2.f;
		new (OutStreamingRenderAssets) FStreamingRenderAssetPrimitiveInfo(SkeletalMesh, Bounds, TexelFactor, PackedRelativeBox_Identity);
	}
}

bool USkinnedMeshComponent::ShouldUpdateBoneVisibility() const
{
	// do not update if it has MasterPoseComponent
	return !MasterPoseComponent.IsValid();
}
void USkinnedMeshComponent::RebuildVisibilityArray()
{
	// BoneVisibility needs update if MasterComponent == NULL
	// if MaterComponent, it should follow MaterPoseComponent
	if ( ShouldUpdateBoneVisibility())
	{
		// If the BoneVisibilityStates array has a 0 for a parent bone, all children bones are meant to be hidden as well
		// (as the concatenated matrix will have scale 0).  This code propagates explicitly hidden parents to children.

		// On the first read of any cell of BoneVisibilityStates, BVS_HiddenByParent and BVS_Visible are treated as visible.
		// If it starts out visible, the value written back will be BVS_Visible if the parent is visible; otherwise BVS_HiddenByParent.
		// If it starts out hidden, the BVS_ExplicitlyHidden value stays in place

		// The following code relies on a complete hierarchy sorted from parent to children
		TArray<uint8>& EditableBoneVisibilityStates = GetEditableBoneVisibilityStates();
		if (EditableBoneVisibilityStates.Num() != SkeletalMesh->RefSkeleton.GetNum())
		{
			UE_LOG(LogSkinnedMeshComp, Warning, TEXT("RebuildVisibilityArray() failed because EditableBoneVisibilityStates size: %d not equal to RefSkeleton bone count: %d."), EditableBoneVisibilityStates.Num(), SkeletalMesh->RefSkeleton.GetNum());
			return;
		}

		for (int32 BoneId=0; BoneId < EditableBoneVisibilityStates.Num(); ++BoneId)
		{
			uint8 VisState = EditableBoneVisibilityStates[BoneId];

			// if not exclusively hidden, consider if parent is hidden
			if (VisState != BVS_ExplicitlyHidden)
			{
				// Check direct parent (only need to do one deep, since we have already processed the parent and written to BoneVisibilityStates previously)
				const int32 ParentIndex = SkeletalMesh->RefSkeleton.GetParentIndex(BoneId);
				if ((ParentIndex == -1) || (EditableBoneVisibilityStates[ParentIndex] == BVS_Visible))
				{
					EditableBoneVisibilityStates[BoneId] = BVS_Visible;
				}
				else
				{
					EditableBoneVisibilityStates[BoneId] = BVS_HiddenByParent;
				}
			}
		}

		bBoneVisibilityDirty = true;
	}
}

FBoxSphereBounds USkinnedMeshComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	SCOPE_CYCLE_COUNTER(STAT_CalcSkelMeshBounds);

	return CalcMeshBound( FVector::ZeroVector, false, LocalToWorld );
}


class UPhysicsAsset* USkinnedMeshComponent::GetPhysicsAsset() const
{
	if (PhysicsAssetOverride)
	{
		return PhysicsAssetOverride;
	}

	if (SkeletalMesh && SkeletalMesh->PhysicsAsset)
	{
		return SkeletalMesh->PhysicsAsset;
	}

	return NULL;
}

FBoxSphereBounds USkinnedMeshComponent::CalcMeshBound(const FVector& RootOffset, bool UsePhysicsAsset, const FTransform& LocalToWorld) const
{
	FBoxSphereBounds NewBounds;

	// If physics are asleep, and actor is using physics to move, skip updating the bounds.
	AActor* Owner = GetOwner();
	FVector DrawScale = LocalToWorld.GetScale3D();	

	const USkinnedMeshComponent* const MasterPoseComponentInst = MasterPoseComponent.Get();
	UPhysicsAsset * const PhysicsAsset = GetPhysicsAsset();
	UPhysicsAsset * const MasterPhysicsAsset = (MasterPoseComponentInst != nullptr)? MasterPoseComponentInst->GetPhysicsAsset() : nullptr;

	// Can only use the PhysicsAsset to calculate the bounding box if we are not non-uniformly scaling the mesh.
	const bool bCanUsePhysicsAsset = DrawScale.IsUniform() && (SkeletalMesh != NULL)
		// either space base exists or child component
		&& ( (GetNumComponentSpaceTransforms() == SkeletalMesh->RefSkeleton.GetNum()) || (MasterPhysicsAsset) );

	const bool bDetailModeAllowsRendering = (DetailMode <= GetCachedScalabilityCVars().DetailMode);
	const bool bIsVisible = ( bDetailModeAllowsRendering && (ShouldRender() || bCastHiddenShadow));

	const bool bHasPhysBodies = PhysicsAsset && PhysicsAsset->SkeletalBodySetups.Num();
	const bool bMasterHasPhysBodies = MasterPhysicsAsset && MasterPhysicsAsset->SkeletalBodySetups.Num();

	// if not visible, or we were told to use fixed bounds, use skelmesh bounds
	if ( (!bIsVisible || bComponentUseFixedSkelBounds) && SkeletalMesh ) 
	{
		FBoxSphereBounds RootAdjustedBounds = SkeletalMesh->GetBounds();
		RootAdjustedBounds.Origin += RootOffset; // Adjust bounds by root bone translation
		NewBounds = RootAdjustedBounds.TransformBy(LocalToWorld);
	}
	else if(MasterPoseComponentInst && MasterPoseComponentInst->SkeletalMesh && MasterPoseComponentInst->bComponentUseFixedSkelBounds)
	{
		FBoxSphereBounds RootAdjustedBounds = MasterPoseComponentInst->SkeletalMesh->GetBounds();
		RootAdjustedBounds.Origin += RootOffset; // Adjust bounds by root bone translation
		NewBounds = RootAdjustedBounds.TransformBy(LocalToWorld);
	}
	// Use MasterPoseComponent's PhysicsAsset if told to
	else if (MasterPoseComponentInst && bCanUsePhysicsAsset && bUseBoundsFromMasterPoseComponent)
	{
		NewBounds = MasterPoseComponentInst->Bounds;
	}
#if WITH_EDITOR
	// For AnimSet Viewer, use 'bounds preview' physics asset if present.
	else if(SkeletalMesh && bHasPhysBodies && bCanUsePhysicsAsset && PhysicsAsset->CanCalculateValidAABB(this, LocalToWorld))
	{
		NewBounds = FBoxSphereBounds(PhysicsAsset->CalcAABB(this, LocalToWorld));
	}
#endif // WITH_EDITOR
	// If we have a PhysicsAsset (with at least one matching bone), and we can use it, do so to calc bounds.
	else if( bHasPhysBodies && bCanUsePhysicsAsset && UsePhysicsAsset )
	{
		NewBounds = FBoxSphereBounds(PhysicsAsset->CalcAABB(this, LocalToWorld));
	}
	// Use MasterPoseComponent's PhysicsAsset, if we don't have one and it does
	else if(MasterPoseComponentInst && bCanUsePhysicsAsset && bMasterHasPhysBodies)
	{
		NewBounds = FBoxSphereBounds(MasterPhysicsAsset->CalcAABB(this, LocalToWorld));
	}
	// Fallback is to use the one from the skeletal mesh. Usually pretty bad in terms of Accuracy of where the SkelMesh Bounds are located (i.e. usually bigger than it needs to be)
	else if( SkeletalMesh )
	{
		FBoxSphereBounds RootAdjustedBounds = SkeletalMesh->GetBounds();

		// Adjust bounds by root bone translation
		RootAdjustedBounds.Origin += RootOffset;
		NewBounds = RootAdjustedBounds.TransformBy(LocalToWorld);
	}
	else
	{
		NewBounds = FBoxSphereBounds(LocalToWorld.GetLocation(), FVector::ZeroVector, 0.f);
	}

	// Add bounds of any per-poly collision data.
	// TODO UE4

	NewBounds.BoxExtent *= BoundsScale;
	NewBounds.SphereRadius *= BoundsScale;

	return NewBounds;
}

void USkinnedMeshComponent::GetPreSkinnedLocalBounds(FBoxSphereBounds& OutBounds) const
{
	const USkinnedMeshComponent* const MasterPoseComponentInst = MasterPoseComponent.Get();

	if (SkeletalMesh)
	{
		// Get the Pre-skinned bounds from the skeletal mesh. Note that these bounds are the "ExtendedBounds", so they can be tweaked on the SkeletalMesh   
		OutBounds = SkeletalMesh->GetBounds();
	}
	else if(MasterPoseComponentInst && MasterPoseComponentInst->SkeletalMesh)
	{
		// Get the bounds from the master pose if ther is no skeletal mesh
		OutBounds = MasterPoseComponentInst->SkeletalMesh->GetBounds();
	}
	else
	{
		// Fall back
		OutBounds = FBoxSphereBounds(ForceInitToZero);
	}
}

FMatrix USkinnedMeshComponent::GetBoneMatrix(int32 BoneIdx) const
{
	if ( !IsRegistered() )
	{
		// if not registered, we don't have SpaceBases yet. 
		// also GetComponentTransform() isn't set yet (They're set from relativetranslation, relativerotation, relativescale)
		return FMatrix::Identity;
	}

	// Handle case of use a MasterPoseComponent - get bone matrix from there.
	const USkinnedMeshComponent* const MasterPoseComponentInst = MasterPoseComponent.Get();
	if(MasterPoseComponentInst)
	{
		if(BoneIdx < MasterBoneMap.Num())
		{
			int32 ParentBoneIndex = MasterBoneMap[BoneIdx];

			// If ParentBoneIndex is valid, grab matrix from MasterPoseComponent.
			if(	ParentBoneIndex != INDEX_NONE && 
				ParentBoneIndex < MasterPoseComponentInst->GetNumComponentSpaceTransforms())
			{
				return MasterPoseComponentInst->GetComponentSpaceTransforms()[ParentBoneIndex].ToMatrixWithScale() * GetComponentTransform().ToMatrixWithScale();
			}
			else
			{
				UE_LOG(LogSkinnedMeshComp, Verbose, TEXT("GetBoneMatrix : ParentBoneIndex(%d:%s) out of range of MasterPoseComponent->SpaceBases for %s(%s)"), BoneIdx, *GetNameSafe(MasterPoseComponentInst->SkeletalMesh), *GetNameSafe(SkeletalMesh), *GetPathName());
				return FMatrix::Identity;
			}
		}
		else
		{
			UE_LOG(LogSkinnedMeshComp, Warning, TEXT("GetBoneMatrix : BoneIndex(%d) out of range of MasterBoneMap for %s (%s)"), BoneIdx, *this->GetFName().ToString(), SkeletalMesh ? *SkeletalMesh->GetFName().ToString() : TEXT("NULL") );
			return FMatrix::Identity;
		}
	}
	else
	{
		const int32 NumTransforms = GetNumComponentSpaceTransforms();
		if(BoneIdx >= 0 && BoneIdx < NumTransforms)
		{
			return GetComponentSpaceTransforms()[BoneIdx].ToMatrixWithScale() * GetComponentTransform().ToMatrixWithScale();
		}
		else
		{
			UE_LOG(LogSkinnedMeshComp, Warning, TEXT("GetBoneMatrix : BoneIndex(%d) out of range of SpaceBases for %s (%s)"), BoneIdx, *GetPathName(), SkeletalMesh ? *SkeletalMesh->GetFullName() : TEXT("NULL") );
			return FMatrix::Identity;
		}
	}
}

FTransform USkinnedMeshComponent::GetBoneTransform(int32 BoneIdx) const
{
	if (!IsRegistered())
	{
		// if not registered, we don't have SpaceBases yet. 
		// also GetComponentTransform() isn't set yet (They're set from relativelocation, relativerotation, relativescale)
		return FTransform::Identity;
	}

	return GetBoneTransform(BoneIdx, GetComponentTransform());
}

FTransform USkinnedMeshComponent::GetBoneTransform(int32 BoneIdx, const FTransform& LocalToWorld) const
{
	// Handle case of use a MasterPoseComponent - get bone matrix from there.
	const USkinnedMeshComponent* const MasterPoseComponentInst = MasterPoseComponent.Get();
	if(MasterPoseComponentInst)
	{
		if (!MasterPoseComponentInst->IsRegistered())
		{
			// We aren't going to get anything valid from the master pose if it
			// isn't valid so for now return identity
			return FTransform::Identity;
		}
		if(BoneIdx < MasterBoneMap.Num())
		{
			int32 MasterBoneIndex = MasterBoneMap[BoneIdx];

			// If ParentBoneIndex is valid, grab matrix from MasterPoseComponent.
			if(	MasterBoneIndex != INDEX_NONE && 
				MasterBoneIndex < MasterPoseComponentInst->GetNumComponentSpaceTransforms())
			{
				return MasterPoseComponentInst->GetComponentSpaceTransforms()[MasterBoneIndex] * LocalToWorld;
			}
			else
			{
				// Is this a missing bone we have cached?
				FMissingMasterBoneCacheEntry MissingBoneInfo;
				const FMissingMasterBoneCacheEntry* MissingBoneInfoPtr = MissingMasterBoneMap.Find(BoneIdx);
				if(MissingBoneInfoPtr != nullptr)
				{
					return MissingBoneInfoPtr->RelativeTransform * MasterPoseComponentInst->GetComponentSpaceTransforms()[MissingBoneInfoPtr->CommonAncestorBoneIndex] * LocalToWorld;
				}
				// Otherwise we might be able to generate the missing transform on the fly (although this is expensive)
				else if(GetMissingMasterBoneRelativeTransform(BoneIdx, MissingBoneInfo))
				{
					return MissingBoneInfo.RelativeTransform * MasterPoseComponentInst->GetComponentSpaceTransforms()[MissingBoneInfo.CommonAncestorBoneIndex] * LocalToWorld;
				}

				UE_LOG(LogSkinnedMeshComp, Verbose, TEXT("GetBoneTransform : ParentBoneIndex(%d) out of range of MasterPoseComponent->SpaceBases for %s"), BoneIdx, *this->GetFName().ToString() );
				return FTransform::Identity;
			}
		}
		else
		{
			UE_LOG(LogSkinnedMeshComp, Warning, TEXT("GetBoneTransform : BoneIndex(%d) out of range of MasterBoneMap for %s"), BoneIdx, *this->GetFName().ToString() );
			return FTransform::Identity;
		}
	}
	else
	{
		const int32 NumTransforms = GetNumComponentSpaceTransforms();
		if(BoneIdx >= 0 && BoneIdx < NumTransforms)
		{
			return GetComponentSpaceTransforms()[BoneIdx] * LocalToWorld;
		}
		else
		{
			UE_LOG(LogSkinnedMeshComp, Warning, TEXT("GetBoneTransform : BoneIndex(%d) out of range of SpaceBases for %s (%s)"), BoneIdx, *GetPathName(), SkeletalMesh ? *SkeletalMesh->GetFullName() : TEXT("NULL") );
			return FTransform::Identity;
		}
	}
}

int32 USkinnedMeshComponent::GetNumBones()const
{
	return SkeletalMesh ? SkeletalMesh->RefSkeleton.GetNum() : 0;
}

int32 USkinnedMeshComponent::GetBoneIndex( FName BoneName) const
{
	int32 BoneIndex = INDEX_NONE;
	if ( BoneName != NAME_None && SkeletalMesh )
	{
		BoneIndex = SkeletalMesh->RefSkeleton.FindBoneIndex( BoneName );
	}

	return BoneIndex;
}


FName USkinnedMeshComponent::GetBoneName(int32 BoneIndex) const
{
	return (SkeletalMesh != NULL && SkeletalMesh->RefSkeleton.IsValidIndex(BoneIndex)) ? SkeletalMesh->RefSkeleton.GetBoneName(BoneIndex) : NAME_None;
}


FName USkinnedMeshComponent::GetParentBone( FName BoneName ) const
{
	FName Result = NAME_None;

	int32 BoneIndex = GetBoneIndex(BoneName);
	if ((BoneIndex != INDEX_NONE) && (BoneIndex > 0)) // This checks that this bone is not the root (ie no parent), and that BoneIndex != INDEX_NONE (ie bone name was found)
	{
		Result = SkeletalMesh->RefSkeleton.GetBoneName(SkeletalMesh->RefSkeleton.GetParentIndex(BoneIndex));
	}
	return Result;
}

FTransform USkinnedMeshComponent::GetDeltaTransformFromRefPose(FName BoneName, FName BaseName/* = NAME_None*/) const
{
	if (SkeletalMesh)
	{
		const FReferenceSkeleton& RefSkeleton = SkeletalMesh->RefSkeleton;
		const int32 BoneIndex = GetBoneIndex(BoneName);
		if (BoneIndex != INDEX_NONE)
		{
			FTransform CurrentTransform = GetBoneTransform(BoneIndex);
			FTransform ReferenceTransform = FAnimationRuntime::GetComponentSpaceTransformRefPose(RefSkeleton, BoneIndex);
			if (BaseName == NAME_None)
			{
				BaseName = GetParentBone(BoneName);
			}

			const int32 BaseIndex = GetBoneIndex(BaseName);
			if (BaseIndex != INDEX_NONE)
			{	
				CurrentTransform = CurrentTransform.GetRelativeTransform(GetBoneTransform(BaseIndex));
				ReferenceTransform = ReferenceTransform.GetRelativeTransform(FAnimationRuntime::GetComponentSpaceTransformRefPose(RefSkeleton, BaseIndex));
			}

			// get delta of two transform
			return CurrentTransform.GetRelativeTransform(ReferenceTransform);
		}
	}

	return FTransform::Identity;
}

bool USkinnedMeshComponent::GetTwistAndSwingAngleOfDeltaRotationFromRefPose(FName BoneName, float& OutTwistAngle, float& OutSwingAngle) const
{
	const FReferenceSkeleton& RefSkeleton = SkeletalMesh->RefSkeleton;
	const int32 BoneIndex = GetBoneIndex(BoneName);
	const TArray<FTransform>& Transforms = GetComponentSpaceTransforms();

	// detect the case where we don't have a pose yet
	if (Transforms.Num() == 0)
	{
		OutTwistAngle = OutSwingAngle = 0.f;
		return false;
	}

	if (BoneIndex != INDEX_NONE && ensureMsgf(BoneIndex < Transforms.Num(), TEXT("Invalid transform access in %s. Index=%d, Num=%d"), *GetPathName(), BoneIndex, Transforms.Num()))
	{
		FTransform LocalTransform = GetComponentSpaceTransforms()[BoneIndex];
		FTransform ReferenceTransform = RefSkeleton.GetRefBonePose()[BoneIndex];
		FName ParentName = GetParentBone(BoneName);
		int32 ParentIndex = INDEX_NONE;
		if (ParentName != NAME_None)
		{
			ParentIndex = GetBoneIndex(ParentName);
		}

		if (ParentIndex != INDEX_NONE)
		{
			LocalTransform = LocalTransform.GetRelativeTransform(GetComponentSpaceTransforms()[ParentIndex]);
		}

		FQuat Swing, Twist;

		// figure out based on ref pose rotation, and calculate twist based on that 
		FVector TwistAxis = ReferenceTransform.GetRotation().Vector();
		ensure(TwistAxis.IsNormalized());
		LocalTransform.GetRotation().ToSwingTwist(TwistAxis, Swing, Twist);
		OutTwistAngle = FMath::RadiansToDegrees(Twist.GetAngle());
		OutSwingAngle = FMath::RadiansToDegrees(Swing.GetAngle());
		return true;
	}

	return false;
}

bool USkinnedMeshComponent::IsSkinCacheAllowed(int32 LodIdx) const
{
	static const IConsoleVariable* CVarDefaultGPUSkinCacheBehavior = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SkinCache.DefaultBehavior"));

	bool bIsRayTracing = IsRayTracingEnabled();

	bool bGlobalDefault = CVarDefaultGPUSkinCacheBehavior && ESkinCacheDefaultBehavior(CVarDefaultGPUSkinCacheBehavior->GetInt()) == ESkinCacheDefaultBehavior::Inclusive;

	if (!SkeletalMesh)
	{
		return GEnableGPUSkinCache && (bIsRayTracing || bGlobalDefault);
	}

	FSkeletalMeshLODInfo* LodInfo = SkeletalMesh->GetLODInfo(LodIdx);
	if (!LodInfo)
	{
		return GEnableGPUSkinCache && (bIsRayTracing || bGlobalDefault);
	}

	bool bLodEnabled = LodInfo->SkinCacheUsage == ESkinCacheUsage::Auto ?
		bGlobalDefault :
		LodInfo->SkinCacheUsage == ESkinCacheUsage::Enabled;

	if (!SkinCacheUsage.IsValidIndex(LodIdx))
	{
		return GEnableGPUSkinCache && (bIsRayTracing || bLodEnabled);
	}

	bool bComponentEnabled = SkinCacheUsage[LodIdx] == ESkinCacheUsage::Auto ? 
		bLodEnabled :
		SkinCacheUsage[LodIdx] == ESkinCacheUsage::Enabled;

	return GEnableGPUSkinCache && (bIsRayTracing || bComponentEnabled);
}

void USkinnedMeshComponent::GetBoneNames(TArray<FName>& BoneNames)
{
	if (SkeletalMesh == NULL)
	{
		// no mesh, so no bones
		BoneNames.Empty();
	}
	else
	{
		// pre-size the array to avoid unnecessary reallocation
		BoneNames.Empty(SkeletalMesh->RefSkeleton.GetNum());
		BoneNames.AddUninitialized(SkeletalMesh->RefSkeleton.GetNum());
		for (int32 i = 0; i < SkeletalMesh->RefSkeleton.GetNum(); i++)
		{
			BoneNames[i] = SkeletalMesh->RefSkeleton.GetBoneName(i);
		}
	}
}


bool USkinnedMeshComponent::BoneIsChildOf(FName BoneName, FName ParentBoneName) const
{
	bool bResult = false;

	if( SkeletalMesh )
	{
		const int32 BoneIndex = SkeletalMesh->RefSkeleton.FindBoneIndex(BoneName);
		if(BoneIndex == INDEX_NONE)
		{
			UE_LOG(LogSkinnedMeshComp, Log, TEXT("execBoneIsChildOf: BoneName '%s' not found in SkeletalMesh '%s'"), *BoneName.ToString(), *SkeletalMesh->GetName());
			return bResult;
		}

		const int32 ParentBoneIndex = SkeletalMesh->RefSkeleton.FindBoneIndex(ParentBoneName);
		if(ParentBoneIndex == INDEX_NONE)
		{
			UE_LOG(LogSkinnedMeshComp, Log, TEXT("execBoneIsChildOf: ParentBoneName '%s' not found in SkeletalMesh '%s'"), *ParentBoneName.ToString(), *SkeletalMesh->GetName());
			return bResult;
		}

		bResult = SkeletalMesh->RefSkeleton.BoneIsChildOf(BoneIndex, ParentBoneIndex);
	}

	return bResult;
}


FVector USkinnedMeshComponent::GetRefPosePosition(int32 BoneIndex)
{
	if(SkeletalMesh && (BoneIndex >= 0) && (BoneIndex < SkeletalMesh->RefSkeleton.GetNum()))
	{
		return SkeletalMesh->RefSkeleton.GetRefBonePose()[BoneIndex].GetTranslation();
	}
	else
	{
		return FVector::ZeroVector;
	}
}


void USkinnedMeshComponent::SetSkeletalMesh(USkeletalMesh* InSkelMesh, bool bReinitPose)
{
	// NOTE: InSkelMesh may be NULL (useful in the editor for removing the skeletal mesh associated with
	//   this component on-the-fly)

	if (InSkelMesh == SkeletalMesh)
	{
		// do nothing if the input mesh is the same mesh we're already using.
		return;
	}

	{
		//Handle destroying and recreating the renderstate
		FRenderStateRecreator RenderStateRecreator(this);

		SkeletalMesh = InSkelMesh;

		//SlavePoseComponents is an array of weak obj ptrs, so it can contain null elements
		for (auto Iter = SlavePoseComponents.CreateIterator(); Iter; ++Iter)
		{
			TWeakObjectPtr<USkinnedMeshComponent> Comp = (*Iter);
			if (Comp.IsValid() == false)
			{
				SlavePoseComponents.RemoveAt(Iter.GetIndex());
				--Iter;
			}
			else
			{
				Comp->UpdateMasterBoneMap();
			}
		}

		// Don't init anim state if not registered
		if (IsRegistered())
		{
			AllocateTransformData();
			UpdateMasterBoneMap();
			InvalidateCachedBounds();
			// clear morphtarget cache
			ActiveMorphTargets.Empty();			
			MorphTargetWeights.Empty();
		}
	}
	
	if (IsRegistered())
	{
		// We do this after the FRenderStateRecreator has gone as
		// UpdateLODStatus needs a valid MeshObject
		UpdateLODStatus(); 
	}
}

FSkeletalMeshRenderData* USkinnedMeshComponent::GetSkeletalMeshRenderData() const
{
	if (MeshObject)
	{
		return &MeshObject->GetSkeletalMeshRenderData();
	}
	else if (SkeletalMesh)
	{
		return SkeletalMesh->GetResourceForRendering();
	}
	else
	{
		return NULL;
	}
}

bool USkinnedMeshComponent::AllocateTransformData()
{
	LLM_SCOPE(ELLMTag::SkeletalMesh);

	// Allocate transforms if not present.
	if ( SkeletalMesh != NULL && MasterPoseComponent == NULL )
	{
		if(GetNumComponentSpaceTransforms() != SkeletalMesh->RefSkeleton.GetNum() )
		{
			for (int32 BaseIndex = 0; BaseIndex < 2; ++BaseIndex)
			{
				ComponentSpaceTransformsArray[BaseIndex].Empty(SkeletalMesh->RefSkeleton.GetNum());
				ComponentSpaceTransformsArray[BaseIndex].AddUninitialized(SkeletalMesh->RefSkeleton.GetNum());

				for (int32 I = 0; I < SkeletalMesh->RefSkeleton.GetNum(); ++I)
				{
					ComponentSpaceTransformsArray[BaseIndex][I].SetIdentity();
				}

				BoneVisibilityStates[BaseIndex].Empty( SkeletalMesh->RefSkeleton.GetNum() );
				if( SkeletalMesh->RefSkeleton.GetNum() )
				{
					BoneVisibilityStates[BaseIndex].AddUninitialized( SkeletalMesh->RefSkeleton.GetNum() );
					for (int32 BoneIndex = 0; BoneIndex < SkeletalMesh->RefSkeleton.GetNum(); BoneIndex++)
					{
						BoneVisibilityStates[BaseIndex][BoneIndex] = BVS_Visible;
					}
				}
			}
 
			// when initialize bone transform first time
			// it is invalid
			bHasValidBoneTransform = false;

			// Init previous arrays only if we are not using double-buffering
			if(!bDoubleBufferedComponentSpaceTransforms)
			{
				PreviousComponentSpaceTransformsArray = ComponentSpaceTransformsArray[0];
				PreviousBoneVisibilityStates = BoneVisibilityStates[0];
			}
		}

		// if it's same, do not touch, and return
		return true;
	}
	
	// Reset the animation stuff when changing mesh.
	ComponentSpaceTransformsArray[0].Empty();
	ComponentSpaceTransformsArray[1].Empty();
	PreviousComponentSpaceTransformsArray.Empty();

	return false;
}

void USkinnedMeshComponent::DeallocateTransformData()
{
	ComponentSpaceTransformsArray[0].Empty();
	ComponentSpaceTransformsArray[1].Empty();
	PreviousComponentSpaceTransformsArray.Empty();
	BoneVisibilityStates[0].Empty();
	BoneVisibilityStates[1].Empty();
	PreviousBoneVisibilityStates.Empty();
}

void USkinnedMeshComponent::SetPhysicsAsset(class UPhysicsAsset* InPhysicsAsset, bool bForceReInit)
{
	PhysicsAssetOverride = InPhysicsAsset;
}

void USkinnedMeshComponent::SetMasterPoseComponent(class USkinnedMeshComponent* NewMasterBoneComponent, bool bForceUpdate)
{
	// Early out if we're already setup.
	if (!bForceUpdate && NewMasterBoneComponent == MasterPoseComponent)
	{
		return;
	}

	USkinnedMeshComponent* OldMasterPoseComponent = MasterPoseComponent.Get();
	USkinnedMeshComponent* ValidNewMasterPose = NewMasterBoneComponent;

	// now add to slave components list, 
	if (ValidNewMasterPose)
	{
		// verify if my current master pose is valid
		// we can't have chain of master poses, so 
		// we'll find the root master pose component
		USkinnedMeshComponent* Iterator = ValidNewMasterPose;
		while (Iterator->MasterPoseComponent.IsValid())
		{
			ValidNewMasterPose = Iterator->MasterPoseComponent.Get();
			Iterator = ValidNewMasterPose;

			// we have cycling, where in this chain, if it comes back to me, then reject it
			if (Iterator == this)
			{
				ensureAlwaysMsgf(false,
					TEXT("SetMasterPoseComponent detected loop (the input master pose chain point to itself. (%s <- %s)). Aborting... "),
					*GetNameSafe(NewMasterBoneComponent), *GetNameSafe(this));
				ValidNewMasterPose = nullptr;
				break;
			}
		}

		// if we have valid master pose, compare with input data and we warn users
		if (ValidNewMasterPose)
		{
			// Output if master is not same as input, which means it has changed. 
			UE_CLOG(ValidNewMasterPose == NewMasterBoneComponent, LogSkinnedMeshComp, Verbose,
				TEXT("MasterPoseComponent chain is detected (%s). We re-route to top-most MasterPoseComponent (%s)"),
				*GetNameSafe(ValidNewMasterPose), *GetNameSafe(NewMasterBoneComponent));
		}
	}

	// now we have valid master pose, set it
	MasterPoseComponent = ValidNewMasterPose;
	if (ValidNewMasterPose)
	{
		bool bAddNew = true;
		// make sure no empty element is there, this is weak obj ptr, so it will go away unless there is 
		// other reference, this is intentional as master to slave reference is weak
		for (auto Iter = ValidNewMasterPose->SlavePoseComponents.CreateIterator(); Iter; ++Iter)
		{
			TWeakObjectPtr<USkinnedMeshComponent> Comp = (*Iter);
			if (Comp.IsValid() == false)
			{
				// remove
				ValidNewMasterPose->SlavePoseComponents.RemoveAt(Iter.GetIndex());
				--Iter;
			}
			// if it has same as me, ignore to add
			else if (Comp.Get() == this)
			{
				bAddNew = false;
			}
		}

		if (bAddNew)
		{
			ValidNewMasterPose->AddSlavePoseComponent(this);
		}

		// set up tick dependency between master & slave components
		PrimaryComponentTick.AddPrerequisite(ValidNewMasterPose, ValidNewMasterPose->PrimaryComponentTick);
	}

	if ((OldMasterPoseComponent != nullptr) && (OldMasterPoseComponent != ValidNewMasterPose))
	{
		OldMasterPoseComponent->RemoveSlavePoseComponent(this);

		// Only remove tick dependency if the old master pose comp isn't our attach parent. We should always have a tick dependency with our parent (see USceneComponent::AttachToComponent)
		if (GetAttachParent() != OldMasterPoseComponent)
		{
			// remove tick dependency between master & slave components
			PrimaryComponentTick.RemovePrerequisite(OldMasterPoseComponent, OldMasterPoseComponent->PrimaryComponentTick);
		}
	}

	AllocateTransformData();
	RecreatePhysicsState();
	UpdateMasterBoneMap();

	// Update Slave in case Master has already been ticked, and we won't get an update for another frame.
	if (ValidNewMasterPose)
	{
		// if I have master, but I also have slaves, they won't work anymore
		// we have to reroute the slaves to new master
		if (SlavePoseComponents.Num() > 0)
		{
			UE_LOG(LogSkinnedMeshComp, Verbose,
				TEXT("MasterPoseComponent chain is detected (%s). We re-route all children to new MasterPoseComponent (%s)"),
				*GetNameSafe(this), *GetNameSafe(ValidNewMasterPose));

			// Walk through array in reverse, as changing the Slaves' MasterPoseComponent will remove them from our SlavePoseComponents array.
			const int32 NumSlaves = SlavePoseComponents.Num();
			for (int32 SlaveIndex = NumSlaves - 1; SlaveIndex >= 0; SlaveIndex--)
			{
				if (USkinnedMeshComponent* SlaveComp = SlavePoseComponents[SlaveIndex].Get())
				{
					SlaveComp->SetMasterPoseComponent(ValidNewMasterPose);
				}
			}
		}

		UpdateSlaveComponent();
	}
}

const TArray< TWeakObjectPtr<USkinnedMeshComponent> >& USkinnedMeshComponent::GetSlavePoseComponents() const
{
	return SlavePoseComponents;
}

void USkinnedMeshComponent::AddSlavePoseComponent(USkinnedMeshComponent* SkinnedMeshComponent)
{
	SlavePoseComponents.AddUnique(SkinnedMeshComponent);
}

void USkinnedMeshComponent::RemoveSlavePoseComponent(USkinnedMeshComponent* SkinnedMeshComponent)
{
	SlavePoseComponents.Remove(SkinnedMeshComponent);
}

void USkinnedMeshComponent::InvalidateCachedBounds()
{
	bCachedLocalBoundsUpToDate = false;

	// Also invalidate all slave components.
	for (const TWeakObjectPtr<USkinnedMeshComponent>& SkinnedMeshComp : SlavePoseComponents)
	{
		if (USkinnedMeshComponent* SkinnedMeshCompPtr = SkinnedMeshComp.Get())
		{
			SkinnedMeshCompPtr->bCachedLocalBoundsUpToDate = false;
		}
	}

	// We need to invalidate all attached skinned mesh components as well
	const TArray<USceneComponent*>& AttachedComps = GetAttachChildren();
	for (USceneComponent* ChildComp : AttachedComps)
	{
		if (USkinnedMeshComponent* SkinnedChild = Cast<USkinnedMeshComponent>(ChildComp))
		{
			if (SkinnedChild->bCachedLocalBoundsUpToDate)
			{
				SkinnedChild->InvalidateCachedBounds();
			}
		}
	}
}

void USkinnedMeshComponent::RefreshSlaveComponents()
{
	for (const TWeakObjectPtr<USkinnedMeshComponent>& MeshComp : SlavePoseComponents)
	{
		if (USkinnedMeshComponent* MeshCompPtr = MeshComp.Get())
		{
			// Update any children of the slave components if they are using sockets
			MeshCompPtr->UpdateChildTransforms(EUpdateTransformFlags::OnlyUpdateIfUsingSocket);

			MeshCompPtr->MarkRenderDynamicDataDirty();
			MeshCompPtr->MarkRenderTransformDirty();
		}
	}
}

void USkinnedMeshComponent::SetForceWireframe(bool InForceWireframe)
{
	if(bForceWireframe != InForceWireframe)
	{
		bForceWireframe = InForceWireframe;
		MarkRenderStateDirty();
	}
}

#if WITH_EDITOR
void USkinnedMeshComponent::SetSectionPreview(int32 InSectionIndexPreview)
{
	if (SectionIndexPreview != InSectionIndexPreview)
	{
		SectionIndexPreview = InSectionIndexPreview;
		MarkRenderStateDirty();
	}
}

void USkinnedMeshComponent::SetMaterialPreview(int32 InMaterialIndexPreview)
{
	if (MaterialIndexPreview != InMaterialIndexPreview)
	{
		MaterialIndexPreview = InMaterialIndexPreview;
		MarkRenderStateDirty();
	}
}

void USkinnedMeshComponent::SetSelectedEditorSection(int32 InSelectedEditorSection)
{
	if (SelectedEditorSection != InSelectedEditorSection)
	{
		SelectedEditorSection = InSelectedEditorSection;
		MarkRenderStateDirty();
	}
}

void USkinnedMeshComponent::SetSelectedEditorMaterial(int32 InSelectedEditorMaterial)
{
	if (SelectedEditorMaterial != InSelectedEditorMaterial)
	{
		SelectedEditorMaterial = InSelectedEditorMaterial;
		MarkRenderStateDirty();
	}
}

#endif // WITH_EDITOR

UMorphTarget* USkinnedMeshComponent::FindMorphTarget( FName MorphTargetName ) const
{
	if( SkeletalMesh != NULL )
	{
		return SkeletalMesh->FindMorphTarget(MorphTargetName);
	}

	return NULL;
}

bool USkinnedMeshComponent::GetMissingMasterBoneRelativeTransform(int32 InBoneIndex, FMissingMasterBoneCacheEntry& OutInfo) const
{
	const FReferenceSkeleton& SlaveRefSkeleton = SkeletalMesh->RefSkeleton;
	check(SlaveRefSkeleton.IsValidIndex(InBoneIndex));
	const TArray<FTransform>& BoneSpaceRefPoseTransforms = SlaveRefSkeleton.GetRefBonePose();

	OutInfo.CommonAncestorBoneIndex = INDEX_NONE;
	OutInfo.RelativeTransform = FTransform::Identity;

	FTransform RelativeTransform = BoneSpaceRefPoseTransforms[InBoneIndex];

	// we need to find a common base component-space transform in this skeletal mesh as it
	// isnt present in the master, so run up the hierarchy
	int32 CommonAncestorBoneIndex = InBoneIndex;
	while(CommonAncestorBoneIndex != INDEX_NONE)
	{
		CommonAncestorBoneIndex = SlaveRefSkeleton.GetParentIndex(CommonAncestorBoneIndex);
		if(CommonAncestorBoneIndex != INDEX_NONE)
		{
			OutInfo.CommonAncestorBoneIndex = MasterBoneMap[CommonAncestorBoneIndex];
			if(OutInfo.CommonAncestorBoneIndex != INDEX_NONE)
			{
				OutInfo.RelativeTransform = RelativeTransform;
				return true;
			}

			RelativeTransform = RelativeTransform * BoneSpaceRefPoseTransforms[CommonAncestorBoneIndex];
		}
	}

	return false;
}

void USkinnedMeshComponent::UpdateMasterBoneMap()
{
	MasterBoneMap.Reset();
	MissingMasterBoneMap.Reset();

	if (SkeletalMesh)
	{
		if (USkinnedMeshComponent* MasterPoseComponentPtr = MasterPoseComponent.Get())
		{
			if (USkeletalMesh* MasterMesh = MasterPoseComponentPtr->SkeletalMesh)
			{
				const FReferenceSkeleton& SlaveRefSkeleton = SkeletalMesh->RefSkeleton;
				const FReferenceSkeleton& MasterRefSkeleton = MasterMesh->RefSkeleton;

				MasterBoneMap.AddUninitialized(SlaveRefSkeleton.GetNum());
				if (SkeletalMesh == MasterMesh)
				{
					// if the meshes are the same, the indices must match exactly so we don't need to look them up
					for (int32 BoneIndex = 0; BoneIndex < MasterBoneMap.Num(); BoneIndex++)
					{
						MasterBoneMap[BoneIndex] = BoneIndex;
					}
				}
				else
				{
					for (int32 BoneIndex = 0; BoneIndex < MasterBoneMap.Num(); BoneIndex++)
					{
						const FName BoneName = SlaveRefSkeleton.GetBoneName(BoneIndex);
						MasterBoneMap[BoneIndex] = MasterRefSkeleton.FindBoneIndex(BoneName);
					}

					// Cache bones for any SOCKET bones that are missing in the master.
					// We assume that sockets will be potentially called more often, so we
					// leave out missing BONE transforms here to try to balance memory & performance.
					for(USkeletalMeshSocket* Socket : SkeletalMesh->GetActiveSocketList())
					{
						int32 BoneIndex = SlaveRefSkeleton.FindBoneIndex(Socket->BoneName);
						int32 MasterBoneIndex = MasterRefSkeleton.FindBoneIndex(Socket->BoneName);
						if(BoneIndex != INDEX_NONE && MasterBoneIndex == INDEX_NONE)
						{
							FMissingMasterBoneCacheEntry MissingBoneInfo;
							if(GetMissingMasterBoneRelativeTransform(BoneIndex, MissingBoneInfo))
							{
								MissingMasterBoneMap.Add(BoneIndex, MissingBoneInfo);
							}
						}
					}
				}
			}
		}
	}

	MasterBoneMapCacheCount += 1;
}

FTransform USkinnedMeshComponent::GetSocketTransform(FName InSocketName, ERelativeTransformSpace TransformSpace) const
{
	QUICK_SCOPE_CYCLE_COUNTER(USkinnedMeshComponent_GetSocketTransform);

	FTransform OutSocketTransform = GetComponentTransform();

	if (InSocketName != NAME_None)
	{
		int32 SocketBoneIndex;
		FTransform SocketLocalTransform;
		USkeletalMeshSocket const* const Socket = GetSocketInfoByName(InSocketName, SocketLocalTransform, SocketBoneIndex);
		// apply the socket transform first if we find a matching socket
		if (Socket)
		{
			if (TransformSpace == RTS_ParentBoneSpace)
			{
				//we are done just return now
				return SocketLocalTransform;
			}

			if (SocketBoneIndex != INDEX_NONE)
			{
				FTransform BoneTransform = GetBoneTransform(SocketBoneIndex);
				OutSocketTransform = SocketLocalTransform * BoneTransform;
			}
		}
		else
		{
			int32 BoneIndex = GetBoneIndex(InSocketName);
			if (BoneIndex != INDEX_NONE)
			{
				OutSocketTransform = GetBoneTransform(BoneIndex);

				if (TransformSpace == RTS_ParentBoneSpace)
				{
					FName ParentBone = GetParentBone(InSocketName);
					int32 ParentIndex = GetBoneIndex(ParentBone);
					if (ParentIndex != INDEX_NONE)
					{
						return OutSocketTransform.GetRelativeTransform(GetBoneTransform(ParentIndex));
					}
					return OutSocketTransform.GetRelativeTransform(GetComponentTransform());
				}
			}
		}
	}

	switch(TransformSpace)
	{
		case RTS_Actor:
		{
			if( AActor* Actor = GetOwner() )
			{
				return OutSocketTransform.GetRelativeTransform( Actor->GetTransform() );
			}
			break;
		}
		case RTS_Component:
		{
			return OutSocketTransform.GetRelativeTransform( GetComponentTransform() );
		}
	}

	return OutSocketTransform;
}

class USkeletalMeshSocket const* USkinnedMeshComponent::GetSocketInfoByName(FName InSocketName, FTransform& OutTransform, int32& OutBoneIndex) const
{
	const FName* OverrideSocket = SocketOverrideLookup.Find(InSocketName);
	const FName OverrideSocketName = OverrideSocket ? *OverrideSocket : InSocketName;

	USkeletalMeshSocket const* Socket = nullptr;

	if( SkeletalMesh )
	{
		int32 SocketIndex;
		Socket = SkeletalMesh->FindSocketInfo(OverrideSocketName, OutTransform, OutBoneIndex, SocketIndex);
	}
	else
	{
		if (OverrideSocket)
		{
			UE_LOG(LogSkinnedMeshComp, Warning, TEXT("GetSocketByName(%s -> override To %s): No SkeletalMesh for Component(%s) Actor(%s)"),
				*InSocketName.ToString(), *OverrideSocketName.ToString(), *GetName(), *GetNameSafe(GetOuter()));
		}
		else
		{
			UE_LOG(LogSkinnedMeshComp, Warning, TEXT("GetSocketByName(%s): No SkeletalMesh for Component(%s) Actor(%s)"),
				*OverrideSocketName.ToString(), *GetName(), *GetNameSafe(GetOuter()));
		}
	}

	return Socket;
}

class USkeletalMeshSocket const* USkinnedMeshComponent::GetSocketByName(FName InSocketName) const
{
	const FName* OverrideSocket = SocketOverrideLookup.Find(InSocketName);
	const FName OverrideSocketName = OverrideSocket ? *OverrideSocket : InSocketName;

	USkeletalMeshSocket const* Socket = nullptr;

	if( SkeletalMesh )
	{
		Socket = SkeletalMesh->FindSocket(OverrideSocketName);
	}
	else
	{
		if (OverrideSocket)
		{
			UE_LOG(LogSkinnedMeshComp, Warning, TEXT("GetSocketByName(%s -> override To %s): No SkeletalMesh for Component(%s) Actor(%s)"),
				*InSocketName.ToString(), *OverrideSocketName.ToString(), *GetName(), *GetNameSafe(GetOuter()));
		}
		else
		{
			UE_LOG(LogSkinnedMeshComp, Warning, TEXT("GetSocketByName(%s): No SkeletalMesh for Component(%s) Actor(%s)"),
				*OverrideSocketName.ToString(), *GetName(), *GetNameSafe(GetOuter()));
		}
	}

	return Socket;
}

void USkinnedMeshComponent::AddSocketOverride(FName SourceSocketName, FName OverrideSocketName, bool bWarnHasOverrided)
{
	if (FName* FoundName = SocketOverrideLookup.Find(SourceSocketName))
	{
		if (*FoundName != OverrideSocketName)
		{
			if (bWarnHasOverrided)
			{
				UE_LOG(LogSkinnedMeshComp, Warning, TEXT("AddSocketOverride(%s, %s): Component(%s) Actor(%s) has already defined an override for socket(%s), replacing %s as override"),
					*SourceSocketName.ToString(), *OverrideSocketName.ToString(), *GetName(), *GetNameSafe(GetOuter()), *SourceSocketName.ToString(), *(FoundName->ToString()));
			}
			*FoundName = OverrideSocketName;
		}
	}
	else
	{
		SocketOverrideLookup.Add(SourceSocketName, OverrideSocketName);
	}
}

void USkinnedMeshComponent::RemoveSocketOverrides(FName SourceSocketName)
{
	SocketOverrideLookup.Remove(SourceSocketName);
}

void USkinnedMeshComponent::RemoveAllSocketOverrides()
{
	SocketOverrideLookup.Reset();
}

bool USkinnedMeshComponent::DoesSocketExist(FName InSocketName) const
{
	return (GetSocketBoneName(InSocketName) != NAME_None);
}

FName USkinnedMeshComponent::GetSocketBoneName(FName InSocketName) const
{
	if(!SkeletalMesh)
	{
		return NAME_None;
	}

	const FName* OverrideSocket = SocketOverrideLookup.Find(InSocketName);
	const FName OverrideSocketName = OverrideSocket ? *OverrideSocket : InSocketName;

	// First check for a socket
	USkeletalMeshSocket const* TmpSocket = SkeletalMesh->FindSocket(OverrideSocketName);
	if( TmpSocket )
	{
		return TmpSocket->BoneName;
	}

	// If socket is not found, maybe it was just a bone name.
	if( GetBoneIndex(OverrideSocketName) != INDEX_NONE )
	{
		return OverrideSocketName;
	}

	// Doesn't exist.
	return NAME_None;
}


FQuat USkinnedMeshComponent::GetBoneQuaternion(FName BoneName, EBoneSpaces::Type Space) const
{
	int32 BoneIndex = GetBoneIndex(BoneName);

	if( BoneIndex == INDEX_NONE )
	{
		UE_LOG(LogSkinnedMeshComp, Warning, TEXT("USkinnedMeshComponent::execGetBoneQuaternion : Could not find bone: %s"), *BoneName.ToString());
		return FQuat::Identity;
	}

	FTransform BoneTransform;
	if( Space == EBoneSpaces::ComponentSpace )
	{
		const USkinnedMeshComponent* const MasterPoseComponentInst = MasterPoseComponent.Get();
		if(MasterPoseComponentInst)
		{
			if(BoneIndex < MasterBoneMap.Num())
			{
				int32 ParentBoneIndex = MasterBoneMap[BoneIndex];
				// If ParentBoneIndex is valid, grab matrix from MasterPoseComponent.
				if(	ParentBoneIndex != INDEX_NONE && 
					ParentBoneIndex < MasterPoseComponentInst->GetNumComponentSpaceTransforms())
				{
					BoneTransform = MasterPoseComponentInst->GetComponentSpaceTransforms()[ParentBoneIndex];
				}
				else
				{
					BoneTransform = FTransform::Identity;
				}
			}
			else
			{
				BoneTransform = FTransform::Identity;
			}
		}
		else
		{
			BoneTransform = GetComponentSpaceTransforms()[BoneIndex];
		}
	}
	else
	{
		BoneTransform = GetBoneTransform(BoneIndex);
	}

	BoneTransform.RemoveScaling();
	return BoneTransform.GetRotation();
}


FVector USkinnedMeshComponent::GetBoneLocation(FName BoneName, EBoneSpaces::Type Space) const
{
	int32 BoneIndex = GetBoneIndex(BoneName);
	if( BoneIndex == INDEX_NONE )
	{
		UE_LOG(LogAnimation, Log, TEXT("USkinnedMeshComponent::GetBoneLocation (%s %s): Could not find bone: %s"), *GetFullName(), *GetDetailedInfo(), *BoneName.ToString() );
		return FVector::ZeroVector;
	}

	if( Space == EBoneSpaces::ComponentSpace )
	{
		const USkinnedMeshComponent* const MasterPoseComponentInst = MasterPoseComponent.Get();
		if(MasterPoseComponentInst)
		{
			if(BoneIndex < MasterBoneMap.Num())
			{
				int32 ParentBoneIndex = MasterBoneMap[BoneIndex];
				// If ParentBoneIndex is valid, grab transform from MasterPoseComponent.
				if(	ParentBoneIndex != INDEX_NONE && 
					ParentBoneIndex < MasterPoseComponentInst->GetNumComponentSpaceTransforms())
				{
					return MasterPoseComponentInst->GetComponentSpaceTransforms()[ParentBoneIndex].GetLocation();
				}
			}
			
			// return empty vector
			return FVector::ZeroVector;			
		}
		else
		{
			return GetComponentSpaceTransforms()[BoneIndex].GetLocation();
		}
	}
	else if (Space == EBoneSpaces::WorldSpace)
	{
		// To support non-uniform scale (via LocalToWorld), use GetBoneMatrix
		return GetBoneMatrix(BoneIndex).GetOrigin();
	}
	else
	{
		check(false); // Unknown BoneSpace
		return FVector::ZeroVector;
	}
}


FVector USkinnedMeshComponent::GetBoneAxis( FName BoneName, EAxis::Type Axis ) const
{
	int32 BoneIndex = GetBoneIndex(BoneName);
	if (BoneIndex == INDEX_NONE)
	{
		UE_LOG(LogSkinnedMeshComp, Warning, TEXT("USkinnedMeshComponent::execGetBoneAxis : Could not find bone: %s"), *BoneName.ToString());
		return FVector::ZeroVector;
	}
	else if (Axis == EAxis::None)
	{
		UE_LOG(LogSkinnedMeshComp, Warning, TEXT("USkinnedMeshComponent::execGetBoneAxis: Invalid axis specified"));
		return FVector::ZeroVector;
	}
	else
	{
		return GetBoneMatrix(BoneIndex).GetUnitAxis(Axis);
	}
}

bool USkinnedMeshComponent::HasAnySockets() const
{
	return (SkeletalMesh != NULL) && (
#if WITH_EDITOR
		(SkeletalMesh->GetActiveSocketList().Num() > 0) ||
#endif
		(SkeletalMesh->RefSkeleton.GetNum() > 0));
}

void USkinnedMeshComponent::QuerySupportedSockets(TArray<FComponentSocketDescription>& OutSockets) const
{
	if (SkeletalMesh != NULL)
	{
		// Grab all the mesh and skeleton sockets
		const TArray<USkeletalMeshSocket*> AllSockets = SkeletalMesh->GetActiveSocketList();

		for (int32 SocketIdx = 0; SocketIdx < AllSockets.Num(); ++SocketIdx)
		{
			if (USkeletalMeshSocket* Socket = AllSockets[SocketIdx])
			{
				new (OutSockets) FComponentSocketDescription(Socket->SocketName, EComponentSocketType::Socket);
			}
		}

		// Now grab the bones, which can behave exactly like sockets
		for (int32 BoneIdx = 0; BoneIdx < SkeletalMesh->RefSkeleton.GetNum(); ++BoneIdx)
		{
			const FName BoneName = SkeletalMesh->RefSkeleton.GetBoneName(BoneIdx);
			new (OutSockets) FComponentSocketDescription(BoneName, EComponentSocketType::Bone);
		}
	}
}

bool USkinnedMeshComponent::UpdateOverlapsImpl(const TOverlapArrayView* PendingOverlaps, bool bDoNotifies, const TOverlapArrayView* OverlapsAtEndLocation)
{
	// we don't support overlap test on destructible or physics asset
	// so use SceneComponent::UpdateOverlaps to handle children
	return USceneComponent::UpdateOverlapsImpl(PendingOverlaps, bDoNotifies, OverlapsAtEndLocation);
}

void USkinnedMeshComponent::TransformToBoneSpace(FName BoneName, FVector InPosition, FRotator InRotation, FVector& OutPosition, FRotator& OutRotation) const
{
	int32 BoneIndex = GetBoneIndex(BoneName);
	if(BoneIndex != INDEX_NONE)
	{
		FMatrix BoneToWorldTM = GetBoneMatrix(BoneIndex);
		FMatrix WorldTM = FRotationTranslationMatrix(InRotation, InPosition);
		FMatrix LocalTM = WorldTM * BoneToWorldTM.Inverse();

		OutPosition = LocalTM.GetOrigin();
		OutRotation = LocalTM.Rotator();
	}
}


void USkinnedMeshComponent::TransformFromBoneSpace(FName BoneName, FVector InPosition, FRotator InRotation, FVector& OutPosition, FRotator& OutRotation)
{
	int32 BoneIndex = GetBoneIndex(BoneName);
	if(BoneIndex != INDEX_NONE)
	{
		FMatrix BoneToWorldTM = GetBoneMatrix(BoneIndex);

		FMatrix LocalTM = FRotationTranslationMatrix(InRotation, InPosition);
		FMatrix WorldTM = LocalTM * BoneToWorldTM;

		OutPosition = WorldTM.GetOrigin();
		OutRotation = WorldTM.Rotator();
	}
}



FName USkinnedMeshComponent::FindClosestBone(FVector TestLocation, FVector* BoneLocation, float IgnoreScale, bool bRequirePhysicsAsset) const
{
	if (SkeletalMesh == NULL)
	{
		if (BoneLocation != NULL)
		{
			*BoneLocation = FVector::ZeroVector;
		}
		return NAME_None;
	}
	else
	{
		// cache the physics asset
		const UPhysicsAsset* PhysAsset = GetPhysicsAsset();
		if (bRequirePhysicsAsset && !PhysAsset)
		{
			if (BoneLocation != NULL)
			{
				*BoneLocation = FVector::ZeroVector;
			}
			return NAME_None;
		}

		// transform the TestLocation into mesh local space so we don't have to transform the (mesh local) bone locations
		TestLocation = GetComponentTransform().InverseTransformPosition(TestLocation);
		
		float IgnoreScaleSquared = FMath::Square(IgnoreScale);
		float BestDistSquared = BIG_NUMBER;
		int32 BestIndex = -1;

		const USkinnedMeshComponent* BaseComponent = MasterPoseComponent.IsValid() ? MasterPoseComponent.Get() : this;
		const TArray<FTransform>& CompSpaceTransforms = BaseComponent->GetComponentSpaceTransforms();

		for (int32 i = 0; i < BaseComponent->GetNumComponentSpaceTransforms(); i++)
		{
			// If we require a physics asset, then look it up in the map
			bool bPassPACheck = !bRequirePhysicsAsset;
			if (bRequirePhysicsAsset)
			{
				FName BoneName = SkeletalMesh->RefSkeleton.GetBoneName(i);
				bPassPACheck = (PhysAsset->BodySetupIndexMap.Find(BoneName) != nullptr);
			}

			if (bPassPACheck && (IgnoreScale < 0.f || CompSpaceTransforms[i].GetScaledAxis(EAxis::X).SizeSquared() > IgnoreScaleSquared))
			{
				float DistSquared = (TestLocation - CompSpaceTransforms[i].GetLocation()).SizeSquared();
				if (DistSquared < BestDistSquared)
				{
					BestIndex = i;
					BestDistSquared = DistSquared;
				}
			}
		}

		if (BestIndex == -1)
		{
			if (BoneLocation != NULL)
			{
				*BoneLocation = FVector::ZeroVector;
			}
			return NAME_None;
		}
		else
		{
			// transform the bone location into world space
			if (BoneLocation != NULL)
			{
				*BoneLocation = (CompSpaceTransforms[BestIndex] * GetComponentTransform()).GetLocation();
			}
			return SkeletalMesh->RefSkeleton.GetBoneName(BestIndex);
		}
	}
}

FName USkinnedMeshComponent::FindClosestBone_K2(FVector TestLocation, FVector& BoneLocation, float IgnoreScale, bool bRequirePhysicsAsset) const
{
	BoneLocation = FVector::ZeroVector;
	return FindClosestBone(TestLocation, &BoneLocation, IgnoreScale, bRequirePhysicsAsset);
}

void USkinnedMeshComponent::ShowMaterialSection(int32 MaterialID, int32 SectionIndex, bool bShow, int32 LODIndex)
{
	if (!SkeletalMesh)
	{
		// no skeletalmesh, then nothing to do. 
		return;
	}
	// Make sure LOD info for this component has been initialized
	InitLODInfos();
	if (LODInfo.IsValidIndex(LODIndex))
	{
		const FSkeletalMeshLODInfo& SkelLODInfo = *SkeletalMesh->GetLODInfo(LODIndex);
		FSkelMeshComponentLODInfo& SkelCompLODInfo = LODInfo[LODIndex];
		TArray<bool>& HiddenMaterials = SkelCompLODInfo.HiddenMaterials;
	
		// allocate if not allocated yet
		if ( HiddenMaterials.Num() != SkeletalMesh->Materials.Num() )
		{
			// Using skeletalmesh component because Materials.Num() should be <= SkeletalMesh->Materials.Num()		
			HiddenMaterials.Empty(SkeletalMesh->Materials.Num());
			HiddenMaterials.AddZeroed(SkeletalMesh->Materials.Num());
		}
		// If we have a valid LODInfo LODMaterialMap, route material index through it.
		int32 UseMaterialIndex = MaterialID;			
		if(SkelLODInfo.LODMaterialMap.IsValidIndex(SectionIndex) && SkelLODInfo.LODMaterialMap[SectionIndex] != INDEX_NONE)
		{
			UseMaterialIndex = SkelLODInfo.LODMaterialMap[SectionIndex];
			UseMaterialIndex = FMath::Clamp( UseMaterialIndex, 0, HiddenMaterials.Num() );
		}
		// Mark the mapped section material entry as visible/hidden
		if (HiddenMaterials.IsValidIndex(UseMaterialIndex))
		{
			HiddenMaterials[UseMaterialIndex] = !bShow;
		}

		if ( MeshObject )
		{
			// need to send render thread for updated hidden section
			FSkeletalMeshObject* InMeshObject = MeshObject;
			ENQUEUE_RENDER_COMMAND(FUpdateHiddenSectionCommand)(
				[InMeshObject, HiddenMaterials, LODIndex](FRHICommandListImmediate& RHICmdList)
			{
				InMeshObject->SetHiddenMaterials(LODIndex, HiddenMaterials);
			});
		}
	}
}

void USkinnedMeshComponent::ShowAllMaterialSections(int32 LODIndex)
{
	InitLODInfos();
	if (LODInfo.IsValidIndex(LODIndex))
	{
		FSkelMeshComponentLODInfo& SkelCompLODInfo = LODInfo[LODIndex];
		TArray<bool>& HiddenMaterials = SkelCompLODInfo.HiddenMaterials;

		// Only need to do anything if array is allocated - otherwise nothing is being hidden
		if (HiddenMaterials.Num() > 0)
		{
			for (int32 MatIdx = 0; MatIdx < HiddenMaterials.Num(); MatIdx++)
			{
				HiddenMaterials[MatIdx] = false;
			}

			if (MeshObject)
			{
				// need to send render thread for updated hidden section
				FSkeletalMeshObject* InMeshObject = MeshObject;
				ENQUEUE_RENDER_COMMAND(FUpdateHiddenSectionCommand)(
					[InMeshObject, HiddenMaterials, LODIndex](FRHICommandListImmediate& RHICmdList)
					{
						InMeshObject->SetHiddenMaterials(LODIndex, HiddenMaterials);
					});
			}
		}
	}
}

bool USkinnedMeshComponent::IsMaterialSectionShown(int32 MaterialID, int32 LODIndex)
{
	bool bHidden = false;
	if (LODInfo.IsValidIndex(LODIndex))
	{
		FSkelMeshComponentLODInfo& SkelCompLODInfo = LODInfo[LODIndex];
		TArray<bool>& HiddenMaterials = SkelCompLODInfo.HiddenMaterials;
		if (HiddenMaterials.IsValidIndex(MaterialID))
		{
			bHidden = HiddenMaterials[MaterialID];
		}
	}
	return !bHidden;
}


void USkinnedMeshComponent::GetUsedMaterials( TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials ) const
{
	if( SkeletalMesh )
	{
		// The max number of materials used is the max of the materials on the skeletal mesh and the materials on the mesh component
		const int32 NumMaterials = FMath::Max( SkeletalMesh->Materials.Num(), OverrideMaterials.Num() );
		for( int32 MatIdx = 0; MatIdx < NumMaterials; ++MatIdx )
		{
			// GetMaterial will determine the correct material to use for this index.  
			UMaterialInterface* MaterialInterface = GetMaterial( MatIdx );
			OutMaterials.Add( MaterialInterface );
		}

		for (int32 MatIdx = 0; MatIdx < SecondaryMaterials.Num(); ++MatIdx)
		{
			UMaterialInterface* MaterialInterface = GetSecondaryMaterial(MatIdx);
			OutMaterials.Add(MaterialInterface);
		}
	}

	if (bGetDebugMaterials)
	{
#if WITH_EDITOR
		if (UPhysicsAsset* PhysicsAssetForDebug = GetPhysicsAsset())
		{
			PhysicsAssetForDebug->GetUsedMaterials(OutMaterials);
		}
#endif
	}
}

FSkinWeightVertexBuffer* USkinnedMeshComponent::GetSkinWeightBuffer(int32 LODIndex) const
{
	FSkinWeightVertexBuffer* WeightBuffer = nullptr;

	if (SkeletalMesh && 
		SkeletalMesh->GetResourceForRendering() && 
		SkeletalMesh->GetResourceForRendering()->LODRenderData.IsValidIndex(LODIndex) )
	{
		FSkeletalMeshLODRenderData& LODData = SkeletalMesh->GetResourceForRendering()->LODRenderData[LODIndex];

		// Grab weight buffer (check for override)
		if (LODInfo.IsValidIndex(LODIndex) &&
			LODInfo[LODIndex].OverrideSkinWeights && 
			LODInfo[LODIndex].OverrideSkinWeights->GetNumVertices() == LODData.GetNumVertices())
		{
			WeightBuffer = LODInfo[LODIndex].OverrideSkinWeights;
		}
		else if (LODInfo.IsValidIndex(LODIndex) &&
			LODInfo[LODIndex].OverrideProfileSkinWeights &&
			LODInfo[LODIndex].OverrideProfileSkinWeights->GetNumVertices() == LODData.GetNumVertices())
		{
			WeightBuffer = LODInfo[LODIndex].OverrideProfileSkinWeights;
		}
		else
		{
			WeightBuffer = LODData.GetSkinWeightVertexBuffer();
		}
	}

	return WeightBuffer;
}

FVector USkinnedMeshComponent::GetSkinnedVertexPosition(USkinnedMeshComponent* Component, int32 VertexIndex, const FSkeletalMeshLODRenderData& LODData, FSkinWeightVertexBuffer& SkinWeightBuffer) 
{
	FVector SkinnedPos(0, 0, 0);

	int32 SectionIndex;
	int32 VertIndex;
	LODData.GetSectionFromVertexIndex(VertexIndex, SectionIndex, VertIndex);

	check(SectionIndex < LODData.RenderSections.Num());
	const FSkelMeshRenderSection& Section = LODData.RenderSections[SectionIndex];

	return GetTypedSkinnedVertexPosition<false>(Component, Section, LODData.StaticVertexBuffers.PositionVertexBuffer, SkinWeightBuffer, VertIndex);
}

FVector USkinnedMeshComponent::GetSkinnedVertexPosition(USkinnedMeshComponent* Component, int32 VertexIndex, const FSkeletalMeshLODRenderData& LODData, FSkinWeightVertexBuffer& SkinWeightBuffer, TArray<FMatrix>& CachedRefToLocals) 
{
	FVector SkinnedPos(0, 0, 0);
	
	int32 SectionIndex;
	int32 VertIndex;
	LODData.GetSectionFromVertexIndex(VertexIndex, SectionIndex, VertIndex);

	check(SectionIndex < LODData.RenderSections.Num());
	const FSkelMeshRenderSection& Section = LODData.RenderSections[SectionIndex];

	return GetTypedSkinnedVertexPosition<false>(Component, Section, LODData.StaticVertexBuffers.PositionVertexBuffer, SkinWeightBuffer, VertIndex, CachedRefToLocals);
}

void USkinnedMeshComponent::SetRefPoseOverride(const TArray<FTransform>& NewRefPoseTransforms)
{
	if (!SkeletalMesh)
	{
		UE_LOG(LogSkeletalMesh, Warning, TEXT("SetRefPoseOverride (%s) : Not valid without SkeletalMesh assigned."), *GetName());
		return;
	}

	const int32 NumRealBones = SkeletalMesh->RefSkeleton.GetRawBoneNum();

	if (NumRealBones != NewRefPoseTransforms.Num())
	{
		UE_LOG(LogSkeletalMesh, Warning, TEXT("SetRefPoseOverride (%s) : Expected %d transforms, got %d."), *SkeletalMesh->GetName(), NumRealBones, NewRefPoseTransforms.Num());
		return;
	}

	// If override exists, reset info
	if (RefPoseOverride)
	{
		RefPoseOverride->RefBasesInvMatrix.Reset();
		RefPoseOverride->RefBonePoses.Reset();
	}
	// If not, allocate new struct to keep info
	else
	{
		RefPoseOverride = new FSkelMeshRefPoseOverride();
	}

	// Copy input transforms into override data
	RefPoseOverride->RefBonePoses = NewRefPoseTransforms;

	// Allocate output inv matrices
	RefPoseOverride->RefBasesInvMatrix.AddUninitialized(NumRealBones);

	// Reset cached mesh-space ref pose
	TArray<FMatrix> CachedComposedRefPoseMatrices;
	CachedComposedRefPoseMatrices.AddUninitialized(NumRealBones);

	// Compute the RefBasesInvMatrix array
	for (int32 BoneIndex = 0; BoneIndex < NumRealBones; BoneIndex++)
	{
		FTransform BoneTransform = RefPoseOverride->RefBonePoses[BoneIndex];
		// Make sure quaternion is normalized!
		BoneTransform.NormalizeRotation();

		// Render the default pose.
		CachedComposedRefPoseMatrices[BoneIndex] = BoneTransform.ToMatrixWithScale();

		// Construct mesh-space skeletal hierarchy.
		if (BoneIndex > 0)
		{
			int32 ParentIndex = SkeletalMesh->RefSkeleton.GetRawParentIndex(BoneIndex);
			CachedComposedRefPoseMatrices[BoneIndex] = CachedComposedRefPoseMatrices[BoneIndex] * CachedComposedRefPoseMatrices[ParentIndex];
		}

		// Check for zero matrix
		FVector XAxis, YAxis, ZAxis;
		CachedComposedRefPoseMatrices[BoneIndex].GetScaledAxes(XAxis, YAxis, ZAxis);
		if (XAxis.IsNearlyZero(SMALL_NUMBER) &&
			YAxis.IsNearlyZero(SMALL_NUMBER) &&
			ZAxis.IsNearlyZero(SMALL_NUMBER))
		{
			// this is not allowed, warn them 
			UE_LOG(LogSkeletalMesh, Warning, TEXT("Reference Pose for asset %s for joint (%s) includes NIL matrix. Zero scale isn't allowed on ref pose. "), *SkeletalMesh->GetPathName(), *SkeletalMesh->RefSkeleton.GetBoneName(BoneIndex).ToString());
		}

		// Precompute inverse so we can use from-refpose-skin vertices.
		RefPoseOverride->RefBasesInvMatrix[BoneIndex] = CachedComposedRefPoseMatrices[BoneIndex].Inverse();
	}
}

void USkinnedMeshComponent::ClearRefPoseOverride()
{
	// Release mem for override info
	if (RefPoseOverride)
	{
		delete RefPoseOverride;
		RefPoseOverride = nullptr;
	}
}

void USkinnedMeshComponent::CacheRefToLocalMatrices(TArray<FMatrix>& OutRefToLocal)const
{
	const USkinnedMeshComponent* BaseComponent = GetBaseComponent();
	OutRefToLocal.SetNumUninitialized(SkeletalMesh->RefBasesInvMatrix.Num());
	const TArray<FTransform>& CompSpaceTransforms = BaseComponent->GetComponentSpaceTransforms();
	if(CompSpaceTransforms.Num())
	{
		check(CompSpaceTransforms.Num() >= OutRefToLocal.Num());

		for (int32 MatrixIdx = 0; MatrixIdx < OutRefToLocal.Num(); ++MatrixIdx)
		{
			OutRefToLocal[MatrixIdx] = SkeletalMesh->RefBasesInvMatrix[MatrixIdx] * CompSpaceTransforms[MatrixIdx].ToMatrixWithScale();
		}
	}
	else
	{
		//Possible in some cases to request this before the component space transforms are prepared (undo/redo)
		for (int32 MatrixIdx = 0; MatrixIdx < OutRefToLocal.Num(); ++MatrixIdx)
		{
			OutRefToLocal[MatrixIdx] = SkeletalMesh->RefBasesInvMatrix[MatrixIdx];
		}
	}
}

void USkinnedMeshComponent::ComputeSkinnedPositions(USkinnedMeshComponent* Component, TArray<FVector> & OutPositions, TArray<FMatrix>& CachedRefToLocals, const FSkeletalMeshLODRenderData& LODData, const FSkinWeightVertexBuffer& SkinWeightBuffer)
{
	OutPositions.Empty();

	// Fail if no mesh
	if (!Component || !Component->SkeletalMesh)
	{
		return;
	}
	OutPositions.AddUninitialized(LODData.GetNumVertices());

	//update positions
	for (int32 SectionIdx = 0; SectionIdx < LODData.RenderSections.Num(); ++SectionIdx)
	{
		const FSkelMeshRenderSection& Section = LODData.RenderSections[SectionIdx];
		{
			//soft
			const uint32 SoftOffset = Section.BaseVertexIndex;
			const uint32 NumSoftVerts = Section.NumVertices;
			for (uint32 SoftIdx = 0; SoftIdx < NumSoftVerts; ++SoftIdx)
			{
				FVector SkinnedPosition = GetTypedSkinnedVertexPosition<true>(Component, Section, LODData.StaticVertexBuffers.PositionVertexBuffer, SkinWeightBuffer, SoftIdx, CachedRefToLocals);
				OutPositions[SoftOffset + SoftIdx] = SkinnedPosition;
			}
		}
	}
}

FColor USkinnedMeshComponent::GetVertexColor(int32 VertexIndex) const
{
	// Fail if no mesh or no color vertex buffer.
	FColor FallbackColor = FColor(255, 255, 255, 255);
	if (!SkeletalMesh || !MeshObject)
	{
		return FallbackColor;
	}

	// If there is an override, return that
	if (LODInfo.Num() > 0 && 
		LODInfo[0].OverrideVertexColors != nullptr && 
		LODInfo[0].OverrideVertexColors->IsInitialized() &&
		VertexIndex < (int32)LODInfo[0].OverrideVertexColors->GetNumVertices() )
	{
		return LODInfo[0].OverrideVertexColors->VertexColor(VertexIndex);
	}

	FSkeletalMeshLODRenderData& LODData = MeshObject->GetSkeletalMeshRenderData().LODRenderData[0];
	
	if (!LODData.StaticVertexBuffers.ColorVertexBuffer.IsInitialized())
	{
		return FallbackColor;
	}

	// Find the chunk and vertex within that chunk, and skinning type, for this vertex.
	int32 SectionIndex;
	int32 VertIndex;
	LODData.GetSectionFromVertexIndex(VertexIndex, SectionIndex, VertIndex);

	check(SectionIndex < LODData.RenderSections.Num());
	const FSkelMeshRenderSection& Section = LODData.RenderSections[SectionIndex];
	
	int32 VertexBase = Section.BaseVertexIndex;

	return LODData.StaticVertexBuffers.ColorVertexBuffer.VertexColor(VertexBase + VertIndex);
}

FVector2D USkinnedMeshComponent::GetVertexUV(int32 VertexIndex, uint32 UVChannel) const
{
	// Fail if no mesh or no vertex buffer.
	FVector2D FallbackUV = FVector2D::ZeroVector;
	if (!SkeletalMesh || !MeshObject)
	{
		return FallbackUV;
	}

	FSkeletalMeshLODRenderData& LODData = MeshObject->GetSkeletalMeshRenderData().LODRenderData[0];
	
	if (!LODData.StaticVertexBuffers.StaticMeshVertexBuffer.IsInitialized())
	{
		return FallbackUV;
	}

	// Find the chunk and vertex within that chunk, and skinning type, for this vertex.
	int32 SectionIndex;
	int32 VertIndex;
	LODData.GetSectionFromVertexIndex(VertexIndex, SectionIndex, VertIndex);

	check(SectionIndex < LODData.RenderSections.Num());
	const FSkelMeshRenderSection& Section = LODData.RenderSections[SectionIndex];
	
	int32 VertexBase = Section.BaseVertexIndex;
	uint32 ClampedUVChannel = FMath::Min(UVChannel, LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords());

	return LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(VertexBase + VertIndex, ClampedUVChannel);
}

void USkinnedMeshComponent::HideBone( int32 BoneIndex, EPhysBodyOp PhysBodyOption)
{
	TArray<uint8>& EditableBoneVisibilityStates = GetEditableBoneVisibilityStates();
	if (ShouldUpdateBoneVisibility() && BoneIndex < EditableBoneVisibilityStates.Num())
	{
		checkSlow ( BoneIndex != INDEX_NONE );
		EditableBoneVisibilityStates[ BoneIndex ] = BVS_ExplicitlyHidden;
		RebuildVisibilityArray();
	}
}


void USkinnedMeshComponent::UnHideBone( int32 BoneIndex )
{
	TArray<uint8>& EditableBoneVisibilityStates = GetEditableBoneVisibilityStates();
	if (ShouldUpdateBoneVisibility() && BoneIndex < EditableBoneVisibilityStates.Num())
	{
		checkSlow ( BoneIndex != INDEX_NONE );
		//@TODO: If unhiding the child of a still hidden bone (coming in, BoneVisibilityStates(RefSkel(BoneIndex).ParentIndex) != BVS_Visible),
		// should we be re-enabling collision bodies?
		// Setting visible to true here is OK in either case as it will be reset to BVS_HiddenByParent in RecalcRequiredBones later if needed.
		EditableBoneVisibilityStates[ BoneIndex ] = BVS_Visible;
		RebuildVisibilityArray();
	}
}


bool USkinnedMeshComponent::IsBoneHidden( int32 BoneIndex ) const
{
	const TArray<uint8>& EditableBoneVisibilityStates = GetEditableBoneVisibilityStates();
	if (ShouldUpdateBoneVisibility() && BoneIndex < EditableBoneVisibilityStates.Num())
	{
		if ( BoneIndex != INDEX_NONE )
		{
			return EditableBoneVisibilityStates[ BoneIndex ] != BVS_Visible;
		}
	}
	else if (USkinnedMeshComponent* MasterPoseComponentPtr = MasterPoseComponent.Get())
	{
		return MasterPoseComponentPtr->IsBoneHidden( BoneIndex );
	}

	return false;
}


bool USkinnedMeshComponent::IsBoneHiddenByName( FName BoneName )
{
	// Find appropriate BoneIndex
	int32 BoneIndex = GetBoneIndex(BoneName);
	if(BoneIndex != INDEX_NONE)
	{
		return IsBoneHidden(BoneIndex);
	}

	return false;
}

void USkinnedMeshComponent::HideBoneByName( FName BoneName, EPhysBodyOp PhysBodyOption )
{
	// Find appropriate BoneIndex
	int32 BoneIndex = GetBoneIndex(BoneName);
	if ( BoneIndex != INDEX_NONE )
	{
		HideBone(BoneIndex, PhysBodyOption);
	}
}


void USkinnedMeshComponent::UnHideBoneByName( FName BoneName )
{
	int32 BoneIndex = GetBoneIndex(BoneName);
	if ( BoneIndex != INDEX_NONE )
	{
		UnHideBone(BoneIndex);
	}
}

void USkinnedMeshComponent::SetForcedLOD(int32 InNewForcedLOD)
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	const int32 OldValue = ForcedLodModel;
	ForcedLodModel = FMath::Clamp(InNewForcedLOD, 0, GetNumLODs());
	if (OldValue != ForcedLodModel)
	{
		IStreamingManager::Get().NotifyPrimitiveUpdated(this);
	}
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

int32 USkinnedMeshComponent::GetForcedLOD() const
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return ForcedLodModel;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

int32 USkinnedMeshComponent::GetNumLODs() const
{
	int32 NumLODs = 0;
	FSkeletalMeshRenderData* RenderData = GetSkeletalMeshRenderData();
	if (RenderData)
	{
		NumLODs = RenderData->LODRenderData.Num();
	}
	return NumLODs;
}


void USkinnedMeshComponent::SetMinLOD(int32 InNewMinLOD)
{
	int32 MaxLODIndex = GetNumLODs() - 1;
	MinLodModel = FMath::Clamp(InNewMinLOD, 0, MaxLODIndex);
}

int32 USkinnedMeshComponent::ComputeMinLOD() const
{
	int32 MinLodIndex = bOverrideMinLod ? MinLodModel : SkeletalMesh->MinLod.GetValue();
	int32 NumLODs = GetNumLODs();
	// want to make sure MinLOD stays within the valid range
	MinLodIndex = FMath::Min(MinLodIndex, NumLODs - 1);
	MinLodIndex = FMath::Max(MinLodIndex, 0);
	return MinLodIndex;
}

#if WITH_EDITOR
int32 USkinnedMeshComponent::GetLODBias() const
{
	return GSkeletalMeshLODBias;
}
#endif

void USkinnedMeshComponent::SetCastCapsuleDirectShadow(bool bNewValue)
{
	if (bNewValue != bCastCapsuleDirectShadow)
	{
		bCastCapsuleDirectShadow = bNewValue;
		MarkRenderStateDirty();
	}
}

void USkinnedMeshComponent::SetCastCapsuleIndirectShadow(bool bNewValue)
{
	if (bNewValue != bCastCapsuleIndirectShadow)
	{
		bCastCapsuleIndirectShadow = bNewValue;
		MarkRenderStateDirty();
	}
}

void USkinnedMeshComponent::SetCapsuleIndirectShadowMinVisibility(float NewValue)
{
	if (NewValue != CapsuleIndirectShadowMinVisibility)
	{
		CapsuleIndirectShadowMinVisibility = NewValue;
		MarkRenderStateDirty();
	}
}

// @todo: think about consolidating this with UpdateLODStatus_Internal
int32 USkinnedMeshComponent::GetDesiredSyncLOD() const
{
	if (SkeletalMesh && MeshObject)
	{
#if WITH_EDITOR
		const int32 LODBias = GetLODBias();
#else
		const int32 LODBias = GSkeletalMeshLODBias;
#endif
		return MeshObject->MinDesiredLODLevel + LODBias;
	}

	return INDEX_NONE;
}

void USkinnedMeshComponent::SetSyncLOD(int32 LODIndex)
{
	SetForcedLOD(LODIndex + 1);
}

int32 USkinnedMeshComponent::GetCurrentSyncLOD() const
{
	return GetForcedLOD() - 1; // Weird API for forced LOD where 0 means auto, 1 means force to 0 etc
}

int32 USkinnedMeshComponent::GetNumSyncLODs() const
{
	return GetNumLODs();
}

bool USkinnedMeshComponent::UpdateLODStatus()
{
	return UpdateLODStatus_Internal(INDEX_NONE);
}

bool USkinnedMeshComponent::UpdateLODStatus_Internal(int32 InMasterPoseComponentPredictedLODLevel)
{
	SCOPED_NAMED_EVENT(USkinnedMeshComponent_UpdateLODStatus, FColor::Red);

	// Predict the best (min) LOD level we are going to need. Basically we use the Min (best) LOD the renderer desired last frame.
	// Because we update bones based on this LOD level, we have to update bones to this LOD before we can allow rendering at it.

	const int32 OldPredictedLODLevel = PredictedLODLevel;
	int32 NewPredictedLODLevel = OldPredictedLODLevel;

	if (SkeletalMesh != nullptr)
	{
#if WITH_EDITOR
		const int32 LODBias = GetLODBias();
#else
		const int32 LODBias = GSkeletalMeshLODBias;
#endif

		int32 MinLodIndex = ComputeMinLOD();
		int32 MaxLODIndex = MinLodIndex;
		if (MeshObject)
		{
			MaxLODIndex = MeshObject->GetSkeletalMeshRenderData().LODRenderData.Num() - 1;
			MaxDistanceFactor = MeshObject->MaxDistanceFactor;
		}

		// Support forcing to a particular LOD.
		const int32 LocalForcedLodModel = GetForcedLOD();
		if (LocalForcedLodModel > 0)
		{
			NewPredictedLODLevel = FMath::Clamp(LocalForcedLodModel - 1, MinLodIndex, MaxLODIndex);
		}
		else
		{
			// Match LOD of MasterPoseComponent if it exists.
			if (InMasterPoseComponentPredictedLODLevel != INDEX_NONE && !bIgnoreMasterPoseComponentLOD)
			{
				NewPredictedLODLevel = FMath::Clamp(InMasterPoseComponentPredictedLODLevel, 0, MaxLODIndex);
			}
			else if (bSyncAttachParentLOD && GetAttachParent() && GetAttachParent()->IsA(USkinnedMeshComponent::StaticClass()))
			{
				NewPredictedLODLevel = FMath::Clamp(CastChecked<USkinnedMeshComponent>(GetAttachParent())->PredictedLODLevel, 0, MaxLODIndex);
			}
			else if (MeshObject)
			{
				NewPredictedLODLevel = FMath::Clamp(MeshObject->MinDesiredLODLevel + LODBias, 0, MaxLODIndex);
			}
			// If no MeshObject - just assume lowest LOD.
			else
			{
				NewPredictedLODLevel = MaxLODIndex;
			}

			// now check to see if we have a MinLODLevel and apply it
			if ((MinLodIndex > 0))
			{
				if(MinLodIndex <= MaxLODIndex)
				{
					NewPredictedLODLevel = FMath::Clamp(NewPredictedLODLevel, MinLodIndex, MaxLODIndex);
				}
				else
				{
					NewPredictedLODLevel = MaxLODIndex;
				}
			}
		}

		if (SkeletalMesh->IsStreamable() && MeshObject)
		{
			NewPredictedLODLevel = FMath::Max<int32>(NewPredictedLODLevel, MeshObject->GetSkeletalMeshRenderData().PendingFirstLODIdx);
		}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		if (CVarAnimVisualizeLODs.GetValueOnAnyThread() != 0)
		{
			// Reduce to visible animated, non SyncAttachParentLOD to reduce clutter.
			if (SkeletalMesh && MeshObject && bRecentlyRendered)
			{
				const bool bHasValidSyncAttachParent = bSyncAttachParentLOD && GetAttachParent() && GetAttachParent()->IsA(USkinnedMeshComponent::StaticClass());
				if (!bHasValidSyncAttachParent)
				{
					const float ScreenSize = FMath::Sqrt(MeshObject->MaxDistanceFactor) * 2.f;
					FString DebugString = FString::Printf(TEXT("PredictedLODLevel(%d)\nMinDesiredLODLevel(%d) ForcedLodModel(%d) MinLodIndex(%d) LODBias(%d)\nMaxDistanceFactor(%f) ScreenSize(%f)"),
						PredictedLODLevel, MeshObject->MinDesiredLODLevel, LocalForcedLodModel, MinLodIndex, LODBias, MeshObject->MaxDistanceFactor, ScreenSize);

					// See if Child classes want to add something.
					UpdateVisualizeLODString(DebugString);

					FColor DrawColor = FColor::White;
					switch (PredictedLODLevel)
					{
					case 0: DrawColor = FColor::White; break;
					case 1: DrawColor = FColor::Green; break;
					case 2: DrawColor = FColor::Yellow; break;
					case 3: DrawColor = FColor::Red; break;
					default:
						DrawColor = FColor::Purple; break;
					}

					DrawDebugString(GetWorld(), Bounds.Origin, DebugString, nullptr, DrawColor, 0.f, true, 1.2f);
				}
			}
		}
#endif
	}
	else
	{
		NewPredictedLODLevel = 0;
	}

	// See if LOD has changed. 
	bool bLODChanged = (NewPredictedLODLevel != OldPredictedLODLevel);
	PredictedLODLevel = NewPredictedLODLevel;

	// also update slave component LOD status, as we may need to recalc required bones if this changes
	// independently of our LOD
	for (const TWeakObjectPtr<USkinnedMeshComponent>& SlaveComponent : SlavePoseComponents)
	{
		if (USkinnedMeshComponent* SlaveComponentPtr = SlaveComponent.Get())
		{
			bLODChanged |= SlaveComponentPtr->UpdateLODStatus_Internal(NewPredictedLODLevel);
		}
	}

	return bLODChanged;
}

void USkinnedMeshComponent::FinalizeBoneTransform()
{
	FlipEditableSpaceBases();
	// we finalized bone transform, now we have valid bone buffer
	bHasValidBoneTransform = true;
}

void USkinnedMeshComponent::FlipEditableSpaceBases()
{
	if (bNeedToFlipSpaceBaseBuffers)
	{
		bNeedToFlipSpaceBaseBuffers = false;

		if (bDoubleBufferedComponentSpaceTransforms)
		{
			CurrentReadComponentTransforms = CurrentEditableComponentTransforms;
			CurrentEditableComponentTransforms = 1 - CurrentEditableComponentTransforms;

			// copy to other buffer if we dont already have a valid set of transforms
			if (!bHasValidBoneTransform)
			{
				GetEditableComponentSpaceTransforms() = GetComponentSpaceTransforms();
				GetEditableBoneVisibilityStates() = GetBoneVisibilityStates();
				bBoneVisibilityDirty = false;
			}
			// If we have changed bone visibility, then we need to reflect that next frame
			else if(bBoneVisibilityDirty)
			{
				GetEditableBoneVisibilityStates() = GetBoneVisibilityStates();
				bBoneVisibilityDirty = false;
			}
		}
		else
		{
			// save previous transform if it's valid
			if (bHasValidBoneTransform)
			{
				PreviousComponentSpaceTransformsArray = GetComponentSpaceTransforms();
				PreviousBoneVisibilityStates = GetBoneVisibilityStates();
			}

			CurrentReadComponentTransforms = CurrentEditableComponentTransforms = 0;

			// if we don't have a valid transform, we copy after we write, so that it doesn't cause motion blur
			if (!bHasValidBoneTransform)
			{
				PreviousComponentSpaceTransformsArray = GetComponentSpaceTransforms();
				PreviousBoneVisibilityStates = GetBoneVisibilityStates();
			}
		}

		++CurrentBoneTransformRevisionNumber;
	}
}

void USkinnedMeshComponent::SetComponentSpaceTransformsDoubleBuffering(bool bInDoubleBufferedComponentSpaceTransforms)
{
	bDoubleBufferedComponentSpaceTransforms = bInDoubleBufferedComponentSpaceTransforms;

	if (bDoubleBufferedComponentSpaceTransforms)
	{
		CurrentEditableComponentTransforms = 1 - CurrentReadComponentTransforms;
	}
	else
	{
		CurrentEditableComponentTransforms = CurrentReadComponentTransforms = 0;
	}
}

void USkinnedMeshComponent::GetCPUSkinnedVertices(TArray<FFinalSkinVertex>& OutVertices, int32 InLODIndex)
{
	if (USkinnedMeshComponent* MasterPoseComponentPtr = MasterPoseComponent.Get())
	{
		MasterPoseComponentPtr->SetForcedLOD(InLODIndex + 1);
		MasterPoseComponentPtr->UpdateLODStatus();
		MasterPoseComponentPtr->RefreshBoneTransforms(nullptr);
	}
	else
	{
		SetForcedLOD(InLODIndex + 1);
		UpdateLODStatus();
		RefreshBoneTransforms(nullptr);
	}

	// switch to CPU skinning
	const bool bCachedCPUSkinning = GetCPUSkinningEnabled();
	constexpr bool bRecreateRenderStateImmediately = true;
	SetCPUSkinningEnabled(true, bRecreateRenderStateImmediately);

	check(MeshObject);
	check(MeshObject->IsCPUSkinned());
		
	// Copy our vertices out. We know we are using CPU skinning now, so this cast is safe
	OutVertices = static_cast<FSkeletalMeshObjectCPUSkin*>(MeshObject)->GetCachedFinalVertices();
	
	// switch skinning mode, LOD etc. back
	SetForcedLOD(0);
	SetCPUSkinningEnabled(bCachedCPUSkinning, bRecreateRenderStateImmediately);
}

void USkinnedMeshComponent::ReleaseResources()
{
	for (int32 LODIndex = 0; LODIndex < LODInfo.Num(); LODIndex++)
	{
		LODInfo[LODIndex].BeginReleaseOverrideVertexColors();
		LODInfo[LODIndex].BeginReleaseOverrideSkinWeights();
	}

	DetachFence.BeginFence();
}

void USkinnedMeshComponent::RegisterLODStreamingCallback(FLODStreamingCallback&& Callback, int32 LODIdx, float TimeoutSecs, bool bOnStreamIn)
{
	if (SkeletalMesh)
	{
		SkeletalMesh->RegisterMipLevelChangeCallback(this, LODIdx, TimeoutSecs, bOnStreamIn, MoveTemp(Callback));
	}
}

void USkinnedMeshComponent::BeginDestroy()
{
	if (SkeletalMesh)
	{
		SkeletalMesh->RemoveMipLevelChangeCallback(this);
	}

	Super::BeginDestroy();
	ReleaseResources();

	if (bSkinWeightProfilePending)
	{
		bSkinWeightProfilePending = false;
		if (FSkinWeightProfileManager * Manager = FSkinWeightProfileManager::Get(GetWorld()))
		{
			Manager->CancelSkinWeightProfileRequest(this);
		}
	}

	// Release ref pose override if allocated
	if (RefPoseOverride)
	{
		delete RefPoseOverride;
		RefPoseOverride = nullptr;
	}

	// Disconnect slave components from this component if present.
	// They will currently have no transforms allocated so will be
	// in an invalid state when this component is destroyed
	// Walk backwards as we'll be removing from this array
	const int32 NumSlaveComponents = SlavePoseComponents.Num();
	for(int32 SlaveIndex = NumSlaveComponents - 1; SlaveIndex >= 0; --SlaveIndex)
	{
		if(USkinnedMeshComponent* Slave = SlavePoseComponents[SlaveIndex].Get())
		{
			Slave->SetMasterPoseComponent(nullptr);
		}
	}
}

//////////////////////////////////////////////////////////////////////////

FSkelMeshComponentLODInfo::FSkelMeshComponentLODInfo()
: OverrideVertexColors(nullptr)
, OverrideSkinWeights(nullptr)
, OverrideProfileSkinWeights(nullptr)
{

}

FSkelMeshComponentLODInfo::~FSkelMeshComponentLODInfo()
{
	CleanUpOverrideVertexColors();
	CleanUpOverrideSkinWeights();
}

void FSkelMeshComponentLODInfo::ReleaseOverrideVertexColorsAndBlock()
{
	if (OverrideVertexColors)
	{
		// enqueue a rendering command to release
		BeginReleaseResource(OverrideVertexColors);
		// Ensure the RT no longer accessed the data, might slow down
		FlushRenderingCommands();
		// The RT thread has no access to it any more so it's safe to delete it.
		CleanUpOverrideVertexColors();
	}
}

void FSkelMeshComponentLODInfo::BeginReleaseOverrideVertexColors()
{
	if (OverrideVertexColors)
	{
		// enqueue a rendering command to release
		BeginReleaseResource(OverrideVertexColors);
	}
}

void FSkelMeshComponentLODInfo::CleanUpOverrideVertexColors()
{
	if (OverrideVertexColors)
	{
		delete OverrideVertexColors;
		OverrideVertexColors = nullptr;
	}
}

void FSkelMeshComponentLODInfo::ReleaseOverrideSkinWeightsAndBlock()
{
	if (OverrideSkinWeights)
	{
		// enqueue a rendering command to release
		OverrideSkinWeights->BeginReleaseResources();
		// Ensure the RT no longer accessed the data, might slow down
		FlushRenderingCommands();
		// The RT thread has no access to it any more so it's safe to delete it.
		CleanUpOverrideSkinWeights();
	}
}

void FSkelMeshComponentLODInfo::BeginReleaseOverrideSkinWeights()
{
	if (OverrideSkinWeights)
	{
		// enqueue a rendering command to release
		OverrideSkinWeights->BeginReleaseResources();
	}
}

void FSkelMeshComponentLODInfo::CleanUpOverrideSkinWeights()
{
	if (OverrideSkinWeights)
	{
		delete OverrideSkinWeights;
		OverrideSkinWeights = nullptr;
	}

	if (OverrideProfileSkinWeights)
	{
		OverrideProfileSkinWeights = nullptr;
	}
}

//////////////////////////////////////////////////////////////////////////

void USkinnedMeshComponent::SetVertexColorOverride_LinearColor(int32 LODIndex, const TArray<FLinearColor>& VertexColors)
{
	TArray<FColor> Colors;
	if (VertexColors.Num() > 0)
	{
		Colors.SetNum(VertexColors.Num());

		for (int32 ColorIdx = 0; ColorIdx < VertexColors.Num(); ColorIdx++)
		{
			Colors[ColorIdx] = VertexColors[ColorIdx].ToFColor(false);
		}
	}
	SetVertexColorOverride(LODIndex, Colors);
}


void USkinnedMeshComponent::SetVertexColorOverride(int32 LODIndex, const TArray<FColor>& VertexColors)
{
	InitLODInfos();

	FSkeletalMeshRenderData* SkelMeshRenderData = GetSkeletalMeshRenderData();

	// If we have a render resource, and the requested LODIndex is valid (for both component and mesh, though these should be the same)
	if (SkelMeshRenderData != nullptr && LODInfo.IsValidIndex(LODIndex) && SkelMeshRenderData->LODRenderData.IsValidIndex(LODIndex))
	{
		ensure(LODInfo.Num() == SkelMeshRenderData->LODRenderData.Num());

		FSkelMeshComponentLODInfo& Info = LODInfo[LODIndex];
		if (Info.OverrideVertexColors != nullptr)
		{
			Info.ReleaseOverrideVertexColorsAndBlock();
		}

		const TArray<FColor>* UseColors;
		TArray<FColor> ResizedColors;

		FSkeletalMeshLODRenderData& LODData = SkelMeshRenderData->LODRenderData[LODIndex];
		const int32 ExpectedNumVerts = LODData.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices();

		// If colors passed in are correct size, just use them
		if (VertexColors.Num() == ExpectedNumVerts)
		{
			UseColors = &VertexColors;
		}
		// If not the correct size, resize to correct size
		else
		{
			// presize array
			ResizedColors.AddUninitialized(ExpectedNumVerts);

			// Copy while input and output are valid
			int32 VertCount = 0;
			while (VertCount < ExpectedNumVerts)
			{
				if (VertCount < VertexColors.Num())
				{
					ResizedColors[VertCount] = VertexColors[VertCount];
				}
				else
				{
					ResizedColors[VertCount] = FColor::White;
				}

				VertCount++;
			}

			UseColors = &ResizedColors;
		}

		Info.OverrideVertexColors = new FColorVertexBuffer;
		Info.OverrideVertexColors->InitFromColorArray(*UseColors);

		BeginInitResource(Info.OverrideVertexColors);

		MarkRenderStateDirty();
	}
}

void USkinnedMeshComponent::ClearVertexColorOverride(int32 LODIndex)
{
	// If we have a render resource, and the requested LODIndex is valid (for both component and mesh, though these should be the same)
	if (LODInfo.IsValidIndex(LODIndex))
	{
		FSkelMeshComponentLODInfo& Info = LODInfo[LODIndex];
		if (Info.OverrideVertexColors != nullptr)
		{
			Info.ReleaseOverrideVertexColorsAndBlock();
			MarkRenderStateDirty();
		}
	}
}

/** 
 *	Util for converting from API skin weight description to GPU format. 
 * 	This includes remapping from skeleton bone index to section bone index.
 */
void CreateSectionSkinWeightsArray(
	const TArray<FSkelMeshSkinWeightInfo>& InSourceWeights,
	int32 StartIndex,
	int32 NumVerts,
	const TMap<int32, int32>& SkelToSectionBoneMap,
	TArray<FSkinWeightInfo>& OutGPUWeights,
	TArray<int32>& OutInvalidBones)
{
	OutGPUWeights.AddUninitialized(NumVerts);

	TArray<int32> InvalidBones;

	bool bWeightUnderrun = false;
	// Iterate over new output buffer
	for(int VertIndex = StartIndex; VertIndex < StartIndex + NumVerts; VertIndex++)
	{
		FSkinWeightInfo& TargetWeight = OutGPUWeights[VertIndex];
		// while we have valid entries in input buffer
		if (VertIndex < InSourceWeights.Num())
		{
			const FSkelMeshSkinWeightInfo& SrcWeight = InSourceWeights[VertIndex];

			// Iterate over influences
			for (int32 InfIndex = 0; InfIndex < MAX_TOTAL_INFLUENCES; InfIndex++)
			{
				// init to zero
				TargetWeight.InfluenceBones[InfIndex] = 0;
				TargetWeight.InfluenceWeights[InfIndex] = 0;

				// if we have a valid weight, see if we have a valid bone mapping for desired bone
				uint8 InfWeight = SrcWeight.Weights[InfIndex];
				if (InfWeight > 0)
				{
					const int32 SkelBoneIndex = SrcWeight.Bones[InfIndex];
					const int32* SectionBoneIndexPtr = SkelToSectionBoneMap.Find(SkelBoneIndex);

					// We do, use remapped value and copy weight
					if (SectionBoneIndexPtr)
					{
						TargetWeight.InfluenceBones[InfIndex] = *SectionBoneIndexPtr;
						TargetWeight.InfluenceWeights[InfIndex] = InfWeight;
					}
					// We don't, we'll warn, and leave zeros (this will mess up mesh, but not clear how to resolve this...)
					else
					{
						OutInvalidBones.AddUnique(SkelBoneIndex);
					}
				}
			}
		}
		// Oops, 
		else
		{
			bWeightUnderrun = true;

			TargetWeight.InfluenceBones[0] = 0;
			TargetWeight.InfluenceWeights[0] = 255;

			for (int32 InfIndex = 1; InfIndex < MAX_TOTAL_INFLUENCES; InfIndex++)
			{
				TargetWeight.InfluenceBones[InfIndex] = 0;
				TargetWeight.InfluenceWeights[InfIndex] = 0;
			}
		}
	}

	if (bWeightUnderrun)
	{
		UE_LOG(LogSkinnedMeshComp, Warning, TEXT("SetSkinWeightOverride: Too few weights specified."));
	}
}

void CreateSkinWeightsArray(
	const TArray<FSkelMeshSkinWeightInfo>& InSourceWeights,
	FSkeletalMeshLODRenderData& LODData,
	TArray<FSkinWeightInfo>& OutGPUWeights,
	const FReferenceSkeleton& RefSkel)
{
	// Index of first vertex in current section, in the big overall buffer
	int32 BaseVertIndex = 0;
	for (int32 SectionIdx = 0; SectionIdx < LODData.RenderSections.Num(); SectionIdx++)
	{
		const FSkelMeshRenderSection& Section = LODData.RenderSections[SectionIdx];
		const int32 NumVertsInSection = Section.NumVertices;

		// Build inverse mapping from skeleton bone index to section vertex index
		TMap<int32, int32> SkelToSectionBoneMap;
		for (int32 i = 0; i < Section.BoneMap.Num(); i++)
		{
			SkelToSectionBoneMap.Add(Section.BoneMap[i], i);
		}

		// Convert skin weight struct format and assign to new vertex buffer
		TArray<int32> InvalidBones;
		CreateSectionSkinWeightsArray(InSourceWeights, BaseVertIndex, NumVertsInSection, SkelToSectionBoneMap, OutGPUWeights, InvalidBones);

		// Log info for invalid bones
		if (InvalidBones.Num() > 0)
		{
			UE_LOG(LogSkinnedMeshComp, Warning, TEXT("SetSkinWeightOverride: Invalid bones index specified for section %d:"), SectionIdx);

			for (int32 BoneIndex : InvalidBones)
			{
				FName BoneName = RefSkel.GetBoneName(BoneIndex);
				UE_LOG(LogSkinnedMeshComp, Warning, TEXT("SetSkinWeightOverride: %d %s"), BoneIndex, *BoneName.ToString());
			}
		}

		BaseVertIndex += NumVertsInSection;
	}
}


void USkinnedMeshComponent::SetSkinWeightOverride(int32 LODIndex, const TArray<FSkelMeshSkinWeightInfo>& SkinWeights)
{
	InitLODInfos();

	FSkeletalMeshRenderData* SkelMeshRenderData = GetSkeletalMeshRenderData();

	// If we have a render resource, and the requested LODIndex is valid (for both component and mesh, though these should be the same)
	if (SkelMeshRenderData != nullptr && LODInfo.IsValidIndex(LODIndex) && SkelMeshRenderData->LODRenderData.IsValidIndex(LODIndex))
	{
		ensure(LODInfo.Num() == SkelMeshRenderData->LODRenderData.Num());

		FSkelMeshComponentLODInfo& Info = LODInfo[LODIndex];
		if (Info.OverrideSkinWeights != nullptr)
		{
			Info.ReleaseOverrideSkinWeightsAndBlock();
		}

		FSkeletalMeshLODRenderData& LODData = SkelMeshRenderData->LODRenderData[LODIndex];
		const int32 ExpectedNumVerts = LODData.GetNumVertices();

		// Only proceed if we have enough weights (we can proceed if we have too many)
		if (SkinWeights.Num() >= ExpectedNumVerts)
		{
			if (SkinWeights.Num() > ExpectedNumVerts)
			{
				UE_LOG(LogSkinnedMeshComp, Warning, TEXT("SetSkinWeightOverride: Too many weights - expected %d, got %d - truncating"), ExpectedNumVerts, SkinWeights.Num());
			}

			uint32 NumBoneInfluences = LODData.GetVertexBufferMaxBoneInfluences();
			bool bUse16BitBoneIndex = LODData.DoesVertexBufferUse16BitBoneIndex();

			// Allocate skin weight override buffer
			Info.OverrideSkinWeights = new FSkinWeightVertexBuffer;
			Info.OverrideSkinWeights->SetNeedsCPUAccess(true);
			Info.OverrideSkinWeights->SetMaxBoneInfluences(NumBoneInfluences);
			Info.OverrideSkinWeights->SetUse16BitBoneIndex(bUse16BitBoneIndex);

			const FReferenceSkeleton& RefSkel = SkeletalMesh->RefSkeleton;
			TArray<FSkinWeightInfo> GPUWeights;
			CreateSkinWeightsArray(SkinWeights, LODData, GPUWeights, RefSkel);
			*(Info.OverrideSkinWeights) = GPUWeights;
			Info.OverrideSkinWeights->BeginInitResources();

			MarkRenderStateDirty();
		}
		else
		{
			UE_LOG(LogSkinnedMeshComp, Warning, TEXT("SetSkinWeightOverride: Not enough weights - expected %d, got %d - aborting."), ExpectedNumVerts, SkinWeights.Num());
		}
	}
}

void USkinnedMeshComponent::ClearSkinWeightOverride(int32 LODIndex)
{
	SCOPED_NAMED_EVENT(USkinnedMeshComponent_ClearSkinWeightOverride, FColor::Yellow);

	// If we have a render resource, and the requested LODIndex is valid (for both component and mesh, though these should be the same)
	if (LODInfo.IsValidIndex(LODIndex))
	{
		FSkelMeshComponentLODInfo& Info = LODInfo[LODIndex];
		if (Info.OverrideSkinWeights != nullptr)
		{
			Info.ReleaseOverrideSkinWeightsAndBlock();
			MarkRenderStateDirty();
		}
	}
}

bool USkinnedMeshComponent::SetSkinWeightProfile(FName InProfileName)
{
	bool bContainsProfile = false;

	if (FSkeletalMeshRenderData* SkelMeshRenderData = GetSkeletalMeshRenderData())
	{
		// Ensure the LOD infos array is initialized
		InitLODInfos();
		for (int32 LODIndex = 0; LODIndex < LODInfo.Num(); ++LODIndex)
        {
			// Check whether or not setting a profile is allow for this LOD index
			if (LODIndex > GSkinWeightProfilesAllowedFromLOD)
			{
				FSkeletalMeshLODRenderData& RenderData = SkelMeshRenderData->LODRenderData[LODIndex];

				bContainsProfile |= RenderData.SkinWeightProfilesData.ContainsProfile(InProfileName);

				// Retrieve this profile's skin weight buffer
				FSkinWeightVertexBuffer* Buffer = RenderData.SkinWeightProfilesData.GetOverrideBuffer(InProfileName);
        
				FSkelMeshComponentLODInfo& Info = LODInfo[LODIndex];
				Info.OverrideProfileSkinWeights = Buffer;
                
				if (Buffer != nullptr)
				{
					bSkinWeightProfileSet = true;
				}
			}
        }

		if (bContainsProfile)
		{
			CurrentSkinWeightProfileName = InProfileName;

			if (bSkinWeightProfileSet)
			{
				UpdateSkinWeightOverrideBuffer();
			}
			else 
			{
				TWeakObjectPtr<USkinnedMeshComponent> WeakComponent = this;
				FRequestFinished Callback = [WeakComponent](TWeakObjectPtr<USkeletalMesh> WeakMesh, FName ProfileName)
				{
					// Ensure that the request objects are still valid
					if (WeakMesh.IsValid() && WeakComponent.IsValid())
					{
						USkinnedMeshComponent* Component = WeakComponent.Get();
						Component->InitLODInfos();

						Component->bSkinWeightProfilePending = false;
						Component->bSkinWeightProfileSet = true;

						if (FSkeletalMeshRenderData * RenderData = WeakMesh->GetResourceForRendering())
						{
							const int32 NumLODs = RenderData->LODRenderData.Num();
							for (int32 Index = 0; Index < NumLODs; ++Index)
							{
								FSkeletalMeshLODRenderData& LODRenderData = RenderData->LODRenderData[Index];
								FSkinWeightProfilesData& SkinweightData = LODRenderData.SkinWeightProfilesData;

								// Check whether or not setting a profile is allow for this LOD index
								if (Index > GSkinWeightProfilesAllowedFromLOD)
								{
									// Retrieve this profile's skin weight buffer
									FSkinWeightVertexBuffer* Buffer = SkinweightData.GetOverrideBuffer(ProfileName);
									FSkelMeshComponentLODInfo& Info = Component->LODInfo[Index];
									Info.OverrideProfileSkinWeights = Buffer;
								}
							}

							Component->UpdateSkinWeightOverrideBuffer();
						}
					}
				};

				// Put in a skin weight profile request
				if (FSkinWeightProfileManager* Manager = FSkinWeightProfileManager::Get(GetWorld()))
				{
					Manager->RequestSkinWeightProfile(InProfileName, SkeletalMesh, this, Callback);
					bSkinWeightProfilePending = true;
				}
			}
		}
	}

	return bContainsProfile;
}

void USkinnedMeshComponent::ClearSkinWeightProfile()
{
	if (FSkeletalMeshRenderData* SkelMeshRenderData = GetSkeletalMeshRenderData())
	{	
		bool bCleared = false;

		if (bSkinWeightProfileSet)
		{
			InitLODInfos();
			// Clear skin weight buffer set for all of the LODs
			for (int32 LODIndex = 0; LODIndex < LODInfo.Num(); ++LODIndex)
			{
				FSkelMeshComponentLODInfo& Info = LODInfo[LODIndex];
				bCleared |= (Info.OverrideProfileSkinWeights != nullptr);
				Info.OverrideProfileSkinWeights = nullptr;
			}

			if (bCleared)
			{
				UpdateSkinWeightOverrideBuffer();
			}
		}

		if (bSkinWeightProfilePending)
		{
			if (FSkinWeightProfileManager * Manager = FSkinWeightProfileManager::Get(GetWorld()))
			{
				Manager->CancelSkinWeightProfileRequest(this);
			}
		}
	}

	bSkinWeightProfilePending = false;
	bSkinWeightProfileSet = false;
	CurrentSkinWeightProfileName = NAME_None;
}

void USkinnedMeshComponent::UnloadSkinWeightProfile(FName InProfileName)
{
	if (FSkeletalMeshRenderData* SkelMeshRenderData = GetSkeletalMeshRenderData())
	{
		if (LODInfo.Num())
		{
			bool bCleared = false;
			for (int32 LODIndex = 0; LODIndex < LODInfo.Num(); ++LODIndex)
			{
				// Queue release and deletion of the skin weight buffer associated with the profile name
				FSkeletalMeshLODRenderData& RenderData = SkelMeshRenderData->LODRenderData[LODIndex];
				RenderData.SkinWeightProfilesData.ReleaseBuffer(InProfileName);

				// In case the buffer previously released is currently set for this component, clear it
				if (CurrentSkinWeightProfileName == InProfileName)
				{
					FSkelMeshComponentLODInfo& Info = LODInfo[LODIndex];
					Info.OverrideProfileSkinWeights = nullptr;
					bCleared = true;
				}
			}

			if (bCleared)
			{
				UpdateSkinWeightOverrideBuffer();
			}
		}

		if (bSkinWeightProfilePending)
		{
			if (FSkinWeightProfileManager * Manager = FSkinWeightProfileManager::Get(GetWorld()))
			{
				Manager->CancelSkinWeightProfileRequest(this);
			}

			bSkinWeightProfilePending = false;
		}
	}

	if (CurrentSkinWeightProfileName == InProfileName)
	{
		bSkinWeightProfileSet = false;
		CurrentSkinWeightProfileName = NAME_None;
	}
}

void USkinnedMeshComponent::UpdateSkinWeightOverrideBuffer()
{
	// Force a mesh update to ensure bone buffers are up to date
	bForceMeshObjectUpdate = true;
	MarkRenderDynamicDataDirty();

	// Queue an update of the skin weight buffer used by the current Mesh Object
	if (MeshObject)
	{
		MeshObject->UpdateSkinWeightBuffer(this);
	}
}

void USkinnedMeshComponent::ReleaseUpdateRateParams()
{
	FAnimUpdateRateManager::CleanupUpdateRateParametersRef(this);
	AnimUpdateRateParams = nullptr;
}

void USkinnedMeshComponent::RefreshUpdateRateParams()
{
	if (AnimUpdateRateParams)
	{
		ReleaseUpdateRateParams();
	}

	AnimUpdateRateParams = FAnimUpdateRateManager::GetUpdateRateParameters(this);
}

void USkinnedMeshComponent::SetRenderStatic(bool bNewValue)
{
	if (bRenderStatic != bNewValue)
	{
		bRenderStatic = bNewValue;
		MarkRenderStateDirty();
	}
}

int32 USkinnedMeshComponent::GetVertexOffsetUsage(int32 LODIndex) const
{
	if (LODInfo.IsValidIndex(LODIndex))
	{
		return (LODIndex < VertexOffsetUsage.Num()) ? VertexOffsetUsage[LODIndex].Usage : 0;
	}

	return 0;
}

void USkinnedMeshComponent::SetVertexOffsetUsage(int32 LODIndex, int32 Usage)
{
	InitLODInfos();

	if (LODInfo.IsValidIndex(LODIndex))
	{
		if (LODIndex >= VertexOffsetUsage.Num())
		{
			VertexOffsetUsage.SetNumZeroed(LODIndex+1);
		}

		VertexOffsetUsage[LODIndex].Usage = Usage;

		if ((Usage & int32(EVertexOffsetUsageType::PreSkinningOffset)) == 0)
		{
			LODInfo[LODIndex].PreSkinningOffsets.Empty();
		}

		if ((Usage & int32(EVertexOffsetUsageType::PostSkinningOffset)) == 0)
		{
			LODInfo[LODIndex].PostSkinningOffsets.Empty();
		}

		MarkRenderStateDirty();
	}
}

void USkinnedMeshComponent::SetPreSkinningOffsets(int32 LODIndex, TArray<FVector> Offsets)
{
	InitLODInfos();

	FSkeletalMeshRenderData* SkelMeshRenderData = GetSkeletalMeshRenderData();

	// If we have a render resource, and the requested LODIndex is valid (for both component and mesh, though these should be the same)
	if (SkelMeshRenderData && LODInfo.IsValidIndex(LODIndex) && SkelMeshRenderData->LODRenderData.IsValidIndex(LODIndex))
	{
		ensure(LODInfo.Num() == SkelMeshRenderData->LODRenderData.Num());

		FSkelMeshComponentLODInfo& Info = LODInfo[LODIndex];
		FSkeletalMeshLODRenderData& LODData = SkelMeshRenderData->LODRenderData[LODIndex];

		uint32 VertexCount = LODData.GetNumVertices();
		Offsets.SetNumZeroed(VertexCount);

		Info.PreSkinningOffsets = MoveTemp(Offsets);

		MarkRenderDynamicDataDirty();
	}
}

void USkinnedMeshComponent::SetPostSkinningOffsets(int32 LODIndex, TArray<FVector> Offsets)
{
	InitLODInfos();

	FSkeletalMeshRenderData* SkelMeshRenderData = GetSkeletalMeshRenderData();

	// If we have a render resource, and the requested LODIndex is valid (for both component and mesh, though these should be the same)
	if (SkelMeshRenderData && LODInfo.IsValidIndex(LODIndex) && SkelMeshRenderData->LODRenderData.IsValidIndex(LODIndex))
	{
		ensure(LODInfo.Num() == SkelMeshRenderData->LODRenderData.Num());

		FSkelMeshComponentLODInfo& Info = LODInfo[LODIndex];
		FSkeletalMeshLODRenderData& LODData = SkelMeshRenderData->LODRenderData[LODIndex];

		uint32 VertexCount = LODData.GetNumVertices();
		Offsets.SetNumZeroed(VertexCount);

		Info.PostSkinningOffsets = MoveTemp(Offsets);

		MarkRenderDynamicDataDirty();
	}
}

#if WITH_EDITOR
void USkinnedMeshComponent::BindWorldDelegates()
{
	FWorldDelegates::OnPostWorldCreation.AddStatic(&HandlePostWorldCreation);
}

void USkinnedMeshComponent::HandlePostWorldCreation(UWorld* InWorld)
{
	TWeakObjectPtr<UWorld> WeakWorld = InWorld;
	InWorld->AddOnFeatureLevelChangedHandler(FOnFeatureLevelChanged::FDelegate::CreateStatic(&HandleFeatureLevelChanged, WeakWorld));
}

void USkinnedMeshComponent::HandleFeatureLevelChanged(ERHIFeatureLevel::Type InFeatureLevel, TWeakObjectPtr<UWorld> InWorld)
{
	if(UWorld* World = InWorld.Get())
	{
		for(TObjectIterator<USkinnedMeshComponent> It; It; ++It)
		{
			if(USkinnedMeshComponent* Component = *It)
			{
				if(Component->GetWorld() == World)
				{
					Component->CachedSceneFeatureLevel = InFeatureLevel;
				}
			}
		}
	}
}
#endif

void FAnimUpdateRateParameters::SetTrailMode(float DeltaTime, uint8 UpdateRateShift, int32 NewUpdateRate, int32 NewEvaluationRate, bool bNewInterpSkippedFrames)
{
	OptimizeMode = TrailMode;
	ThisTickDelta = DeltaTime;

	UpdateRate = FMath::Max(NewUpdateRate, 1);

	// Make sure EvaluationRate is a multiple of UpdateRate.
	EvaluationRate = FMath::Max((NewEvaluationRate / UpdateRate) * UpdateRate, 1);
	bInterpolateSkippedFrames = (FAnimUpdateRateManager::CVarURODisableInterpolation.GetValueOnAnyThread() == 0) &&
		((bNewInterpSkippedFrames && (EvaluationRate < MaxEvalRateForInterpolation)) || (FAnimUpdateRateManager::CVarForceInterpolation.GetValueOnAnyThread() == 1));

	// Make sure we don't overflow. we don't need very large numbers.
	const uint32 Counter = (GFrameCounter + UpdateRateShift) % MAX_uint32;

	bSkipUpdate = ((Counter % UpdateRate) > 0);
	bSkipEvaluation = ((Counter % EvaluationRate) > 0);

	// As UpdateRate changes, because of LODs for example,
	// make sure we're not caught in a loop where we don't update longer than our update rate.
	{
		SkippedUpdateFrames = bSkipUpdate ? ++SkippedUpdateFrames : 0;
		SkippedEvalFrames = bSkipEvaluation ? ++SkippedEvalFrames : 0;

		// If we've gone longer that our UpdateRate, force an update to happen.
		if ((SkippedUpdateFrames >= UpdateRate) || (SkippedEvalFrames >= EvaluationRate))
		{
			bSkipUpdate = false;
			bSkipEvaluation = false;
			SkippedUpdateFrames = 0;
			SkippedEvalFrames = 0;
		}
	}

	// We should never trigger an Eval without an Update.
	check((bSkipEvaluation && bSkipUpdate) || (bSkipEvaluation && !bSkipUpdate) || (!bSkipEvaluation && !bSkipUpdate));

	AdditionalTime = 0.f;

	if (bSkipUpdate)
	{
		TickedPoseOffestTime -= DeltaTime;
	}
	else
	{
		if (TickedPoseOffestTime < 0.f)
		{
			AdditionalTime = -TickedPoseOffestTime;
			TickedPoseOffestTime = 0.f;
		}
	}
}

void FAnimUpdateRateParameters::SetLookAheadMode(float DeltaTime, uint8 UpdateRateShift, float LookAheadAmount)
{
	float OriginalTickedPoseOffestTime = TickedPoseOffestTime;
	if (OptimizeMode == TrailMode)
	{
		TickedPoseOffestTime = 0.f;
	}
	OptimizeMode = LookAheadMode;
	ThisTickDelta = DeltaTime;

	bInterpolateSkippedFrames = true;

	TickedPoseOffestTime -= DeltaTime;

	if (TickedPoseOffestTime < 0.f)
	{
		LookAheadAmount = FMath::Max(TickedPoseOffestTime*-1.f, LookAheadAmount);
		AdditionalTime = LookAheadAmount;
		TickedPoseOffestTime += LookAheadAmount;

		bool bValid = (TickedPoseOffestTime >= 0.f);
		if (!bValid)
		{
			FPlatformMisc::LowLevelOutputDebugStringf(TEXT("TPO Time: %.3f | Orig TPO Time: %.3f | DT: %.3f | LookAheadAmount: %.3f\n"), TickedPoseOffestTime, OriginalTickedPoseOffestTime, DeltaTime, LookAheadAmount);
		}
		check(bValid);
		bSkipUpdate = bSkipEvaluation = false;
	}
	else
	{
		AdditionalTime = 0.f;
		bSkipUpdate = bSkipEvaluation = true;
	}
}

float FAnimUpdateRateParameters::GetInterpolationAlpha() const
{
	if (OptimizeMode == TrailMode)
	{
		return 0.25f + (1.f / float(FMath::Max(EvaluationRate, 2) * 2));
	}
	else if (OptimizeMode == LookAheadMode)
	{
		return FMath::Clamp(ThisTickDelta / (TickedPoseOffestTime + ThisTickDelta), 0.f, 1.f);
	}
	check(false); // Unknown mode
	return 0.f;
}

float FAnimUpdateRateParameters::GetRootMotionInterp() const
{
	if (OptimizeMode == LookAheadMode)
	{
		return FMath::Clamp(ThisTickDelta / (TickedPoseOffestTime + ThisTickDelta), 0.f, 1.f);
	}
	return 1.f;
}

/** Simple, CPU evaluation of a vertex's skinned position helper function */
void GetTypedSkinnedTangentBasis(
	const USkinnedMeshComponent* SkinnedComp,
	const FSkelMeshRenderSection& Section,
	const FStaticMeshVertexBuffers& StaticVertexBuffers,
	const FSkinWeightVertexBuffer& SkinWeightVertexBuffer,
	const int32 VertIndex,
	const TArray<FMatrix> & RefToLocals,
	FVector& OutTangentX,
	FVector& OutTangentZ
)
{
	OutTangentX = FVector::ZeroVector;
	OutTangentZ = FVector::ZeroVector;

	const USkinnedMeshComponent* const MasterPoseComponentInst = SkinnedComp->MasterPoseComponent.Get();
	const USkinnedMeshComponent* BaseComponent = MasterPoseComponentInst ? MasterPoseComponentInst : SkinnedComp;

	// Do soft skinning for this vertex.
	const int32 BufferVertIndex = Section.GetVertexBufferIndex() + VertIndex;
	const int32 MaxBoneInfluences = SkinWeightVertexBuffer.GetMaxBoneInfluences();

	const FVector VertexTangentX = StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentX(BufferVertIndex);
	const FVector VertexTangentZ = StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(BufferVertIndex);

#if !PLATFORM_LITTLE_ENDIAN
	// uint8[] elements in LOD.VertexBufferGPUSkin have been swapped for VET_UBYTE4 vertex stream use
	for (int32 InfluenceIndex = MAX_INFLUENCES - 1; InfluenceIndex >= MAX_INFLUENCES - MaxBoneInfluences; InfluenceIndex--)
#else
	for (int32 InfluenceIndex = 0; InfluenceIndex < MaxBoneInfluences; InfluenceIndex++)
#endif
	{
		const int32 MeshBoneIndex = Section.BoneMap[SkinWeightVertexBuffer.GetBoneIndex(BufferVertIndex, InfluenceIndex)];
		const float	Weight = (float)SkinWeightVertexBuffer.GetBoneWeight(BufferVertIndex, InfluenceIndex) / 255.0f;
		const FMatrix& RefToLocal = RefToLocals[MeshBoneIndex];
		OutTangentX += RefToLocal.TransformVector(VertexTangentX) * Weight;
		OutTangentZ += RefToLocal.TransformVector(VertexTangentZ) * Weight;
	}
}

/** Simple, CPU evaluation of a vertex's skinned position helper function */
template <bool bCachedMatrices>
FVector GetTypedSkinnedVertexPosition(
	const USkinnedMeshComponent* SkinnedComp,
	const FSkelMeshRenderSection& Section,
	const FPositionVertexBuffer& PositionVertexBuffer,
	const FSkinWeightVertexBuffer& SkinWeightVertexBuffer,
	const int32 VertIndex,
	const TArray<FMatrix> & RefToLocals
)
{
	FVector SkinnedPos(0, 0, 0);

	const USkinnedMeshComponent* const MasterPoseComponentInst = SkinnedComp->MasterPoseComponent.Get();
	const USkinnedMeshComponent* BaseComponent = MasterPoseComponentInst ? MasterPoseComponentInst : SkinnedComp;

	// Do soft skinning for this vertex.
	int32 BufferVertIndex = Section.GetVertexBufferIndex() + VertIndex;
	int32 MaxBoneInfluences = SkinWeightVertexBuffer.GetMaxBoneInfluences();

#if !PLATFORM_LITTLE_ENDIAN
	// uint8[] elements in LOD.VertexBufferGPUSkin have been swapped for VET_UBYTE4 vertex stream use
	for (int32 InfluenceIndex = MAX_INFLUENCES - 1; InfluenceIndex >= MAX_INFLUENCES - MaxBoneInfluences; InfluenceIndex--)
#else
	for (int32 InfluenceIndex = 0; InfluenceIndex < MaxBoneInfluences; InfluenceIndex++)
#endif
	{
		const int32 MeshBoneIndex = Section.BoneMap[SkinWeightVertexBuffer.GetBoneIndex(BufferVertIndex, InfluenceIndex)];
		int32 TransformBoneIndex = MeshBoneIndex;

		if (MasterPoseComponentInst)
		{
			const TArray<int32>& MasterBoneMap = SkinnedComp->GetMasterBoneMap();
			check(MasterBoneMap.Num() == SkinnedComp->SkeletalMesh->RefSkeleton.GetNum());
			TransformBoneIndex = MasterBoneMap[MeshBoneIndex];
		}

		const float	Weight = (float)SkinWeightVertexBuffer.GetBoneWeight(BufferVertIndex, InfluenceIndex) / 255.0f;
		{
			if (bCachedMatrices)
			{
				const FMatrix& RefToLocal = RefToLocals[MeshBoneIndex];
				SkinnedPos += RefToLocal.TransformPosition(PositionVertexBuffer.VertexPosition(BufferVertIndex)) * Weight;
			}
			else
			{
				const FMatrix BoneTransformMatrix = (TransformBoneIndex != INDEX_NONE) ? BaseComponent->GetComponentSpaceTransforms()[TransformBoneIndex].ToMatrixWithScale() : FMatrix::Identity;
				const FMatrix RefToLocal = SkinnedComp->SkeletalMesh->RefBasesInvMatrix[MeshBoneIndex] * BoneTransformMatrix;
				SkinnedPos += RefToLocal.TransformPosition(PositionVertexBuffer.VertexPosition(BufferVertIndex)) * Weight;
			}
		}
	}

	return SkinnedPos;
}



template FVector GetTypedSkinnedVertexPosition<true>(const USkinnedMeshComponent* SkinnedComp, const FSkelMeshRenderSection& Section, const FPositionVertexBuffer& PositionVertexBuffer,
	const FSkinWeightVertexBuffer& SkinWeightVertexBuffer, const int32 VertIndex, const TArray<FMatrix> & RefToLocals);

template FVector GetTypedSkinnedVertexPosition<false>(const USkinnedMeshComponent* SkinnedComp, const FSkelMeshRenderSection& Section, const FPositionVertexBuffer& PositionVertexBuffer,
	const FSkinWeightVertexBuffer& SkinWeightVertexBuffer, const int32 VertIndex, const TArray<FMatrix> & RefToLocals);
