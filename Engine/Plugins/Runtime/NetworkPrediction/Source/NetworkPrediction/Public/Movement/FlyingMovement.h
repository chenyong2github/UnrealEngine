// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "Engine/NetSerialization.h"
#include "Engine/EngineTypes.h"
#include "Engine/EngineBaseTypes.h"
#include "WorldCollision.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Misc/OutputDevice.h"
#include "Misc/CoreDelegates.h"
#include "NetworkSimulationModelTemplates.h"
#include "NetworkPredictionComponent.h"

#include "FlyingMovement.generated.h"

// -------------------------------------------------------------------------------------------------------------------------------
// FlyingMovement: simple flying movement that was based on UE4's FloatingPawnMovement
// -------------------------------------------------------------------------------------------------------------------------------

class IFlyingMovementDriver;
class USceneComponent;

namespace FlyingMovement
{
	// State the client generates
	struct FInputCmd
	{

		// Client's FrameTime. This is essential for a non-fixed step simulation. The server will run the movement simulation for this client at this rate.
		float FrameDeltaTime = 0.f;

		// Input: "pure" input for this frame. At this level, frame time has not been accounted for. (E.g., "move straight" would be (1,0,0) regardless of frame time)
		FRotator RotationInput;
		FVector MovementInput;

		void NetSerialize(const FNetSerializeParams& P)
		{
			P.Ar << FrameDeltaTime;
			P.Ar << RotationInput;
			P.Ar << MovementInput;
		}

		void Log(FStandardLoggingParameters& P) const
		{
			if (P.Context == EStandardLoggingContext::HeaderOnly)
			{
				P.Ar->Logf(TEXT(" %d "), P.Keyframe);
			}
			else if (P.Context == EStandardLoggingContext::Full)
			{
				P.Ar->Logf(TEXT("FrameDeltaTime: %.4f"), FrameDeltaTime);
				P.Ar->Logf(TEXT("Movement: %s"), *MovementInput.ToString());
				P.Ar->Logf(TEXT("ViewRot: %s"), *RotationInput.ToString());
			}
		}
	};

	// State we are evolving frame to frame and keeping in sync
	struct FMoveState
	{
		FVector Location;
		FVector Velocity;
		FRotator Rotation;

		void NetSerialize(const FNetSerializeParams& P)
		{
			P.Ar << Location;
			P.Ar << Velocity;
			P.Ar << Rotation;
		}

		// Compare this state with AuthorityState. return true if a reconcile (correction) should happen
		bool ShouldReconcile(const FMoveState& AuthorityState)
		{
			const float ErrorTolerance = 1.f;
			return !AuthorityState.Location.Equals(Location, ErrorTolerance);
		}

		void Log(FStandardLoggingParameters& Params) const
		{
			if (Params.Context == EStandardLoggingContext::HeaderOnly)
			{
				Params.Ar->Logf(TEXT(" %d "), Params.Keyframe);
			}
			else if (Params.Context == EStandardLoggingContext::Full)
			{
				Params.Ar->Logf(TEXT("Frame: %d"), Params.Keyframe);
				Params.Ar->Logf(TEXT("Loc: %s"), *Location.ToString());
				Params.Ar->Logf(TEXT("Vel: %s"), *Velocity.ToString());
				Params.Ar->Logf(TEXT("Rot: %s"), *Rotation.ToString());
			}
		}

		void VisualLog(const FVisualLoggingParameters& Parameters, IFlyingMovementDriver* Driver, IFlyingMovementDriver* LogDriver);
	};

	// Auxiliary state that is input into the simulation. Doesn't change during the simulation tick.
	// (It can change and even be predicted but doing so will trigger more bookeeping, etc. Changes will happen "next tick")
	struct FAuxState
	{
		float Multiplier=1;
		void NetSerialize(const FNetSerializeParams& P)
		{
			P.Ar << Multiplier;
		}
	};

	// Actual definition of our network simulation.
	class FMovementSystem : public TNetworkedSimulationModel<FMovementSystem, FInputCmd, FMoveState, FAuxState>
	{
	public:

		// Interface between the simulation and owning component driving it. Functions added here are available in ::Update and must be implemented by UMockNetworkSimulationComponent.
		class IMovementDriver : public IDriver
		{
		public:

			virtual bool SafeMoveUpdatedComponent(const FVector& Delta, const FQuat& NewRotation, bool bSweep, FHitResult& OutHit, ETeleportType Teleport) const = 0;
			virtual bool MoveUpdatedComponent(const FVector& Delta, const FQuat& NewRotation, bool bSweep, FHitResult* OutHit, ETeleportType Teleport) const = 0;
			virtual FTransform GetUpdateComponentTransform() const = 0;


