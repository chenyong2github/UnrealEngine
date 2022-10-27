// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ClothComponent.h"
#include "ChaosClothAsset/ClothAsset.h"
#include "ChaosClothAsset/ClothAssetPrivate.h"
#include "ChaosClothAsset/ClothSimulationModel.h"
#include "ChaosClothAsset/ClothSimulationProxy.h"
#include "HAL/IConsoleManager.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SkeletalRenderPublic.h"

CSV_DECLARE_CATEGORY_MODULE_EXTERN(ENGINE_API, Animation);

UChaosClothComponent::UChaosClothComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bDisableClothSimulation(0)
	, bSuspendSimulation(0)
	, bWaitForParallelClothTask(0)
	, bBindClothToLeaderComponent(0)
{
	bAutoActivate = true;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	PrimaryComponentTick.EndTickGroup = TG_PostPhysics;

	//VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);

	StreamingDistanceMultiplier = 1.0f;
	//bCanHighlightSelectedSections = false;
	CanCharacterStepUpOn = ECB_Owner;
#if WITH_EDITORONLY_DATA
	//SectionIndexPreview = -1;
	//MaterialIndexPreview = -1;

	//SelectedEditorSection = INDEX_NONE;
	//SelectedEditorMaterial = INDEX_NONE;
#endif // WITH_EDITORONLY_DATA
	bCastCapsuleDirectShadow = false;
	bCastCapsuleIndirectShadow = false;
	CapsuleIndirectShadowMinVisibility = .1f;

	bDoubleBufferedComponentSpaceTransforms = true;
	//LastStreamerUpdateBoundsRadius = -1.0;
	CurrentEditableComponentTransforms = 0;
	CurrentReadComponentTransforms = 1;
	bNeedToFlipSpaceBaseBuffers = false;
	bBoneVisibilityDirty = false;

	//bUpdateDeformerAtNextTick = false;

	bCanEverAffectNavigation = false;
	//MasterBoneMapCacheCount = 0;
	bSyncAttachParentLOD = true;
	//bIgnoreMasterPoseComponentLOD = false;

	CurrentBoneTransformRevisionNumber = 0;

	//ExternalInterpolationAlpha = 0.0f;
	//ExternalDeltaTime = 0.0f;
	//ExternalTickRate = 1;
	//bExternalInterpolate = false;
	//bExternalUpdate = false;
	//bExternalEvaluationRateLimited = false;
	//bExternalTickRateControlled = false;

	bMipLevelCallbackRegistered = false;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	//bDrawDebugSkeleton = false;
#endif

	//CurrentSkinWeightProfileName = NAME_None;
}

UChaosClothComponent::UChaosClothComponent(FVTableHelper& Helper)
	: Super(Helper)
{
}

UChaosClothComponent::~UChaosClothComponent() = default;

void UChaosClothComponent::OnRegister()
{
	using namespace UE::Chaos::ClothAsset;

	LLM_SCOPE(ELLMTag::Chaos);

	Super::OnRegister();

	if (GetClothAsset())
	{
		const FChaosClothSimulationModel* const ClothSimulationModel = GetClothAsset()->GetClothSimulationModel();
		if (ensure(ClothSimulationModel) && ClothSimulationModel->GetNumLods())
		{
			FSkeletalMeshLODRenderData& LODData = GetClothAsset()->GetResourceForRendering()->LODRenderData[GetPredictedLODLevel()];
			GetClothAsset()->FillComponentSpaceTransforms(GetClothAsset()->GetRefSkeleton().GetRefBonePose(), LODData.RequiredBones, GetEditableComponentSpaceTransforms());

			bNeedToFlipSpaceBaseBuffers = true; // Have updated space bases so need to flip
			FlipEditableSpaceBases();
			bHasValidBoneTransform = true;

			// Create simulation proxy
			ClothSimulationProxy = MakeUnique<FClothSimulationProxy>(*this);
		}
	}
}

