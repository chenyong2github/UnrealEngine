// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NetworkPhysics.h"

#include "MockNetworkPhysics.generated.h"

class FSingleParticlePhysicsProxy;

// ========================================================================================
//	Temp WIP Mock Gameplay Code example, not final.
// ========================================================================================

// The client authoratative state. Client sends this to the server.
USTRUCT(BlueprintType)
struct FMockPhysInputCmd
{
	GENERATED_BODY()

	// Simple world vector force to be applied
	UPROPERTY(BlueprintReadWrite, Category="Input")
	FVector	Force;

	UPROPERTY(BlueprintReadWrite, Category="Input")
	float Turn=0.f;

	UPROPERTY(BlueprintReadWrite, Category="Input")
	bool bJumpedPressed = false;

	UPROPERTY(BlueprintReadWrite, Category="Input")
	bool bBrakesPressed = false;

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		Ar << Force;
		Ar << Turn;
		Ar << bJumpedPressed;
		Ar << bBrakesPressed;
		return true;
	}

	// This is only called by SP client. AP Client doesn't reconcile InputCmds because they are authoratative on them.
	// SP needs to reconcile InputCmds because of this case:
	//	-FMockPhysInputCmd::Force changes (what buttons player is pressing changes)
	//	-This causes misprediction in the controlled FNetworkPhysicsState (X,V mispredict)
	//	-But this doesn't cause correction in FMockState_GT or FMockState_PT: all that force does is apply a force to the physics proxy
	//	-If thers is no correction needed in MockState_GT or MockState_PT, then old (incorrect) inputs are used during resimulate
	//	-We could change things a bit to make it easy to apply the last recevied InputCmd to all historic inputs/frames.
	//
	//	The bad consequence here is that corrections are going to happen whenever a client InputCmd changes.
	bool ShouldReconcile(const FMockPhysInputCmd& AuthState) const
	{
		return FVector::DistSquared(Force, AuthState.Force) > 0.1f || bJumpedPressed != AuthState.bJumpedPressed || 
			bBrakesPressed != AuthState.bBrakesPressed || FMath::Abs<float>(Turn - AuthState.Turn) > 0.1f;
	}

	// Decays input for non locally controlled sims (client only)
	// disabled behind np2.InputDecay cvar for now
	void Decay(float Alpha)
	{
		Turn *= Alpha;
		if (Turn < 0.001f)
		{
			Turn = 0.f;
		}
	}

	void ToString(FStringBuilderBase& Out)
	{
		Out.Appendf(TEXT("%s. bJumpedPressed: %d. bBrakesPressed: %d. Turn: %.2f"), *Force.ToString(), (int32)bJumpedPressed, (int32)bBrakesPressed, Turn);
	}
};

template<>
struct TStructOpsTypeTraits<FMockPhysInputCmd> : public TStructOpsTypeTraitsBase2<FMockPhysInputCmd>
{
	enum
	{
		WithNetSerializer = true,
		WithNetSharedSerialization = true
	};
};

// The server authoratative state that is read only to physics thread.
// Writes from the GT can happen immediately: we don't marshal this data from PT->GT
//		-But note that the PT seeing the new value will be delayed
//		-We can tell you when a GT write took effect on the PT
//		-We could support a view/API that was like "what was this value on PT on this frame" if needed
//
// I don't think this state needs to cause reconcile itself? Hmm maybe though
USTRUCT(BlueprintType)
struct FMockState_GT
{
	GENERATED_BODY()

	// Actually used by AsyncTick to scale force applied
	UPROPERTY(BlueprintReadWrite, Category="Mock Object")
	float ForceMultiplier = 125000.f;

	// Arbitrary data that doesn't affect sim but could still trigger rollback
	UPROPERTY(BlueprintReadWrite, Category="Mock Object")
	int32 RandValue = 0;

	bool ShouldReconcile(const FMockState_GT& AuthState) const
	{
		return ForceMultiplier != AuthState.ForceMultiplier || RandValue != AuthState.RandValue;
	}

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		Ar << ForceMultiplier;
		Ar << RandValue;
		return true;
	}
};

template<>
struct TStructOpsTypeTraits<FMockState_GT> : public TStructOpsTypeTraitsBase2<FMockState_GT>
{
	enum
	{
		WithNetSerializer = true,
		WithNetSharedSerialization = true
	};
};

// The server authoratative state that is writable by the physics thread.
// Writes from the GT to this state are deferred until they make round trip through physics thread
USTRUCT(BlueprintType)
struct FMockState_PT
{
	GENERATED_BODY()

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

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		Ar << JumpCooldownMS;
		Ar << JumpCount;
		Ar << CheckSum;	
		Ar << RecoveryFrame;
		return true;
	}

	bool ShouldReconcile(const FMockState_PT& AuthState) const
	{
		return JumpCooldownMS != AuthState.JumpCooldownMS || JumpCount != AuthState.JumpCount || RecoveryFrame != AuthState.RecoveryFrame;
	}
};

template<>
struct TStructOpsTypeTraits<FMockState_PT> : public TStructOpsTypeTraitsBase2<FMockState_PT>
{
	enum
	{
		WithNetSerializer = true,
		WithNetSharedSerialization = true
	};
};

USTRUCT()
struct FMockFutureClientInput
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Frame = INDEX_NONE;

	UPROPERTY()
	FMockPhysInputCmd InputCmd;
};

