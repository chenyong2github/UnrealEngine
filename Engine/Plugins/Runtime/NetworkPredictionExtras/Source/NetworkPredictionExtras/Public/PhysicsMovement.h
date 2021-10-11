// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NetworkPhysics.h"
#include "NetworkPredictionLog.h"
#include "Async/NetworkPredictionAsyncProxy.h"
#include "NetworkPhysicsComponent.h"
#include "NetworkPredictionCVars.h"
#include "Springs.h"
#include "PhysicsEffect.h"
#include "PhysicsMovement.generated.h"

namespace UE_NP
{
	
	NETSIM_DEVCVAR_SHIPCONST_INT(ForceReconcilePhysicsMovementState, 0, "np2.ForceReconcilePhysicsMovementState", "");
};

USTRUCT(BlueprintType)
struct FPhysicsInputCmd
{
	GENERATED_BODY()

	FPhysicsInputCmd()
		: Force(ForceInitToZero)
		, Torque(ForceInitToZero)
		, Acceleration(ForceInitToZero)
		, AngularAcceleration(ForceInitToZero)
	{ }

	// Simple world vector force to be applied
	UPROPERTY(BlueprintReadWrite,Category="Input")
	FVector	Force;

	UPROPERTY(BlueprintReadWrite, Category = "Input")
	FVector	Torque;

	UPROPERTY(BlueprintReadWrite, Category = "Input")
	FVector	Acceleration;

	UPROPERTY(BlueprintReadWrite, Category = "Input")
	FVector	AngularAcceleration;

	/** Target yaw of character (Degrees). Torque will be applied to rotate character towards target. */
	UPROPERTY(BlueprintReadWrite, Category = "Input")
	float TargetYaw = 0.f;

	UPROPERTY(BlueprintReadWrite,Category="Input")
	bool bJumpedPressed = false;

	UPROPERTY(BlueprintReadWrite,Category="Input")
	bool bBrakesPressed = false;

	// For testing: to ensure the system only sees user created InputCmds and not defaults
	bool bLegit = false;
	// Debugging. 
	int32 Counter = 0;

	void NetSerialize(FArchive& Ar)
	{
		if (Ar.IsSaving() && !bLegit)
		{
			UE_LOG(LogNetworkPrediction, Warning, TEXT("FPhysicsInputCmd Sending Bad Input!"));
		}

		Ar << Force;
		Ar << Torque;
		Ar << Acceleration;
		Ar << AngularAcceleration;
		Ar << TargetYaw;
		Ar << bJumpedPressed;
		Ar << bBrakesPressed;
		Ar << bLegit;
		Ar << Counter;

		if (!Ar.IsSaving() && !bLegit)
		{
			UE_LOG(LogNetworkPrediction, Warning, TEXT("FPhysicsInputCmd Received Bad Input!"));
		}
	}

	bool ShouldReconcile(const FPhysicsInputCmd& AuthState) const;

	FString ToString() const
	{
		return FString::Printf(TEXT("{Count: %d}"), Counter);
	}
};

USTRUCT(BlueprintType)
struct FPhysicsMovementNetState
{
	GENERATED_BODY()

	// Actually used by AsyncTick to scale force applied
	UPROPERTY(BlueprintReadWrite, Category="Mock Object")
	float ForceMultiplier = 125000.f;

	UPROPERTY(BlueprintReadWrite, Category="Mock Object")
	float JumpStrength = 100.f;

	UPROPERTY(BlueprintReadWrite, Category = "Mock Object")
	float AutoFaceTargetYawStrength = 200000.f;
	
	UPROPERTY(BlueprintReadWrite, Category = "Mock Object")
	float AutoFaceTargetYawDamp = 20.f;

	// If enabled, input cmd should specify target yaw.
	UPROPERTY(BlueprintReadWrite, Category = "Mock Object")
	bool bEnableAutoFaceTargetYaw = false;

	// If enabled, object will attempt to keep its up vector aligned with world up.
	UPROPERTY(BlueprintReadWrite, Category = "Mock Object")
	bool bEnableKeepUpright = true;

