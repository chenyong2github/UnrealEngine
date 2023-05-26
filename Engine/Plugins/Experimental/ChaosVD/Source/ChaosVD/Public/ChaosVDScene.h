// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ChaosVDRecording.h"
#include "Containers/UnrealString.h"
#include "Containers/Map.h"
#include "UObject/ObjectMacros.h"
#include "UObject/GCObject.h"

class FChaosVDGeometryBuilder;
class AChaosVDParticleActor;
class FReferenceCollector;
class UWorld;

typedef TMap<int32, AChaosVDParticleActor*> FChaosVDParticlesByIDMap;

DECLARE_MULTICAST_DELEGATE(FChaosVDSceneUpdatedDelegate)

/** Recreates a UWorld from a recorded Chaos VD Frame */
class FChaosVDScene : public FGCObject , public TSharedFromThis<FChaosVDScene>
{
public:
	FChaosVDScene();
	virtual ~FChaosVDScene() override;

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
	void UpdateFromRecordedStepData(const int32 SolverID, const FString& SolverName, const FChaosVDStepData& InRecordedStepData, const FChaosVDSolverFrameData& InFrameData);

	void HandleNewGeometryData(const TSharedPtr<const Chaos::FImplicitObject>&, const uint32 GeometryID) const;

	void HandleEnterNewGameFrame(int32 FrameNumber, const TArray<int32>& AvailableSolversIds);

	/** Deletes all actors of the Scene and underlying UWorld */
	void CleanUpScene();

	/** Returns the a ptr to the UWorld used to represent the current recorded frame data */
	UWorld* GetUnderlyingWorld() const { return PhysicsVDWorld; };

	bool IsInitialized() const { return  bIsInitialized; }

	const TSharedPtr<FChaosVDGeometryBuilder>& GetGeometryGenerator() { return  GeometryGenerator; }

	FChaosVDGeometryDataLoaded& OnNewGeometryAvailable(){ return NewGeometryAvailableDelegate; }

	const TSharedPtr<const Chaos::FImplicitObject>* GetUpdatedGeometry(int32 GeometryID) const;

	TSharedPtr<FChaosVDRecording> LoadedRecording;

private:

	/** Creates an ChaosVDParticle actor for the Provided recorded Particle Data */
	AChaosVDParticleActor* SpawnParticleFromRecordedData(const FChaosVDParticleDebugData& InParticleData, const FChaosVDSolverFrameData& InFrameData);

	/** Returns the ID used to track this recorded particle data */
	int32 GetIDForRecordedParticleData(const FChaosVDParticleDebugData& InParticleData) const;

	/** Creates the instance of the World which will be used the recorded data*/
	UWorld* CreatePhysicsVDWorld() const;

	/** Map of ID-ChaosVDParticle Actor. Used to keep track of actor instances and be able to modify them as needed*/
	TMap<int32, FChaosVDParticlesByIDMap> ParticlesBySolverID;

	/** UWorld instance used to represent the recorded debug data */
	UWorld* PhysicsVDWorld = nullptr;

	FChaosVDSceneUpdatedDelegate SceneUpdatedDelegate;

	TSharedPtr<FChaosVDGeometryBuilder> GeometryGenerator;

	FChaosVDGeometryDataLoaded NewGeometryAvailableDelegate;

	bool bIsInitialized = false;
};
