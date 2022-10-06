// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ClothComponent.h"
#include "ChaosClothAsset/ClothAsset.h"
#include "SkeletalRenderPublic.h"
#include "PhysicsEngine/PhysicsAsset.h"

DEFINE_LOG_CATEGORY_STATIC(LogChaosClothComponent, Log, All);

#include UE_INLINE_GENERATED_CPP_BY_NAME(ClothComponent)

DECLARE_STATS_GROUP(TEXT("ChaosClothComponent"), STATGROUP_ChaosClothComponent, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("SendRenderDynamicData_Concurrent"), STAT_ChaosClothComponent_SendRenderDynamicData_Concurrent, STATGROUP_ChaosClothComponent);
DECLARE_CYCLE_STAT(TEXT("MeshObjectUpdate"), STAT_ChaosClothComponent_MeshObjectUpdate, STATGROUP_ChaosClothComponent);
DECLARE_CYCLE_STAT(TEXT("CalcBounds"), STAT_ChaosClothComponent_CalcBounds, STATGROUP_ChaosClothComponent);

UChaosClothComponent::UChaosClothComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bAutoActivate = true;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;

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

void UChaosClothComponent::OnRegister()
{
	LLM_SCOPE(ELLMTag::Chaos);

	Super::OnRegister();

	if (GetClothAsset())
	{
		FSkeletalMeshLODRenderData& LODData = GetClothAsset()->GetResourceForRendering()->LODRenderData[GetPredictedLODLevel()];
		GetClothAsset()->FillComponentSpaceTransforms(GetClothAsset()->GetRefSkeleton().GetRefBonePose(), LODData.RequiredBones, GetEditableComponentSpaceTransforms());

		bNeedToFlipSpaceBaseBuffers = true; // Have updated space bases so need to flip
		FlipEditableSpaceBases();
		bHasValidBoneTransform = true;
	}
}

void UChaosClothComponent::RefreshBoneTransforms(FActorComponentTickFunction* TickFunction)
{
	// TODO: RefreshBoneTransforms
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