	// Value 0 to 1 indicating when forces are applied to keep character upright. 0 does nothing, 1 always tries to align character with up,
	// 0.5 will start applying forces when tipped 90 degrees, etc.
	UPROPERTY(BlueprintReadWrite, Category = "Mock Object")
	float KeepUprightThreshold = 0.95f;

	// Strength of auto brake force applied when no input force and on ground.
	UPROPERTY(BlueprintReadWrite, Category = "Mock Object")
	float AutoBrakeStrength = 5.f;

	// Should spring forces scale with mass?
	UPROPERTY(BlueprintReadWrite, Category = "Mock Object")
	bool bScaleSpringForcesWithMass = true;

	// Arbitrary data that doesn't affect sim but could still trigger rollback
	UPROPERTY(BlueprintReadWrite, Category="Mock Object")
	int32 RandValue = 0;

	UPROPERTY(BlueprintReadWrite, Category="Mock Object")
	int32 JumpCooldownMS = 0;

	// Number of frames jump has been pressed
	UPROPERTY(BlueprintReadWrite, Category="Mock Object")
	int32 JumpCount = 0;

	UPROPERTY(BlueprintReadWrite, Category="Mock Object")
	int32 CheckSum = 0;

	// Frame we started "in air recovery" on
	UPROPERTY(BlueprintReadWrite, Category="Mock Object")
	int32 RecoveryFrame = 0;

	// Frame we started jumping on
	UPROPERTY(BlueprintReadWrite, Category="Mock Object")
	int32 JumpStartFrame = 0;

	// Frame we started being in the air
	UPROPERTY(BlueprintReadWrite, Category="Mock Object")
	int32 InAirFrame = 0;

	// Frame we last applied a kick impulse
	UPROPERTY(BlueprintReadWrite, Category="Mock Object")
	int32 KickFrame = 0;

	UPROPERTY(BlueprintReadWrite, Category="Mock Object")
	TArray<FSpring> Springs;

	void NetSerialize(FArchive& Ar)
	{
		Ar << ForceMultiplier;
		Ar << JumpStrength;
		Ar << RandValue;
		Ar << AutoFaceTargetYawStrength;
		Ar << AutoFaceTargetYawDamp;
		Ar << bEnableAutoFaceTargetYaw;
		Ar << AutoBrakeStrength;
		Ar << bScaleSpringForcesWithMass;
		Ar << bEnableKeepUpright;
		Ar << KeepUprightThreshold;
		Ar << JumpCooldownMS;
		Ar << JumpCount;
		Ar << CheckSum;	
		Ar << RecoveryFrame;
		Ar << JumpStartFrame;
		Ar << InAirFrame;
		Ar << KickFrame;
		Ar << Springs;
	}

	bool ShouldReconcile(const FPhysicsMovementNetState& AuthState) const
	{
		return 
			ForceMultiplier != AuthState.ForceMultiplier || 
			JumpStrength != AuthState.JumpStrength ||
			RandValue != AuthState.RandValue ||
			AutoFaceTargetYawStrength != AuthState.AutoFaceTargetYawStrength ||
			AutoFaceTargetYawDamp != AuthState.AutoFaceTargetYawDamp ||
			bEnableAutoFaceTargetYaw != AuthState.bEnableAutoFaceTargetYaw ||
			AutoBrakeStrength != AuthState.AutoBrakeStrength ||
			bScaleSpringForcesWithMass != AuthState.bScaleSpringForcesWithMass || 
			bEnableKeepUpright != AuthState.bEnableKeepUpright ||
			KeepUprightThreshold != AuthState.KeepUprightThreshold || 
			JumpCooldownMS != AuthState.JumpCooldownMS || 
			JumpCount != AuthState.JumpCount ||
			RecoveryFrame != AuthState.RecoveryFrame ||
			JumpStartFrame != AuthState.JumpStartFrame ||
			InAirFrame != AuthState.InAirFrame ||
			KickFrame!= AuthState.KickFrame ||
			CheckSum != AuthState.CheckSum ||
			Springs != AuthState.Springs || 
			(UE_NP::ForceReconcilePhysicsMovementState() > 0);
	}

	FString ToString() const
	{
		return FString();
	}
};

USTRUCT(BlueprintType)
struct FPhysicsMovementLocalState
{
	GENERATED_BODY()

