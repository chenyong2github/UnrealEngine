// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved

#pragma once

#include "NetworkedSimulationModel.h"
#include "BaseMovementComponent.h"

#include "ParametricMovement.generated.h"

// Extremely simple struct for defining parametric motion. This is editable in UParametricMovementComponent's defaults, and also used by the simulation code above. 
USTRUCT(BlueprintType)
struct FSimpleParametricMotion
{
	GENERATED_BODY()

	// Actually turn the given position into a transform. Again, should be static and not conditional on changing state outside of the network sim
	void MapTimeToTransform(const float InPosition, FTransform& OutTransform) const;

	// Advance parametric time. This is meant to do simple things like looping/reversing etc.
	void AdvanceParametricTime(const float InPosition, const float InPlayRate, float &OutPosition, float& OutPlayRate, const float DeltaTimeSeconds) const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=ParametricMovement)
	FVector ParametricDelta = FVector(0.f, 0.f, 500.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=ParametricMovement)
	float MinTime = -1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=ParametricMovement)
	float MaxTime = 1.f;

	FTransform CachedStartingTransform;
};

// -------------------------------------------------------------------------------------------------------------------------------
//	Parametric Movement Simulation Types
// -------------------------------------------------------------------------------------------------------------------------------

// State the client generates
struct FParametricInputCmd
{
	// Input Playrate. This being set can be thought of "telling the simulation what its new playrate should be"
	TOptional<float> PlayRate;

	void NetSerialize(const FNetSerializeParams& P)
	{
		P.Ar << PlayRate;
	}

	void Log(FStandardLoggingParameters& P) const
	{
		if (P.Context == EStandardLoggingContext::HeaderOnly)
		{
			P.Ar->Logf(TEXT(" %d "), P.Frame);
		}
		else if (P.Context == EStandardLoggingContext::Full)
		{
			if (PlayRate.IsSet())
			{
				P.Ar->Logf(TEXT("PlayRate: %.2f"), PlayRate.GetValue());
			}
		}
	}
};

// State we are evolving frame to frame and keeping in sync
struct FParametricSyncState
{
	float Position=0.f;
	float PlayRate=1.f;

	void NetSerialize(const FNetSerializeParams& P)
	{
		P.Ar << Position;
		P.Ar << PlayRate;
	}

	void Log(FStandardLoggingParameters& Params) const
	{
		if (Params.Context == EStandardLoggingContext::HeaderOnly)
		{
			Params.Ar->Logf(TEXT(" %d "), Params.Frame);
		}
		else if (Params.Context == EStandardLoggingContext::Full)
		{
			Params.Ar->Logf(TEXT("Frame: %d"), Params.Frame);
			Params.Ar->Logf(TEXT("Pos: %.2f"), Position);
			Params.Ar->Logf(TEXT("Rate: %.2f"), PlayRate);
		}
	}
};

// Auxiliary state that is input into the simulation. Doesn't change during the simulation tick.
// (It can change and even be predicted but doing so will trigger more bookeeping, etc. Changes will happen "next tick")
struct FParametricAuxState
{
	float Multiplier=1;
	void NetSerialize(const FNetSerializeParams& P)
	{
		P.Ar << Multiplier;
	}

	void Log(FStandardLoggingParameters& Params) const
	{
		if (Params.Context == EStandardLoggingContext::HeaderOnly)
		{
			Params.Ar->Logf(TEXT(" %d "), Params.Frame);
		}
		else if (Params.Context == EStandardLoggingContext::Full)
		{
			Params.Ar->Logf(TEXT("Multiplier: %f"), Multiplier);
		}
	}
};

/** BufferTypes for ParametricMovement */
using ParametricMovementBufferTypes = TNetworkSimBufferTypes<FParametricInputCmd, FParametricSyncState, FParametricAuxState>;

/** The actual movement simulation */
class FParametricMovementSimulation : public FBaseMovementSimulation
{
public:

	void SimulationTick(const FNetSimTimeStep& TimeStep, const TNetSimInput<ParametricMovementBufferTypes>& Input, const TNetSimOutput<ParametricMovementBufferTypes>& Output);

	// Pointer to our static mapping of time->position
	const FSimpleParametricMotion* Motion = nullptr;
};

/** NetworkedSimulation Model type */
class FParametricMovementNetSimModelDef : public FNetSimModelDefBase
{
public:

	using Simulation = FParametricMovementSimulation;
	using BufferTypes = ParametricMovementBufferTypes;

	static const FName GroupName;

