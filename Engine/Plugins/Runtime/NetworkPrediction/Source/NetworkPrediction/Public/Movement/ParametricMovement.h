// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved

#pragma once

#include "NetworkSimulationModelTemplates.h"
#include "BaseMovementComponent.h"

#include "ParametricMovement.generated.h"

class IParametricMovementDriver;

namespace ParametricMovement
{
	// State the client generates
	struct FInputCmd
	{
		// Client's FrameTime. This is essential for a non-fixed step simulation. The server will run the movement simulation for this client at this rate.
		float FrameDeltaTime = 0.f;

		// Input Playrate. This being set can be thought of "telling the simulation what its new playrate should be"
		TOptional<float> PlayRate;

		void NetSerialize(const FNetSerializeParams& P)
		{
			// FIXME: quantization and effecient packing of this data needs to be looked at/generalized in a nice way
			P.Ar << FrameDeltaTime;
			P.Ar << PlayRate;
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
				if (PlayRate.IsSet())
				{
					P.Ar->Logf(TEXT("PlayRate: %.2f"), PlayRate.GetValue());
				}
			}
		}
	};

	// State we are evolving frame to frame and keeping in sync
	struct FMoveState
	{
		float Position;
		float PlayRate;

		void NetSerialize(const FNetSerializeParams& P)
		{
			P.Ar << Position;
			P.Ar << PlayRate;
		}

		// Compare this state with AuthorityState. return true if a reconcile (correction) should happen
		bool ShouldReconcile(const FMoveState& AuthorityState)
		{
			const float ErrorTolerance = 0.01f;
			return (FMath::Abs<float>(AuthorityState.Position - Position) > ErrorTolerance) || (FMath::Abs<float>(AuthorityState.PlayRate - PlayRate) > ErrorTolerance);
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
				Params.Ar->Logf(TEXT("Pos: %.2f"), Position);
				Params.Ar->Logf(TEXT("Rate: %.2f"), PlayRate);
			}
		}

		void VisualLog(const FVisualLoggingParameters& Parameters, IParametricMovementDriver* Driver, IParametricMovementDriver* LogDriver);
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

			// BaseMovement driver API (functions for moving around a primitive component)
			virtual IBaseMovementDriver& GetBaseMovementDriver() = 0;

			// Advance parametric time. This is meant to do simple things like looping/reversing etc.
			// Note how this should be STATIC and not rely on state outside of what is passed in (such thing would need to be done inside the simulation, not through the driver!)
			virtual void AdvanceParametricTime(const float InPosition, const float InPlayRate, float &OutPosition, float& OutPlayRate, const float DeltaTimeSeconds) const = 0;

			// Actually turn the given position into a transform. Again, should be static and not conditional on changing state outside of the network sim
			virtual void SetTransformForPosition(const float InPosition, FTransform& OutTransform) const = 0;
		};

		/** Main update function */
		static void Update(IMovementDriver* Driver, const FInputCmd& InputCmd, const FMoveState& InputState, FMoveState& OutputState, const FAuxState& AuxState);

		/** Dev tool to force simple mispredict */
		static bool ForceMispredict;
	};

} // End namespace

// Needed to trick UHT into letting UMockNetworkSimulationComponent implement. UHT cannot parse the ::
// Also needed for forward declaring. Can't just be a typedef/using =
class IParametricMovementDriver : public ParametricMovement::FMovementSystem::IMovementDriver { };

// -------------------------------------------------------------------------------------------------------------------------------
//	ActorComponent for running basic Parametric movement. 
//	Parametric movement could be anything that takes a Time and returns an FTransform.
//	
//	Initially, we will support pushing (ie, we sweep as we update the mover's position).
//	But we will not allow a parametric mover from being blocked. 
//
// -------------------------------------------------------------------------------------------------------------------------------

UCLASS(BlueprintType, meta=(BlueprintSpawnableComponent))
class NETWORKPREDICTION_API UParametricMovementComponent : public UBaseMovementComponent, public IParametricMovementDriver
{
	GENERATED_BODY()

	UParametricMovementComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// Base TNetworkModelSimulation driver
	void InitSyncState(ParametricMovement::FMoveState& OutSyncState) const override;
	void SyncTo(const ParametricMovement::FMoveState& SyncState) override;

	// Base Movement Driver
	IBaseMovementDriver& GetBaseMovementDriver() override final { return *static_cast<IBaseMovementDriver*>(this); }

	// Parametric Movement Driver
	float GetTransformForTime(const float InTime, FTransform& OutTransform) const;

	virtual void AdvanceParametricTime(const float InPosition, const float InPlayRate, float &OutPosition, float& OutPlayRate, const float DeltaTimeSeconds) const override;
	virtual void SetTransformForPosition(const float InPosition, FTransform& OutTransform) const override;

protected:

	virtual IReplicationProxy* InstantiateNetworkSimulation() override;	
	void InitializeForNetworkRole(ENetRole Role) override;
	FNetworkSimulationModelInitParameters GetSimulationInitParameters(ENetRole Role) override;

	TUniquePtr<ParametricMovement::FMovementSystem> NetworkSim;

	// Temp....
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=ParametricMovement)
	FVector ParametricDelta = FVector(0.f, 0.f, 500.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=ParametricMovement)
	float MinTime = -1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=ParametricMovement)
	float MaxTime = 1.f;

	FTransform CachedStartingTransform;

	TOptional<float> PendingPlayRate = 1.f;
};

