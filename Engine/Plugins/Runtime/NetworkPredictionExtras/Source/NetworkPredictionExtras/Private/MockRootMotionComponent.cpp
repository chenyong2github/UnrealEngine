// Copyright Epic Games, Inc. All Rights Reserved.

#include "MockRootMotionComponent.h"
#include "NetworkPredictionModelDef.h"
#include "NetworkPredictionModelDefRegistry.h"
#include "NetworkPredictionProxyInit.h"
#include "Components/SkeletalMeshComponent.h"
#include "MockRootMotionSourceDataAsset.h"
#include "Animation/AnimMontage.h"
#include "NetworkPredictionProxyWrite.h"
#include "Curves/CurveVector.h"
#include "Animation/AnimInstance.h"

DEFINE_LOG_CATEGORY_STATIC(LogMockRootMotionComponent, Log, All);

/** NetworkedSimulation Model type */
class FMockRootMotionModelDef : public FNetworkPredictionModelDef
{
public:

	NP_MODEL_BODY();

	using StateTypes = MockRootMotionStateTypes;
	using Simulation = FMockRootMotionSimulation;
	using Driver = UMockRootMotionComponent;

	static const TCHAR* GetName() { return TEXT("MockRootMotion"); }
	static constexpr int32 GetSortPriority() { return (int32)ENetworkPredictionSortPriority::PreKinematicMovers + 6; }
};

NP_MODEL_REGISTER(FMockRootMotionModelDef);

// -------------------------------------------------------------------------------------------------------
//	UMockRootMotionComponent
// -------------------------------------------------------------------------------------------------------

void UMockRootMotionComponent::SetUpdatedComponent(USceneComponent* NewUpdatedComponent)
{
	Super::SetUpdatedComponent(NewUpdatedComponent);
	FindAndCacheAnimInstance();
}

void UMockRootMotionComponent::FindAndCacheAnimInstance()
{
	if (AnimInstance != nullptr)
	{
		return;
	}

	AActor* Owner = GetOwner();
	npCheckSlow(Owner);

	USkeletalMeshComponent* SkelMeshComp = Cast<USkeletalMeshComponent>(Owner->GetRootComponent());
	if (SkelMeshComp == nullptr)
	{
		SkelMeshComp = Owner->FindComponentByClass<USkeletalMeshComponent>();
	}

	if (SkelMeshComp)
	{
		AnimInstance = SkelMeshComp->GetAnimInstance();
	}
}

void UMockRootMotionComponent::InitializeNetworkPredictionProxy()
{
	if (!npEnsureMsgf(RootMotionSourceDataAsset != nullptr, TEXT("No RootMotionSourceDataAsset set on %s. Skipping root motion init."), *GetPathName()))
	{
		return;
	}

	if (!npEnsureMsgf(UpdatedComponent != nullptr, TEXT("No UpdatedComponent set on %s. Skipping root motion init."), *GetPathName()))
	{
		return;
	}

	FindAndCacheAnimInstance();

	if (!npEnsureMsgf(AnimInstance != nullptr, TEXT("No AnimInstance set on %s. Skipping root motion init."), *GetPathName()))
	{
		return;
	}

	OwnedMockRootMotionSimulation = MakeUnique<FMockRootMotionSimulation>();
	OwnedMockRootMotionSimulation->SourceMap = this->RootMotionSourceDataAsset;
	OwnedMockRootMotionSimulation->RootMotionComponent = AnimInstance->GetOwningComponent();
	OwnedMockRootMotionSimulation->SetComponents(UpdatedComponent, UpdatedPrimitive);

	NetworkPredictionProxy.Init<FMockRootMotionModelDef>(GetWorld(), GetReplicationProxies(), OwnedMockRootMotionSimulation.Get(), this);
}

void UMockRootMotionComponent::InitializeSimulationState(FMockRootMotionSyncState* SyncState, void* AuxState)
{
	// This assumes no animation is currently playing. Any "play anim on startup" should go through the same path 
	// as playing an animation at runttime wrt NP.

	npCheckSlow(UpdatedComponent);
	npCheckSlow(SyncState);

	SyncState->Location = UpdatedComponent->GetComponentLocation();
	SyncState->Rotation = UpdatedComponent->GetComponentQuat().Rotator();
}

