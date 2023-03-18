// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "UObject/ObjectMacros.h"
#include "Containers/UnrealString.h"

#include "ChaosVDRecording.generated.h"

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
enum class EChaosVDSolverType
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
};

USTRUCT()
struct FChaosVDStepData
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FChaosVDParticleDebugData> RecordedParticles;
};

USTRUCT()
struct FChaosVDSolverData
{
	GENERATED_BODY()

	UPROPERTY()
	EChaosVDSolverType SolverType;

	UPROPERTY()
	TArray<FChaosVDStepData> SolverSteps;
};

USTRUCT()
struct FChaosVDEventData
{
	GENERATED_BODY()

	UPROPERTY()
	FString EventID;

	UPROPERTY()
	TArray<uint8> SerializedEventData;
};

USTRUCT()
struct CHAOSVDRUNTIME_API FChaosVDFrameData
{
	GENERATED_BODY()

	UPROPERTY()
	TMap<EChaosVDSolverType, FChaosVDSolverData> RecordedSolvers;

	UPROPERTY()
	TArray<FChaosVDEventData> RecordedEvents;

	FChaosVDSolverData& GetSolverData(EChaosVDSolverType SolverType) { return RecordedSolvers.Contains(SolverType) ? RecordedSolvers[SolverType] : RecordedSolvers.Add(SolverType,FChaosVDSolverData()); }

	//FChaosVDSolverData& AddSolverData(EChaosVDSolverType SolverType) { return RecordedSolvers.Contains(SolverType) ? RecordedSolvers[SolverType] : RecordedSolvers.Add(SolverType,FChaosVDSolverData()); }
};

USTRUCT()
struct FChaosVDRecordingHeader
{
	GENERATED_BODY()

	UPROPERTY()
	bool bIsRealTimeRecording = false;
};

USTRUCT()
struct CHAOSVDRUNTIME_API FChaosVDRecording 
{
	GENERATED_BODY()

	UPROPERTY()
	FChaosVDRecordingHeader RecordingHeader;

	UPROPERTY()
	TArray<FChaosVDFrameData> RecordedFramesData;

	FChaosVDFrameData& GetCurrentFrame() { return RecordedFramesData.Num() ? RecordedFramesData.Last() : AddFrame(); }

	FChaosVDFrameData& AddFrame();
};
