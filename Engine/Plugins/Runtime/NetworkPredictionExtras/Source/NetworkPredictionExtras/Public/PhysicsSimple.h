// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NetworkPhysics.h"
#include "NetworkPredictionLog.h"
#include "Async/NetworkPredictionAsyncProxy.h"
#include "NetworkPhysicsComponent.h"
#include "NetworkPredictionCVars.h"
#include "PhysicsSimple.generated.h"

namespace UE_NP
{
	NETSIM_DEVCVAR_SHIPCONST_INT(ForceReconcileSimpleInput, 0, "np2.ForceReconcileSimpleInput", "");
	NETSIM_DEVCVAR_SHIPCONST_INT(ForceReconcileSimpleState, 0, "np2.ForceReconcileSimpleState", "");
};

USTRUCT(BlueprintType)
struct FSimpleInputCmd
{
	GENERATED_BODY()

	FSimpleInputCmd()
		: MovementDir(ForceInitToZero)
	{ }

	// Simple world vector force to be applied
	UPROPERTY(BlueprintReadWrite,Category="Input")
	FVector	MovementDir;

	UPROPERTY(BlueprintReadWrite,Category="Input")
	bool	bButtonPressed;
	
	UPROPERTY(BlueprintReadWrite,Category="Input")
	bool	bLegit = false;

	void NetSerialize(FArchive& Ar)
	{
		if (Ar.IsSaving() && !bLegit)
		{
			UE_LOG(LogNetworkPrediction, Warning, TEXT("FSimpleInputCmd Sending Bad Input!"));
		}
		
		Ar << MovementDir;
		Ar << bButtonPressed;
		Ar << bLegit;

		if (!Ar.IsSaving() && !bLegit)
		{
			UE_LOG(LogNetworkPrediction, Warning, TEXT("FSimpleInputCmd Received Bad Input!"));
		}
	}

	bool ShouldReconcile(const FSimpleInputCmd& AuthState) const
	{
		return	MovementDir != AuthState.MovementDir ||
				bButtonPressed != AuthState.bButtonPressed ||
				bLegit != AuthState.bLegit ||
				UE_NP::ForceReconcileSimpleInput() > 0;
	}

	FString ToString() const
	{
		return FString::Printf(TEXT("[%d] %s %s"), bButtonPressed, *MovementDir.ToString(), bLegit ? TEXT("") : TEXT("BAD INPUT"));
	}

	bool ShouldLog() const { return bButtonPressed; }
};

USTRUCT(BlueprintType)
struct FSimpleNetState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="Simple Object")
	int32 ButtonPressedCounter = 0;

	// Frame we last applied a kick impulse
	UPROPERTY(BlueprintReadWrite, Category="Simple Object")
	int32 Counter = 0;

	void NetSerialize(FArchive& Ar)
	{
		Ar << ButtonPressedCounter;
		Ar << Counter;
	}

	bool ShouldReconcile(const FSimpleNetState& AuthState) const
	{
		return	ButtonPressedCounter != AuthState.ButtonPressedCounter ||
				Counter != AuthState.Counter ||
				UE_NP::ForceReconcileSimpleState() > 0;
	}
	
	FString ToString() const
	{
		return FString::Printf(TEXT("[%d/%d]"), ButtonPressedCounter, Counter);	
	}
};

USTRUCT(BlueprintType)
struct FSimpleLocalState
{
	GENERATED_BODY()

	FSingleParticlePhysicsProxy* Proxy = nullptr;
};

// Debug/Testing component
UCLASS(BlueprintType, meta=(BlueprintSpawnableComponent))
class NETWORKPREDICTIONEXTRAS_API UPhysicsSimpleComponent : public UNetworkPhysicsComponent
{
	GENERATED_BODY()

public:

	UPhysicsSimpleComponent();

	virtual void InitializeComponent() override;
	virtual void UninitializeComponent() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	virtual void PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker);

	UPROPERTY(Replicated)
	FNetworkPredictionAsyncProxy NetworkPredictionProxy;

	// Latest MovementState
	UPROPERTY(BlueprintReadOnly, Category="Movement")
	FSimpleNetState SimpleState;

	// Game code should write to this when locally controlling this object.
	UPROPERTY(BlueprintReadWrite, Category="Input")
	FSimpleInputCmd PendingInputCmd;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGenerateLocalInputCmd);

	// Event called when local input is needed: fill out PendingInputCmd with latest input data
	UPROPERTY(BlueprintAssignable, Category = "Input")
	FOnGenerateLocalInputCmd OnGenerateLocalInputCmd;

	UFUNCTION(BlueprintCallable, Category="Movement")
	void SetCounter(int32 NewValue);

private:

	APlayerController* GetOwnerPC() const;

	// Crude way of detecting possession change
	UPROPERTY()
	APlayerController* CachedPC = nullptr;
	bool bHasRegisteredController = false;
};