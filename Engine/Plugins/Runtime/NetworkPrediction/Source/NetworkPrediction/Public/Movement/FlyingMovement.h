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
#include "NetworkSimulationModel.h"
#include "BaseMovementComponent.h"

#include "FlyingMovement.generated.h"

// -------------------------------------------------------------------------------------------------------------------------------
// FlyingMovement: simple flying movement that was based on UE4's FloatingPawnMovement
// -------------------------------------------------------------------------------------------------------------------------------

class IFlyingMovementDriver;
class USceneComponent;

namespace FlyingMovement
{
	class IMovementDriver;

	// State the client generates
	struct FInputCmd
	{
		// Input: "pure" input for this frame. At this level, frame time has not been accounted for. (E.g., "move straight" would be (1,0,0) regardless of frame time)
		FRotator RotationInput;
		FVector MovementInput;

		void NetSerialize(const FNetSerializeParams& P)
		{
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

		void VisualLog(const FVisualLoggingParameters& Parameters, IMovementDriver* Driver, IMovementDriver* LogDriver) const;

		static void Interpolate(const FMoveState& From, const FMoveState& To, const float PCT, FMoveState& OutDest)
		{
			OutDest.Location = From.Location + ((To.Location - From.Location) * PCT);
			OutDest.Rotation = From.Rotation + ((To.Rotation - From.Rotation) * PCT);
		}
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

	using TMovementBufferTypes = TNetworkSimBufferTypes<FInputCmd, FMoveState, FAuxState>;

	static FName SimulationGroupName("FlyingMovement");

	// Interface between the simulation and owning component driving it. Functions added here are available in ::Update and must be implemented by UMockNetworkSimulationComponent.
	class IMovementDriver : public TNetworkSimDriverInterfaceBase<TMovementBufferTypes>
	{
	public:

		// Interface for moving the collision component around
		virtual IBaseMovementDriver& GetBaseMovementDriver() = 0;

		// Called prior to running the sim to make sure to make sure the collision component is in the right place. 
		// This is unfortunate and not good, but is needed to ensure our collision and world position have not been moved out from under us.
		// Refactoring primitive component movement to allow the sim to do all collision queries outside of the component code would be ideal.
		virtual void PreSimSync(const FMoveState& SyncState) = 0;
	};

	class FMovementSimulation
	{
	public:
		/** Main update function */
		static void Update(IMovementDriver* Driver, const float DeltaSeconds, const FInputCmd& InputCmd, const FMoveState& InputState, FMoveState& OutputState, const FAuxState& AuxState);

		/** Tick group the simulation maps to */
		static const FName GroupName;

		/** Dev tool to force simple mispredict */
		static bool ForceMispredict;
	};

	// Actual definition of our network simulation.
	template<int32 InFixedStepMS=0>
	using FMovementSystem = TNetworkedSimulationModel<FMovementSimulation, IMovementDriver, TMovementBufferTypes, TNetworkSimTickSettings<InFixedStepMS> >;

	// Simulation time used by the movement system
	//using TSimTime = FMovementSystem::TSimTime;

	// general tolerance value for rotation checks
	static const float ROTATOR_TOLERANCE = (1e-3);

} // End namespace

// Needed to trick UHT into letting UMockNetworkSimulationComponent implement. UHT cannot parse the ::
// Also needed for forward declaring. Can't just be a typedef/using =
class IFlyingMovementDriver : public FlyingMovement::IMovementDriver { };

// -------------------------------------------------------------------------------------------------------------------------------
// ActorComponent for running FlyingMovement 
// -------------------------------------------------------------------------------------------------------------------------------

UCLASS(BlueprintType, meta=(BlueprintSpawnableComponent))
class NETWORKPREDICTION_API UFlyingMovementComponent : public UBaseMovementComponent, public IFlyingMovementDriver
{
	GENERATED_BODY()

public:

	UFlyingMovementComponent();
	
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;

	IBaseMovementDriver& GetBaseMovementDriver() override final { return *static_cast<IBaseMovementDriver*>(this); }

	FString GetDebugName() const override;
	const UObject* GetVLogOwner() const override;
	void InitSyncState(FlyingMovement::FMoveState& OutSyncState) const override;
	void PreSimSync(const FlyingMovement::FMoveState& SyncState) override;
	void ProduceInput(const FNetworkSimTime SimTime, FlyingMovement::FInputCmd& Cmd) override;
	void FinalizeFrame(const FlyingMovement::FMoveState& SyncState) override;

	DECLARE_DELEGATE_TwoParams(FProduceFlyingInput, const FNetworkSimTime /*SimTime*/, FlyingMovement::FInputCmd& /*Cmd*/)
	FProduceFlyingInput ProduceInputDelegate;

protected:

	// Network Prediction
	virtual INetworkSimulationModel* InstantiateNetworkSimulation() override;
};