// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NetworkPhysics.h"
#include "NetworkPredictionLog.h"
#include "Async/NetworkPredictionAsyncProxy.h"
#include "NetworkPhysicsComponent.h"
#include "NetworkPredictionCVars.h"
#include "PhysicsMovement.generated.h"

namespace UE_NP
{
	NETSIM_DEVCVAR_SHIPCONST_INT(ForceReconcilePhysicsInputCmd, 0, "np2.PhysicsInputCmdForceReconcile", "");
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

	bool ShouldReconcile(const FPhysicsInputCmd& AuthState) const
	{
		return FVector::DistSquared(Force, AuthState.Force) > 0.1f
			|| FVector::DistSquared(Torque, AuthState.Torque) > 0.1f
			|| FVector::DistSquared(Acceleration, AuthState.Acceleration) > 0.1f
			|| FVector::DistSquared(AngularAcceleration, AuthState.AngularAcceleration) > 0.1f
			|| !FMath::IsNearlyEqual(TargetYaw, AuthState.TargetYaw, 1.0f)
			|| bJumpedPressed != AuthState.bJumpedPressed
			//|| Counter != AuthState.Counter // this will cause constant corrections with multiple clients but useful in testing single client
			|| bBrakesPressed != AuthState.bBrakesPressed
			|| (UE_NP::ForceReconcilePhysicsInputCmd() > 0);
			
	}

	FString ToString() const
	{
		return FString(TEXT(""));
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

	// Strength of auto brake force applied when no input force and on ground.
	UPROPERTY(BlueprintReadWrite, Category = "Mock Object")
	float AutoBrakeStrength = 5.f;

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

	void NetSerialize(FArchive& Ar)
	{
		Ar << ForceMultiplier;
		Ar << JumpStrength;
		Ar << RandValue;
		Ar << AutoFaceTargetYawStrength;
		Ar << AutoFaceTargetYawDamp;
		Ar << bEnableAutoFaceTargetYaw;
		Ar << AutoBrakeStrength;
		Ar << bEnableKeepUpright;
		Ar << JumpCooldownMS;
		Ar << JumpCount;
		Ar << CheckSum;	
		Ar << RecoveryFrame;
		Ar << JumpStartFrame;
		Ar << InAirFrame;
		Ar << KickFrame;
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
			bEnableKeepUpright != AuthState.bEnableKeepUpright ||
			JumpCooldownMS != AuthState.JumpCooldownMS || 
			JumpCount != AuthState.JumpCount ||
			RecoveryFrame != AuthState.RecoveryFrame ||
			JumpStartFrame != AuthState.JumpStartFrame ||
			InAirFrame != AuthState.InAirFrame ||
			KickFrame!= AuthState.KickFrame ||
			CheckSum != AuthState.CheckSum ||
			(UE_NP::ForceReconcilePhysicsMovementState() > 0);
	}
};

USTRUCT(BlueprintType)
struct FPhysicsMovementLocalState
{
	GENERATED_BODY()

	FSingleParticlePhysicsProxy* Proxy = nullptr;

	FCollisionQueryParams QueryParams = FCollisionQueryParams::DefaultQueryParam;
};

UCLASS(BlueprintType, meta=(BlueprintSpawnableComponent))
class NETWORKPREDICTIONEXTRAS_API UPhysicsMovementComponent : public UNetworkPhysicsComponent
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

	

	UFUNCTION(BlueprintCallable, Category="Movement")
	void SetAutoTargetYawStrength(float Strength);

	UFUNCTION(BlueprintCallable, Category="Movement")
	void SetAutoTargetYawDamp(float YawDamp);

	UFUNCTION(BlueprintCallable, Category="Movement")
	void SetEnableTargetYaw(bool bTargetYaw);

	UFUNCTION(BlueprintCallable, Category="Movement")
	void SetEnableKeepUpright(bool bKeepUpright);

	UFUNCTION(BlueprintCallable, Category="Movement")
	void SetAutoBrakeStrength(float BrakeStrength);

	void TestMisprediction();

private:

	APlayerController* GetOwnerPC() const;

	// Crude way of detecting possession change
	UPROPERTY()
	APlayerController* CachedPC = nullptr;
	bool bHasRegisteredController = false;
};