			virtual void GetCapsuleDimensions(float &Radius, float& HalfHeight) const = 0;
			virtual UWorld* GetDriverWorld() const = 0;
			virtual UObject* GetVLogOwner() const = 0;
			virtual FTransform GetDebugWorldTransform() const = 0;
		};

		/** Main update function */
		static void Update(IMovementDriver* Driver, const FInputCmd& InputCmd, const FMoveState& InputState, FMoveState& OutputState, const FAuxState& AuxState);

		/** Dev tool to force simple mispredict */
		static bool ForceMispredict;
	};

	// general tolerance value for rotation checks
	static const float ROTATOR_TOLERANCE = (1e-3);

} // End namespace

// Needed to trick UHT into letting UMockNetworkSimulationComponent implement. UHT cannot parse the ::
// Also needed for forward declaring. Can't just be a typedef/using =
class IFlyingMovementDriver : public FlyingMovement::FMovementSystem::IMovementDriver { };

// -------------------------------------------------------------------------------------------------------------------------------
// ActorComponent for running FlyingMovement 
// -------------------------------------------------------------------------------------------------------------------------------

UCLASS(BlueprintType, meta=(BlueprintSpawnableComponent))
class NETWORKPREDICTION_API UFlyingMovementComponent : public UNetworkPredictionComponent, public IFlyingMovementDriver
{
	GENERATED_BODY()

public:

	UFlyingMovementComponent();

	virtual void InitializeComponent() override;
	virtual void OnRegister() override;
	virtual void RegisterComponentTickFunctions(bool bRegister) override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;

	// Gets writable copy of FClientInputCmd for this frame. Should only be called once per frame by the local player.
	FlyingMovement::FInputCmd* GetNextClientInputCmdForWrite(float DeltaTime);

	// IFlyingMovementDriver
	bool SafeMoveUpdatedComponent(const FVector& Delta, const FQuat& NewRotation, bool bSweep, FHitResult& OutHit, ETeleportType Teleport) const;
	bool MoveUpdatedComponent(const FVector& Delta, const FQuat& NewRotation, bool bSweep, FHitResult* OutHit, ETeleportType Teleport) const;
	FTransform GetUpdateComponentTransform() const;

	void InitSyncState(FlyingMovement::FMoveState& OutSyncState) const override;
	void SyncTo(const FlyingMovement::FMoveState& SyncState) override;
	virtual UWorld* GetDriverWorld() const override final { return GetWorld(); }
	virtual UObject* GetVLogOwner() const override final;
	virtual FTransform GetDebugWorldTransform() const override final;
	virtual void GetCapsuleDimensions(float &Radius, float& HalfHeight) const;

protected:

	// Basic "Update Component/Ticking"
	virtual void SetUpdatedComponent(USceneComponent* NewUpdatedComponent);
	virtual void UpdateTickRegistration();

	UFUNCTION()
	virtual void PhysicsVolumeChanged(class APhysicsVolume* NewVolume);

	// Network Prediction
	virtual IReplicationProxy* InstantiateNetworkSimulation() override;	
	virtual void InitializeForNetworkRole(ENetRole Role) override;
	TUniquePtr<FlyingMovement::FMovementSystem> NetworkSim; // The Network sim that this component is managing. This is what is doing all the work.

private:

	UPROPERTY()
	USceneComponent* UpdatedComponent = nullptr;

	UPROPERTY()
	UPrimitiveComponent* UpdatedPrimitive = nullptr;

	/** Transient flag indicating whether we are executing OnRegister(). */
	bool bInOnRegister = false;
	
	/** Transient flag indicating whether we are executing InitializeComponent(). */
	bool bInInitializeComponent = false;

	static FVector GetPenetrationAdjustment(const FHitResult& Hit);

	bool OverlapTest(const FVector& Location, const FQuat& RotationQuat, const ECollisionChannel CollisionChannel, const FCollisionShape& CollisionShape, const AActor* IgnoreActor) const;
	void InitCollisionParams(FCollisionQueryParams &OutParams, FCollisionResponseParams& OutResponseParam) const;
	bool ResolvePenetration(const FVector& ProposedAdjustment, const FHitResult& Hit, const FQuat& NewRotationQuat) const;

	/**  Flags that control the behavior of calls to MoveComponent() on our UpdatedComponent. */
	mutable EMoveComponentFlags MoveComponentFlags = MOVECOMP_NoFlags; // Mutable because we sometimes need to disable these flags ::ResolvePenetration. Better way may be possible
};