void UChaosClothComponent::OnUnregister()
{
	Super::OnUnregister();

	// Release cloth simulation
	ClothSimulationProxy.Reset(nullptr);
}

void UChaosClothComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Physics);

	// TODO: Fields
	//if (ClothingSimulation)
	//{
	//	ClothingSimulation->UpdateWorldForces(this);
	//}

	// TODO: Suspended update
	//// If we are suspended, we will not simulate clothing, but as clothing is simulated in local space
	//// relative to a root bone we need to extract simulation positions as this bone could be animated.
	//if ((!CVarEnableClothPhysics.GetValueOnGameThread() || bClothingSimulationSuspended) && ClothingSimulation)
	//{
	//	// First update the simulation context, since the simulation isn't ticking
	//	// and it is still required to get the correct simulation data and bounds.
	//	constexpr bool bIsInitialization = false;
	//	ClothingSimulation->FillContext(this, DeltaTime, ClothingSimulationContext, bIsInitialization);
	//	ClothingSimulation->GetSimulationData(CurrentSimulationData, this, Cast<USkeletalMeshComponent>(MasterPoseComponent.Get()));
	//}

	// Make sure that the previous frame simulation has completed
	HandleExistingParallelSimulation();

	// < This would be the right place to update the preset/use an interactor, ...etc.

	// Update the proxy and start the simulation parallel task
	StartNewParallelSimulation(DeltaTime);

	// TODO: Wait in tick function
	//if (ShouldWaitForClothInTickFunction())
	//{
	//	FGraphEventArray Prerequisites;
	//	Prerequisites.Add(ParallelClothTask);
	//	FGraphEventRef ClothCompletionEvent = TGraphTask<FParallelClothCompletionTask>::CreateTask(&Prerequisites, ENamedThreads::GameThread).ConstructAndDispatchWhenReady(this);
	//	ThisTickFunction.GetCompletionHandle()->DontCompleteUntil(ClothCompletionEvent);
	//}

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void UChaosClothComponent::RefreshBoneTransforms(FActorComponentTickFunction* TickFunction)
{
	MarkRenderDynamicDataDirty();

	bNeedToFlipSpaceBaseBuffers = true;
	bHasValidBoneTransform = false;
	FlipEditableSpaceBases();
	bHasValidBoneTransform = true;
}

void UChaosClothComponent::GetUpdateClothSimulationData_AnyThread(TMap<int32, FClothSimulData>& OutClothSimulData, FMatrix& OutLocalToWorld, float& OutClothBlendWeight)
{
	OutLocalToWorld = GetComponentToWorld().ToMatrixWithScale();

	const UChaosClothComponent* const LeaderPoseClothComponent = Cast<UChaosClothComponent>(LeaderPoseComponent.Get());
	if (LeaderPoseClothComponent && LeaderPoseClothComponent->ClothSimulationProxy && bBindClothToLeaderComponent)
	{
		OutClothBlendWeight = ClothBlendWeight;
		OutClothSimulData = LeaderPoseClothComponent->ClothSimulationProxy->GetCurrentSimulationData_AnyThread();
	}
	else if (!bDisableClothSimulation && !bBindClothToLeaderComponent)
	{
		OutClothBlendWeight = ClothBlendWeight;
		OutClothSimulData = ClothSimulationProxy->GetCurrentSimulationData_AnyThread();
	}
	else
	{
		OutClothSimulData.Reset();
	}

	// Blend cloth out whenever the simulation data is invalid
	if (!OutClothSimulData.Num())
	{
		OutClothBlendWeight = 0.0f;
	}
}

void UChaosClothComponent::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITORONLY_DATA
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	ClothAsset = GetClothAsset();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
#endif
}

void UChaosClothComponent::SetClothAsset(UChaosClothAsset* InClothAsset)
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
#if WITH_EDITORONLY_DATA
	ClothAsset = InClothAsset;
#endif
	SetSkinnedAsset(InClothAsset);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

UChaosClothAsset* UChaosClothComponent::GetClothAsset() const
{
	return Cast<UChaosClothAsset>(GetSkinnedAsset());
}

