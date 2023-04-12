// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "UObject/ObjectMacros.h"
#include "Containers/UnrealString.h"
#include "ChaosVDRecording.generated.h"

DECLARE_MULTICAST_DELEGATE(FChaosVDRecordingUpdated)

UENUM()
enum class EChaosVDParticleType : uint8
{
	Static,
	Kinematic,
	Rigid,
	Clustered,
	StaticMesh,
	SkeletalMesh,
	GeometryCollection,
	Unknown
};

UENUM()
enum class EChaosVDParticleState : int8
{
	Uninitialized = 0,
	Sleeping = 1,
	Kinematic = 2,
	Static = 3,
	Dynamic = 4,

	Count
};

UENUM()
enum class EChaosVDSolverType : uint32
{
	Rigid
};

USTRUCT()
struct FChaosVDParticleDebugData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category= "Chaos Visual Debugger Data")
	EChaosVDParticleType ParticleType;
	UPROPERTY(VisibleAnywhere, Category= "Chaos Visual Debugger Data")
	FString DebugName;
	UPROPERTY(VisibleAnywhere, Category= "Chaos Visual Debugger Data")
	int32 ParticleIndex;
	UPROPERTY(VisibleAnywhere, Category= "Chaos Visual Debugger Data")
	FVector Position;
	UPROPERTY(VisibleAnywhere, Category= "Chaos Visual Debugger Data")
	FQuat Rotation;
	UPROPERTY(VisibleAnywhere, Category= "Chaos Visual Debugger Data")
	FVector Velocity;
	UPROPERTY(VisibleAnywhere, Category= "Chaos Visual Debugger Data")
	FVector AngularVelocity;
	UPROPERTY(VisibleAnywhere, Category= "Chaos Visual Debugger Data")
	EChaosVDParticleState ParticleState;
};

struct FChaosVDStepData
{
	TArray<FChaosVDParticleDebugData> RecordedParticles;
};

struct CHAOSVDRUNTIME_API FChaosVDSolverFrameData
{
	FString DebugName;
	int32 SolverID;
	TArray<FChaosVDStepData, TInlineAllocator<16>> SolverSteps;
};

enum class EChaosVDFrameLoadState
{
	Unloaded,
	Loaded,
	Buffering,
	Unknown
};

/**
 * Struct that represents a recorded Physics simulation.
 * It is currently populated while analyzing a Trace session
 */
struct CHAOSVDRUNTIME_API FChaosVDRecording
{
	/** Returns the current available recorded frames per recorded solver */
	int32 GetAvailableFramesNumber(const int32 SolverID) const;
	/** Returns the current available recorded solvers number */
	int32 GetAvailableSolversNumber() const { return RecordedFramesDataPerSolver.Num();}
	
	const TMap<int32, TArray<FChaosVDSolverFrameData>>& GetAvailableSolvers() const { return RecordedFramesDataPerSolver; }

	/** Returns a ptr to the recorded frame data for a specific solver -  Do not store as it is a reference to the element in the array */
	FChaosVDSolverFrameData* GetFrameForSolver(const int32 SolverID, const int32 FrameNumber);

	/** Adds a Frame Data for a specific Solver ID. Creates a solver entry if it does not exist */
	void AddFrameForSolver(const int32 SolverID, FChaosVDSolverFrameData&& InFrameData);

	/** Returns the current frame state of a frame used to determine if it is ready for use*/
	EChaosVDFrameLoadState GetFrameState(const int32 SolverID, const int32 FrameNumber);

	/** Called each time the recording changes - Mainly when a new frame is added from the Trace analysis */
	FChaosVDRecordingUpdated& OnRecordingUpdated() { return RecordingUpdatedDelegate; };

	/** Session name of the trace session used to re-build this recording */
	FString SessionName;

protected:

	TMap<int32, EChaosVDFrameLoadState> AvailableFramesState;
	TMap<int32, TArray<FChaosVDSolverFrameData>> RecordedFramesDataPerSolver;
	FChaosVDRecordingUpdated RecordingUpdatedDelegate;
};
