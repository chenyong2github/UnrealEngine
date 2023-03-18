// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ChaosVDRecording.h"
#include "Containers/UnrealString.h"
#include "Containers/Map.h"
#include "UObject/ObjectMacros.h"
#include "UObject/GCObject.h"

class AChaosVDParticleActor;
class FReferenceCollector;
class UWorld;

DECLARE_MULTICAST_DELEGATE(FChaosVDSceneUpdatedDelegate)

/** Recreates a UWorld from a recorded Chaos VD Frame */
class FChaosVDScene : public FGCObject
{
public:

	void Initialize();
	void DeInitialize();

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override
	{
		return TEXT("FChaosVDScene");
	}

	/** Called each time this Scene is modified */
	FChaosVDSceneUpdatedDelegate& OnSceneUpdated() { return SceneUpdatedDelegate; }

	/** Updates, Adds and Remove actors to match the provided Step Data */
	void UpdateFromRecordedStepData(EChaosVDSolverType SolverID, const FChaosVDStepData& InRecordedStepData);

	/** Deletes all actors of the Scene and underlying UWorld */
	void CleanUpScene();

	/** Returns the a ptr to the UWorld used to represent the current recorded frame data */
	UWorld* GetUnderlyingWorld() const { return PhysicsVDWorld; };

private:

	/** Creates an ChaosVDParticle actor for the Provided recorded Particle Data */
	AChaosVDParticleActor* SpawnParticleFromRecordedData(const FChaosVDParticleDebugData& InParticleData);

	/** Returns the ID used to track this recorded particle data */
	uint32 GetIDForRecordedParticleData(const FChaosVDParticleDebugData& InParticleData) const;

	/** Creates the instance of the World which will be used the recorded data*/
	UWorld* CreatePhysicsVDWorld() const;

	/** Map of ID-ChaosVDParticle Actor. Used to keep track of actor instances and be able to modify them as needed*/
	TMap<uint32, AChaosVDParticleActor*> ParticleVDInstancesByID;

	/** UWorld instance used to represent the recorded debug data */
	UWorld* PhysicsVDWorld = nullptr;

	FChaosVDSceneUpdatedDelegate SceneUpdatedDelegate;

	bool bIsInitialized = false;
};