	Chaos::FSingleParticlePhysicsProxy* Proxy = nullptr;

	FCollisionQueryParams QueryParams = FCollisionQueryParams::DefaultQueryParam;
};

UCLASS(BlueprintType, meta=(BlueprintSpawnableComponent))
class NETWORKPREDICTIONEXTRAS_API UPhysicsMovementComponent : public UNetworkPhysicsComponent, public IPhysicsEffectsInterface
{
	GENERATED_BODY()

public:

	UPhysicsMovementComponent();

	virtual void InitializeComponent() override;
	virtual void UninitializeComponent() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	virtual void PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker);

	UPROPERTY(Replicated)
	FNetworkPredictionAsyncProxy NetworkPredictionProxy;

	// Latest MovementState
	UPROPERTY(BlueprintReadOnly, Category="Movement")
	FPhysicsMovementNetState MovementState;

	// Game code should write to this when locally controlling this object.
	UPROPERTY(BlueprintReadWrite, Category="Input")
	FPhysicsInputCmd PendingInputCmd;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGenerateLocalInputCmd);

	// Event called when local input is needed: fill out PendingInputCmd with latest input data
	UPROPERTY(BlueprintAssignable, Category = "Input")
	FOnGenerateLocalInputCmd OnGenerateLocalInputCmd;

	void TestMisprediction();
	
	// ---------------------------------------------------------------------
	//	Movement
	// ---------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category="Movement")
	void SetAutoTargetYawStrength(float Strength);

	UFUNCTION(BlueprintCallable, Category="Movement")
	void SetAutoTargetYawDamp(float YawDamp);

	UFUNCTION(BlueprintCallable, Category="Movement")
	void SetEnableTargetYaw(bool bTargetYaw);

	UFUNCTION(BlueprintCallable, Category="Movement")
	void SetEnableKeepUpright(bool bKeepUpright);

	UFUNCTION(BlueprintCallable, Category="Movement")
	void SetKeepUprightThreshold(float Threshold);

	UFUNCTION(BlueprintCallable, Category="Movement")
	void SetAutoBrakeStrength(float BrakeStrength);

	UFUNCTION(BlueprintCallable, Category = "Movement")
	void SetScaleSpringForcesWithMass(bool bScaleWithMass);
	
	// Add a spring, spring is persistant, can be removed by ClearSprings().
	UFUNCTION(BlueprintCallable, Category="Movement")
	void AddSpring(FSpring Spring);

	UFUNCTION(BlueprintCallable, Category = "Movement")
	void ClearSprings();


	// ---------------------------------------------------------------------
	//	Physics Effects
	// ---------------------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, Category="Physics Effects")
	bool bEnablePhysicsEffects=true;

	FNetworkPredictionAsyncProxy& GetNetworkPredictionAsyncProxy() override { return NetworkPredictionProxy; }
	FPhysicsEffectsExternalState& GetPhysicsEffectsExternalState() override { return PhysicsEffectsExternalState; }

	bool IsController() const override;

	// Forward input producing event to someone else (probably the owning actor)
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPhysicsEffect, int32, TypeID);

	// Delegate to inform the GT/bp that a PE has taken place and is now "seen" on the GT
	UPROPERTY(BlueprintAssignable, Category = "Physics Effect")
	FOnPhysicsEffect NotifyPhysicsEffectExecuted;
	void OnPhysicsEffectExecuted(uint8 TypeID) override { NotifyPhysicsEffectExecuted.Broadcast(TypeID); };

	// Delegate to inform the GT/bp  that a PE will take place soon but not hasn't happened yet
	UPROPERTY(BlueprintAssignable, Category = "Physics Effect")
	FOnPhysicsEffect NotifyPhysicsEffectWindUp;
	void OnPhysicsEffectWindUp(uint8 TypeID) override { NotifyPhysicsEffectWindUp.Broadcast(TypeID); };

private:

	FPhysicsEffectsExternalState PhysicsEffectsExternalState;

	APlayerController* GetOwnerPC() const;

	// Crude way of detecting possession change
	UPROPERTY()
	APlayerController* CachedPC = nullptr;
	bool bHasRegisteredController = false;
};