// For now this struct holds "everything". The actor registers a pointer to this struct as the "managed state" with FMockObjectManager.
// This is effectively the Physics Thread Object. This entire thing is marshalled to PT and does the AsyncTick.
USTRUCT(BlueprintType)
struct FMockManagedState
{
	GENERATED_BODY()

	void AsyncTick(UWorld* World, Chaos::FPhysicsSolver* Solver, const float DeltaSeconds, const int32 SimulationFrame, const int32 LocalStorageFrame);

	UPROPERTY()
	int32 Frame = INDEX_NONE;
		
	UPROPERTY()
	FMockPhysInputCmd InputCmd;
	
	UPROPERTY()
	FMockState_GT GT_State;

	UPROPERTY()
	FMockState_PT PT_State;

	// What PC is controlling this object
	UPROPERTY(NotReplicated)
	APlayerController* PC = nullptr;

	// What physics proxy this is controlling
	FSingleParticlePhysicsProxy* Proxy = nullptr;

	// Future Inputs, to help more accurately predict/repredict SP clients
	// FIXME: this is being repped to AP clients unnecessarilly but we cant have rep conditions on properties in structs
	// pulling this out into a seperate struct is an option but complicates the rest of the code. Since we expect to 
	// re-generalize this stuff, we will consider these problems then
	UPROPERTY()
	TArray<FMockFutureClientInput> FutureInputs;

	float InputDecay = 0.f;

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		Ar << Frame;
		InputCmd.NetSerialize(Ar, Map, bOutSuccess);
		GT_State.NetSerialize(Ar, Map, bOutSuccess);
		PT_State.NetSerialize(Ar, Map, bOutSuccess);
		bOutSuccess = true;
		return true;
	}
};

template<>
struct TStructOpsTypeTraits<FMockManagedState> : public TStructOpsTypeTraitsBase2<FMockManagedState>
{
	enum
	{
		WithNetSerializer = true,
		WithNetSharedSerialization = true
	};
};



// This is just wrapping FMockAsyncObjectManagerCallback 
// I just wanted to keep the chaos stuff bs in the cpp file and define a clean interface for the game code
class FMockObjectManager : public INetworkPhysicsSubsystem
{
public:

	static FName GetName() { return FName("MockObjManager"); }
	static FMockObjectManager* Get(UWorld* World);

	FMockObjectManager(UWorld* World);
	~FMockObjectManager();

	void RegisterManagedMockObject(FMockManagedState* ReplicatedState, FMockManagedState* InState, FMockManagedState* OutState);
	void UnregisterManagedMockObject(FMockManagedState* ReplicatedState, FMockManagedState* InState, FMockManagedState* OutState);

	void PostNetRecv(UWorld* World, int32 FrameOffset, int32 LastProcessedFrame) override;
	void PreNetSend(UWorld* World, float DeltaSeconds) override;

private:

	TArray<FMockManagedState*> ReplicatedMockManagedStates;
	TArray<FMockManagedState*> InMockManagedStates;
	TArray<FMockManagedState*> OutMockManagedStates;
	class FMockAsyncObjectManagerCallback* AsyncCallback = nullptr;
};

// -----------------------------------------------------




UCLASS(BlueprintType, meta=(BlueprintSpawnableComponent))
class NETWORKPREDICTIONEXTRAS_API UNetworkPhysicsComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UNetworkPhysicsComponent();

	virtual void InitializeComponent() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;

	UPROPERTY(Replicated, transient)
	FNetworkPhysicsState NetworkPhysicsState;

	// ----------------------------------------------------------------
	// API for setting pending input cmd (used by local controller)
	// ----------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Mock Input")
	FMockPhysInputCmd GetPendingInputCmd() const { return InManagedState.InputCmd; }

	UFUNCTION(BlueprintCallable, Category = "Mock Input")
	void SetPendingInputCmd(const FMockPhysInputCmd& In) { InManagedState.InputCmd = In; }

	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGeneratedLocalInputCmd);
	
	UPROPERTY(BlueprintAssignable, Category = "Components|Activation")
	FOnGeneratedLocalInputCmd OnGeneratedLocalInputCmd;

	// ----------------------------------------------------------------
	// API for setting the GT owned state
	//	Its probably wrong to let clients mod this without a PredictionKey mechanism
	// ----------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Mock State")
	FMockState_GT GetMockState_GT() const { return InManagedState.GT_State; }

	UFUNCTION(BlueprintCallable, Category = "Mock State")
	void SetMockState_GT(const FMockState_GT& In) { InManagedState.GT_State = In; }

	// ----------------------------------------------------------------
	// API for setting the PT owned state
	// ----------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Mock Input")
	FMockState_PT GetMockState_PT() const { return OutManagedState.PT_State; }

	// ----------------------------------------------------------------
	// Misc API
	// ----------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Network Physics")
	int32 GetNetworkPredictionLOD() const { return NetworkPhysicsState.LocalLOD; }

protected:

	// Managed state should not be publically exposed
	UPROPERTY(Replicated, transient)
	FMockManagedState ReplicatedManagedState;

	UPROPERTY(transient)
	FMockManagedState InManagedState;

	UPROPERTY(transient)
	FMockManagedState OutManagedState;

	APlayerController* GetOwnerPC() const;

};