	// Compare this state with AuthorityState. return true if a reconcile (correction) should happen
	static bool ShouldReconcile(const FParametricSyncState& AuthoritySync, const FParametricAuxState& AuthorityAux, const FParametricSyncState& PredictedSync, const FParametricAuxState& PredictedAux)
	{
		const float ErrorTolerance = 0.01f;
		return (FMath::Abs<float>(AuthoritySync.Position - PredictedSync.Position) > ErrorTolerance) || (FMath::Abs<float>(AuthoritySync.PlayRate - PredictedSync.PlayRate) > ErrorTolerance);
	}

	static void Interpolate(const TInterpolatorParameters<FParametricSyncState, FParametricAuxState>& Params)
	{
		Params.Out.Sync.Position = Params.From.Sync.Position + ((Params.To.Sync.Position - Params.From.Sync.Position) * Params.InterpolationPCT);
		Params.Out.Sync.PlayRate = Params.From.Sync.PlayRate + ((Params.To.Sync.PlayRate - Params.From.Sync.PlayRate) * Params.InterpolationPCT);
	}
};

/** Additional specialized types of the Parametric Movement NetSimModel */
class FParametricMovementNetSimModelDef_Fixed30hz : public FParametricMovementNetSimModelDef
{ 
public:
	using TickSettings = TNetworkSimTickSettings<33>;
};

// -------------------------------------------------------------------------------------------------------------------------------
//	ActorComponent for running basic Parametric movement. 
//	Parametric movement could be anything that takes a Time and returns an FTransform.
//	
//	Initially, we will support pushing (ie, we sweep as we update the mover's position).
//	But we will not allow a parametric mover from being blocked. 
//
// -------------------------------------------------------------------------------------------------------------------------------

UCLASS(BlueprintType, meta=(BlueprintSpawnableComponent))
class NETWORKPREDICTION_API UParametricMovementComponent : public UBaseMovementComponent, public TNetworkedSimulationModelDriver<ParametricMovementBufferTypes>
{
	GENERATED_BODY()

	UParametricMovementComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// Base TNetworkModelSimulation driver
	FString GetDebugName() const override;
	const AActor* GetVLogOwner() const override;
	void VisualLog(const FParametricInputCmd* Input, const FParametricSyncState* Sync, const FParametricAuxState* Aux, const FVisualLoggingParameters& SystemParameters) const override;

	void ProduceInput(const FNetworkSimTime SimTime, FParametricInputCmd& Cmd);
	void FinalizeFrame(const FParametricSyncState& SyncState, const FParametricAuxState& AuxState) override;

protected:

	TNetSimStateAccessor<FParametricSyncState> MovementSyncState;
	TNetSimStateAccessor<FParametricAuxState> MovementAuxState;

	virtual INetworkedSimulationModel* InstantiateNetworkedSimulation() override;
	FNetworkSimulationModelInitParameters GetSimulationInitParameters(ENetRole Role) override;

	FParametricMovementSimulation* ParametricMovementSimulation = nullptr;

	template<typename TSimulation>
	void InitParametricMovementSimulation(TSimulation* Simulation, FParametricSyncState& InitialSyncState, FParametricAuxState& InitialAuxState)
	{
		check(ParametricMovementSimulation == nullptr);
		ParametricMovementSimulation = Simulation;
		ParametricMovementSimulation->Motion = &ParametricMotion;
	}

	template<typename TNetSimModel>
	void InitParametricMovementNetSimModel(TNetSimModel* Model)
	{
		Model->RepProxy_Simulated.bAllowSimulatedExtrapolation = !bEnableInterpolation;
		MovementSyncState.Bind(Model);
		MovementAuxState.Bind(Model);
	}

	// ------------------------------------------------------------------------
	// Temp Parametric movement example
	//	The essence of this movement simulation is to map some Time value to a transform. That is it.
	//	(It could be mapped via a spline, a curve, a simple blueprint function, etc).
	//	What is below is just a simple C++ implementation to stand things up. Most likely we would 
	//	do additional subclasses to vary the way this is implemented)
	// ------------------------------------------------------------------------
	
	/** Disables starting the simulation. For development/testing ease of use */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=ParametricMovement)
	bool bDisableParametricMovementSimulation = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=ParametricMovement)
	FSimpleParametricMotion ParametricMotion;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=ParametricMovementNetworking)
	bool bEnableDependentSimulation = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=ParametricMovementNetworking)
	bool bEnableInterpolation = true;

	/** Calls ForceNetUpdate every frame. Has slightly different behavior than a very high NetUpdateFrequency */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=ParametricMovementNetworking)
	bool bEnableForceNetUpdate = false;

	/** Sets NetUpdateFrequency on parent. This is editable on the component and really just meant for use during development/test maps */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=ParametricMovementNetworking)
	float ParentNetUpdateFrequency = 0.f;

	TOptional<float> PendingPlayRate = 1.f;
};

