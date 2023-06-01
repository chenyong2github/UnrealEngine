// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PhysicsReplication.h
	Manage replication of physics bodies
=============================================================================*/

#pragma once

#include "Engine/EngineTypes.h"
#include "Engine/ReplicatedState.h"
#include "PhysicsReplicationInterface.h"
#include "Physics/PhysicsInterfaceDeclares.h"
#include "PhysicsProxy/SingleParticlePhysicsProxyFwd.h"
#include "Chaos/Particles.h"

namespace Chaos
{
	struct FSimCallbackInput;
}

namespace CharacterMovementCVars
{
	extern ENGINE_API int32 SkipPhysicsReplication;
	extern ENGINE_API float NetPingExtrapolation;
	extern ENGINE_API float NetPingLimit;
	extern ENGINE_API float ErrorPerLinearDifference;
	extern ENGINE_API float ErrorPerAngularDifference;
	extern ENGINE_API float ErrorAccumulationSeconds;
	extern ENGINE_API float ErrorAccumulationDistanceSq;
	extern ENGINE_API float ErrorAccumulationSimilarity;
	extern ENGINE_API float MaxLinearHardSnapDistance;
	extern ENGINE_API float MaxRestoredStateError;
	extern ENGINE_API float PositionLerp;
	extern ENGINE_API float LinearVelocityCoefficient;
	extern ENGINE_API float AngleLerp;
	extern ENGINE_API float AngularVelocityCoefficient;
	extern ENGINE_API int32 AlwaysHardSnap;
	extern ENGINE_API int32 AlwaysResetPhysics;
	extern ENGINE_API int32 ApplyAsyncSleepState;
}

#if !UE_BUILD_SHIPPING
namespace PhysicsReplicationCVars
{
	extern ENGINE_API int32 LogPhysicsReplicationHardSnaps;
}
#endif

class FPhysScene_PhysX;

struct FReplicatedPhysicsTarget
{
	FReplicatedPhysicsTarget() :
		ArrivedTimeSeconds(0.0f),
		AccumulatedErrorSeconds(0.0f),
		ServerFrame(0)
	{ }

	/** The target state replicated by server */
	FRigidBodyState TargetState;

	/** The bone name used to find the body */
	FName BoneName;

	/** Client time when target state arrived */
	float ArrivedTimeSeconds;

	/** Physics sync error accumulation */
	float AccumulatedErrorSeconds;

	/** Correction values from previous update */
	FVector PrevPosTarget;
	FVector PrevPos;

	/** ServerFrame this target was replicated on (must be converted to local frame prior to client-side use) */
	int32 ServerFrame;

#if !UE_BUILD_SHIPPING
	FDebugFloatHistory ErrorHistory;
#endif
};

struct ErrorCorrectionData
{
	float LinearVelocityCoefficient;
	float AngularVelocityCoefficient;
	float PositionLerp;
	float AngleLerp;
};

/** Final computed desired state passed into the physics sim */
struct FAsyncPhysicsDesiredState
{
	FTransform WorldTM;
	FVector LinearVelocity;
	FVector AngularVelocity;
	Chaos::FSingleParticlePhysicsProxy* Proxy;
	TOptional<ErrorCorrectionData> ErrorCorrection;
	bool bShouldSleep;
};

struct FBodyInstance;
struct FRigidBodyErrorCorrection;
class UWorld;
class UPrimitiveComponent;
class FPhysicsReplicationAsyncCallback;
struct FAsyncPhysicsRepCallbackData;

class ENGINE_API FPhysicsReplication : public IPhysicsReplication
{
public:
	FPhysicsReplication(FPhysScene* PhysScene);
	virtual ~FPhysicsReplication();

	/** Helper method so the Skip Replication CVar can be check elsewhere (including game extensions to this class) */
	static bool ShouldSkipPhysicsReplication();

	/** Tick and update all body states according to replicated targets */
	virtual void Tick(float DeltaSeconds) override;

	/** Sets the latest replicated target for a body instance */
	UE_DEPRECATED(5.1, "SetReplicatedTarget now takes the ServerFrame.  Please update calls and overloads.")
	virtual void SetReplicatedTarget(UPrimitiveComponent* Component, FName BoneName, const FRigidBodyState& ReplicatedTarget) { SetReplicatedTarget(Component, BoneName, ReplicatedTarget, 0); }
	virtual void SetReplicatedTarget(UPrimitiveComponent* Component, FName BoneName, const FRigidBodyState& ReplicatedTarget, int32 ServerFrame) override;

	/** Remove the replicated target*/
	virtual void RemoveReplicatedTarget(UPrimitiveComponent* Component) override;

	/** Get the resim frame (min server frame from the targets) */
	virtual int32 GetResimFrame() const override;

	/** Set the resim frame for replication */
	virtual void SetResimFrame(const int32 InResimFrame) override;

protected:

	/** Update the physics body state given a set of replicated targets */
	virtual void OnTick(float DeltaSeconds, TMap<TWeakObjectPtr<UPrimitiveComponent>, FReplicatedPhysicsTarget>& ComponentsToTargets);
	virtual void OnTargetRestored(TWeakObjectPtr<UPrimitiveComponent> Component, const FReplicatedPhysicsTarget& Target) {}
	virtual void OnSetReplicatedTarget(UPrimitiveComponent* Component, FName BoneName, const FRigidBodyState& ReplicatedTarget, int32 ServerFrame, FReplicatedPhysicsTarget& Target) {}

	/** Called when a dynamic rigid body receives a physics update */
	virtual bool ApplyRigidBodyState(float DeltaSeconds, FBodyInstance* BI, FReplicatedPhysicsTarget& PhysicsTarget, const FRigidBodyErrorCorrection& ErrorCorrection, const float PingSecondsOneWay, int32 LocalFrame, int32 NumPredictedFrames);
	virtual bool ApplyRigidBodyState(float DeltaSeconds, FBodyInstance* BI, FReplicatedPhysicsTarget& PhysicsTarget, const FRigidBodyErrorCorrection& ErrorCorrection, const float PingSecondsOneWay, bool* bDidHardSnap = nullptr); // deprecated path with no localframe/numpredicted

	UWorld* GetOwningWorld();
	const UWorld* GetOwningWorld() const;

private:

	/** Get the ping from this machine to the server */
	float GetLocalPing() const;

	/** Get the ping from  */
	float GetOwnerPing(const AActor* const Owner, const FReplicatedPhysicsTarget& Target) const;

	static void ApplyAsyncDesiredState(float DeltaSeconds, const FAsyncPhysicsRepCallbackData* Input);

private:
	TMap<TWeakObjectPtr<UPrimitiveComponent>, FReplicatedPhysicsTarget> ComponentToTargets;
	FPhysScene* PhysScene;

	FPhysicsReplicationAsyncCallback* AsyncCallback;
	
	void PrepareAsyncData_External(const FRigidBodyErrorCorrection& ErrorCorrection);	//prepare async data for writing. Call on external thread (i.e. game thread)
	FAsyncPhysicsRepCallbackData* CurAsyncData;	//async data being written into before we push into callback
	friend FPhysicsReplicationAsyncCallback;

	int32 ResimFrame = INDEX_NONE;

};