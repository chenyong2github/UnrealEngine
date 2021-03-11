// Copyright Epic Games, Inc. All Rights Reserved.

#include "ClothingSimulation.h"
#include "Components/SkeletalMeshComponent.h"
#include "PhysicsEngine/PhysicsSettings.h"

//==============================================================================
// FClothingSimulationContextCommon
//==============================================================================
DEFINE_STAT(STAT_ClothComputeNormals);
DEFINE_STAT(STAT_ClothInternalSolve);
DEFINE_STAT(STAT_ClothUpdateCollisions);
DEFINE_STAT(STAT_ClothSkinPhysMesh);
DEFINE_STAT(STAT_ClothFillContext);

static TAutoConsoleVariable<float> GClothMaxDeltaTimeTeleportMultiplier(
	TEXT("p.Cloth.MaxDeltaTimeTeleportMultiplier"),
	1.5f,
	TEXT("A multiplier of the MaxPhysicsDelta time at which we will automatically just teleport cloth to its new location\n")
	TEXT(" default: 1.5"));

FClothingSimulationContextCommon::FClothingSimulationContextCommon()
	: ComponentToWorld(FTransform::Identity)
	, WorldGravity(FVector::ZeroVector)
	, WindVelocity(FVector::ZeroVector)
	, WindAdaption(0.f)
	, DeltaSeconds(0.f)
	, TeleportMode(EClothingTeleportMode::None)
	, MaxDistanceScale(1.f)
	, PredictedLod(INDEX_NONE)
{}

FClothingSimulationContextCommon::~FClothingSimulationContextCommon()
{}

void FClothingSimulationContextCommon::Fill(const USkeletalMeshComponent* InComponent, float InDeltaSeconds, float InMaxPhysicsDelta)
{
	SCOPE_CYCLE_COUNTER(STAT_ClothFillContext);
	LLM_SCOPE(ELLMTag::SkeletalMesh);

	check(InComponent);
	FillBoneTransforms(InComponent);
	FillRefToLocals(InComponent);
	FillComponentToWorld(InComponent);
	FillWorldGravity(InComponent);
	FillWindVelocity(InComponent);
	FillDeltaSeconds(InDeltaSeconds, InMaxPhysicsDelta);
	FillTeleportMode(InComponent, InDeltaSeconds, InMaxPhysicsDelta);
	FillMaxDistanceScale(InComponent);

	PredictedLod = InComponent->GetPredictedLODLevel();
}

void FClothingSimulationContextCommon::FillBoneTransforms(const USkeletalMeshComponent* InComponent)
{
	const USkeletalMesh* const SkeletalMesh = InComponent->SkeletalMesh;

	if (USkinnedMeshComponent* const MasterComponent = InComponent->MasterPoseComponent.Get())
	{
		const TArray<int32>& MasterBoneMap = InComponent->GetMasterBoneMap();
		int32 NumBones = MasterBoneMap.Num();

		if (NumBones == 0)
		{
			if (SkeletalMesh)
			{
				// This case indicates an invalid master pose component (e.g. no skeletal mesh)
				NumBones = SkeletalMesh->GetRefSkeleton().GetNum();

				BoneTransforms.Empty(NumBones);
				BoneTransforms.AddDefaulted(NumBones);
			}
		}
		else
		{
			BoneTransforms.Reset(NumBones);
			BoneTransforms.AddDefaulted(NumBones);

			const TArray<FTransform>& MasterTransforms = MasterComponent->GetComponentSpaceTransforms();
			for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
			{
				bool bFoundMaster = false;
				if (MasterBoneMap.IsValidIndex(BoneIndex))
				{
					const int32 MasterIndex = MasterBoneMap[BoneIndex];
					if (MasterIndex != INDEX_NONE && MasterIndex < MasterTransforms.Num())
					{
						BoneTransforms[BoneIndex] = MasterTransforms[MasterIndex];
						bFoundMaster = true;
					}
				}

				if (!bFoundMaster && SkeletalMesh)
				{
					const int32 ParentIndex = SkeletalMesh->GetRefSkeleton().GetParentIndex(BoneIndex);

					BoneTransforms[BoneIndex] =
						BoneTransforms.IsValidIndex(ParentIndex) && ParentIndex < BoneIndex ?
						BoneTransforms[ParentIndex] * SkeletalMesh->GetRefSkeleton().GetRefBonePose()[BoneIndex] :
						SkeletalMesh->GetRefSkeleton().GetRefBonePose()[BoneIndex];
				}
			}
		}
	}
	else
	{
		BoneTransforms = InComponent->GetComponentSpaceTransforms();
	}
}