void UMockRootMotionComponent::ProduceInput(const int32 SimTimeMS, FMockRootMotionInputCmd* Cmd)
{
	npCheckSlow(Cmd);
	*Cmd = PendingInputCmd;
}

void UMockRootMotionComponent::FinalizeFrame(const FMockRootMotionSyncState* SyncState, const void* AuxState)
{
	npCheckSlow(UpdatedComponent);
	npCheckSlow(AnimInstance);

	
	// Update component transform
	FTransform Transform(SyncState->Rotation.Quaternion(), SyncState->Location, UpdatedComponent->GetComponentTransform().GetScale3D() );

	UpdatedComponent->SetWorldTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);

	// Update animation state (pose) - make sure it matches SyncState.
	RootMotionSourceDataAsset->FinalizePose(SyncState, AnimInstance);
}


void UMockRootMotionComponent::Input_PlayRootMotionBySourceID(int32 ID)
{
	if (!npEnsureMsgf(RootMotionSourceDataAsset != nullptr, TEXT("No RootMotionSourceDataAsset set on %s. Skipping Input_PlayRootMotionBySourceID."), *GetPathName()))
	{
		return;
	}

	if (!RootMotionSourceDataAsset->IsValidSourceID(ID))
	{
		UE_LOG(LogMockRootMotionComponent, Warning, TEXT("Invalid RootMotionSource ID: %d called on %s. Skipping"), ID, *GetPathName());
		return;
	}

	PendingInputCmd.PlaySourceID = ID;
	PendingInputCmd.PlayCount++;
}

template<typename AssetType>
int32 UMockRootMotionComponent::PlayRootMotionByAssetType(AssetType* Asset)
{
	if (!npEnsureMsgf(RootMotionSourceDataAsset != nullptr, TEXT("No RootMotionSourceDataAsset set on %s. Skipping PlayRootMotion call."), *GetPathName()))
	{
		return INDEX_NONE;
	}

	int32 ID = RootMotionSourceDataAsset->FindRootMotionSourceID(Asset);
	if (ID == INDEX_NONE)
	{
		UE_LOG(LogMockRootMotionComponent, Warning, TEXT("Invalid RootMotion asset %s. Not in %s. Skipping"), *Asset->GetName(), *RootMotionSourceDataAsset->GetPathName());
		return ID;
	}

	PendingInputCmd.PlaySourceID = ID;
	PendingInputCmd.PlayCount++;

	return ID;
}


void UMockRootMotionComponent::Input_PlayRootMotionByMontage(UAnimMontage* Montage)
{
	PlayRootMotionByAssetType(Montage);
}

void UMockRootMotionComponent::Input_PlayRootMotionByCurve(UCurveVector* CurveVector)
{
	PlayRootMotionByAssetType(CurveVector);
}

void UMockRootMotionComponent::PlayRootMotionMontage(UAnimMontage* Montage, float PlayRate)
{
	if (!npEnsureMsgf(RootMotionSourceDataAsset != nullptr, TEXT("No RootMotionSourceDataAsset set on %s. Skipping PlayRootMotionMontage."), *GetPathName()))
	{
		return;
	}

	int32 ID = RootMotionSourceDataAsset->FindRootMotionSourceID(Montage);
	if (ID == INDEX_NONE)
	{
		UE_LOG(LogMockRootMotionComponent, Warning, TEXT("Invalid Montage %s. Not in %s. Skipping"), *Montage->GetName(), *RootMotionSourceDataAsset->GetPathName());
		return;
	}

	// We are writing directly to the sync state here, not setting up the pending input cmd.
	//	The server can always do this - they are the authority and can set the sync state to whatever they want.
	//	The client is free to do this - but will get a mispredict if the server does not do it too.
	//		This type of prediction is "risky" - the client has to predict doing it at the same simulation time as the server
	//		but since this function (UMockRootMotionComponent::PlayRootMotionMontage) is callable from anywhere, its not possible
	//		to guaranteed. If you want guaranteed prediction, do it via InputCmd and the SimulationTick logic.

	NetworkPredictionProxy.WriteSyncState<FMockRootMotionSyncState>([ID, PlayRate](FMockRootMotionSyncState& Sync)
	{
		Sync.RootMotionSourceID = ID;
		Sync.PlayRate = PlayRate;
		Sync.PlayPosition = 0.f;
	}, "UMockRootMotionComponent::PlayRootMotionMontage");	// The string here is for Insights tracing. "Who did this?"
}