#if WITH_EDITOR
void UChaosClothComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// Set the skinned asset pointer with the alias pointer (must happen before the call to Super::PostEditChangeProperty)
	if (const FProperty* const Property = PropertyChangedEvent.Property)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UChaosClothComponent, ClothAsset))
		{
			SetSkinnedAsset(ClothAsset);
		}
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif // WITH_EDITOR

bool UChaosClothComponent::RequiresPreEndOfFrameSync() const
{
	if (!IsSimulationSuspended() && !ShouldWaitForClothInTickFunction())
	{
		// By default we await the cloth task in TickComponent, but...
		// If we have cloth and have no game-thread dependencies on the cloth output, 
		// then we will wait for the cloth task in SendAllEndOfFrameUpdates.
		return true;
	}
	return Super::RequiresPreEndOfFrameSync();
}

void UChaosClothComponent::OnPreEndOfFrameSync()
{
	Super::OnPreEndOfFrameSync();

	HandleExistingParallelSimulation();
}

FBoxSphereBounds UChaosClothComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	const IConsoleVariable* const CVarCacheLocalSpaceBounds = IConsoleManager::Get().FindConsoleVariable(TEXT("a.CacheLocalSpaceBounds"));
	const bool bCacheLocalSpaceBounds = CVarCacheLocalSpaceBounds ? CVarCacheLocalSpaceBounds->GetBool() : true;

	const FTransform CachedBoundsTransform = bCacheLocalSpaceBounds ? FTransform::Identity : LocalToWorld;

	FBoxSphereBounds NewBounds;
	if (ClothSimulationProxy)
	{
		NewBounds = ClothSimulationProxy->CalculateBounds_AnyThread().TransformBy(CachedBoundsTransform);
	}

	CachedWorldOrLocalSpaceBounds = NewBounds;
	bCachedLocalBoundsUpToDate = bCacheLocalSpaceBounds;
	bCachedWorldSpaceBoundsUpToDate = !bCacheLocalSpaceBounds;

	if (bCacheLocalSpaceBounds)
	{
		CachedWorldToLocalTransform.SetIdentity();
		return NewBounds.TransformBy(LocalToWorld);
	}
	CachedWorldToLocalTransform = LocalToWorld.ToInverseMatrixWithScale();
	return NewBounds;
}

void UChaosClothComponent::StartNewParallelSimulation(float DeltaTime)
{
	if (ClothSimulationProxy.IsValid())
	{
		CSV_SCOPED_TIMING_STAT(Animation, Cloth);
		ClothSimulationProxy->Tick_GameThread(DeltaTime);
	}
}

void UChaosClothComponent::HandleExistingParallelSimulation()
{
	if (bBindClothToLeaderComponent)
	{
		if (UChaosClothComponent* const LeaderComponent = Cast<UChaosClothComponent>(LeaderPoseComponent.Get()))
		{
			LeaderComponent->HandleExistingParallelSimulation();
		}
	}

	if (ClothSimulationProxy.IsValid())
	{
		ClothSimulationProxy->CompleteParallelSimulation_GameThread();
	}
}

bool UChaosClothComponent::ShouldWaitForClothInTickFunction() const
{
	static IConsoleVariable* const CVarClothPhysicsWaitForParallelClothTask = IConsoleManager::Get().FindConsoleVariable(TEXT("p.ClothPhysics.WaitForParallelClothTask"));

	return bWaitForParallelClothTask || (CVarClothPhysicsWaitForParallelClothTask && CVarClothPhysicsWaitForParallelClothTask->GetBool());
}

bool UChaosClothComponent::IsSimulationSuspended() const
{
	static IConsoleVariable* const CVarClothPhysics = IConsoleManager::Get().FindConsoleVariable(TEXT("p.ClothPhysics"));

	return bSuspendSimulation || !ClothSimulationProxy.IsValid() || (CVarClothPhysics && !CVarClothPhysics->GetBool());
}