void FClothingSimulationContextCommon::FillRefToLocals(const USkeletalMeshComponent* InComponent)
{
	RefToLocals.Reset();
	InComponent->GetCurrentRefToLocalMatrices(RefToLocals, InComponent->GetPredictedLODLevel());
}

void FClothingSimulationContextCommon::FillComponentToWorld(const USkeletalMeshComponent* InComponent)
{
	ComponentToWorld = InComponent->GetComponentTransform();
}

void FClothingSimulationContextCommon::FillWorldGravity(const USkeletalMeshComponent* InComponent)
{
	const UWorld* const ComponentWorld = InComponent->GetWorld();
	check(ComponentWorld);
	WorldGravity = FVector(0.f, 0.f, ComponentWorld->GetGravityZ());
}

void FClothingSimulationContextCommon::FillWindVelocity(const USkeletalMeshComponent* InComponent)
{
	InComponent->GetWindForCloth_GameThread(WindVelocity, WindAdaption);
}

void FClothingSimulationContextCommon::FillDeltaSeconds(float InDeltaSeconds, float InMaxPhysicsDelta)
{
	DeltaSeconds = FMath::Min(InDeltaSeconds, InMaxPhysicsDelta);
}

void FClothingSimulationContextCommon::FillTeleportMode(const USkeletalMeshComponent* InComponent, float InDeltaSeconds, float InMaxPhysicsDelta)
{
	TeleportMode = (InDeltaSeconds > InMaxPhysicsDelta * GClothMaxDeltaTimeTeleportMultiplier.GetValueOnGameThread()) ?
		EClothingTeleportMode::Teleport :
		InComponent->ClothTeleportMode;
}

void FClothingSimulationContextCommon::FillMaxDistanceScale(const USkeletalMeshComponent* InComponent)
{
	MaxDistanceScale = InComponent->GetClothMaxDistanceScale();
}

//==============================================================================
// FClothingSimulationCommon
//==============================================================================

FClothingSimulationCommon::FClothingSimulationCommon()
{
	MaxPhysicsDelta = UPhysicsSettings::Get()->MaxPhysicsDeltaTime;
}

FClothingSimulationCommon::~FClothingSimulationCommon()
{}

void FClothingSimulationCommon::FillContext(USkeletalMeshComponent* InComponent, float InDeltaTime, IClothingSimulationContext* InOutContext)
{
	check(InOutContext);
	FClothingSimulationContextCommon* const Context = static_cast<FClothingSimulationContextCommon*>(InOutContext);

	Context->Fill(InComponent, InDeltaTime, MaxPhysicsDelta);

	// Checking the component here to track rare issue leading to invalid contexts
	if (InComponent->IsPendingKill())
	{
		const AActor* const CompOwner = InComponent->GetOwner();
		UE_LOG(LogSkeletalMesh, Warning, 
			TEXT("Attempting to fill a clothing simulation context for a PendingKill skeletal mesh component (Comp: %s, Actor: %s). "
				"Pending kill skeletal mesh components should be unregistered before marked pending kill."), 
			*InComponent->GetName(), CompOwner ? *CompOwner->GetName() : TEXT("None"));

		// Make sure we clear this out to skip any attempted simulations
		Context->BoneTransforms.Reset();
	}

	if (Context->BoneTransforms.Num() == 0)
	{
		const AActor* const CompOwner = InComponent->GetOwner();
		const USkinnedMeshComponent* const Master = InComponent->MasterPoseComponent.Get();
		UE_LOG(LogSkeletalMesh, Warning, TEXT("Attempting to fill a clothing simulation context for a skeletal mesh component that has zero bones (Comp: %s, Master: %s, Actor: %s)."), *InComponent->GetName(), Master ? *Master->GetName() : TEXT("None"), CompOwner ? *CompOwner->GetName() : TEXT("None"));

		// Make sure we clear this out to skip any attempted simulations
		Context->BoneTransforms.Reset();
	}
}
