// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "GameFramework/Actor.h"
#include "WorldPartition/WorldPartitionStreamingSource.h"
#include "WorldPartitionReplay.generated.h"

struct FWorldPartitionReplayStreamingSource : public FWorldPartitionStreamingSource
{
	FWorldPartitionReplayStreamingSource()
	{

	}

	FWorldPartitionReplayStreamingSource(const FWorldPartitionStreamingSource& InStreamingSource)
		: FWorldPartitionStreamingSource(InStreamingSource.Name, InStreamingSource.Location, InStreamingSource.Rotation, InStreamingSource.TargetState, InStreamingSource.bBlockOnSlowLoading, InStreamingSource.Priority, InStreamingSource.Velocity)
	{
	}

	friend FArchive& operator<<(FArchive& Ar, FWorldPartitionReplayStreamingSource& StreamingSource);
};

struct FWorldPartitionReplaySample
{
	FWorldPartitionReplaySample(AWorldPartitionReplay* InReplay);
			
	friend FArchive& operator<<(FArchive& Ar, FWorldPartitionReplaySample& StreamingSource);
	
	TArray<int32> StreamingSourceNameIndices;
	TArray<FWorldPartitionReplayStreamingSource> StreamingSources;
	class AWorldPartitionReplay* Replay = nullptr;
	float TimeSeconds = 0.f;
};

/**
 * Actor used to record world partition replay data (streaming sources for now)
 */
UCLASS(notplaceable, transient)
class ENGINE_API AWorldPartitionReplay : public AActor
{
	GENERATED_UCLASS_BODY()

public:
	static void Initialize(UWorld* World);
	static bool IsEnabled(UWorld* World);

	virtual void BeginPlay() override;
	virtual void RewindForReplay() override;
	virtual void PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker) override;
		
	bool IsEnabled() const { return bEnabled; }
	bool GetReplayStreamingSources(TArray<FWorldPartitionStreamingSource>& OutStreamingSources);
		
private:
	friend FArchive& operator<<(FArchive& Ar, FWorldPartitionReplaySample& StreamingSource);
	friend struct FWorldPartitionReplaySample;
	TArray<FWorldPartitionStreamingSource> GetRecordingStreamingSources() const;

	UPROPERTY(Transient, Replicated)
	TArray<FName> StreamingSourceNames;

	TArray<FWorldPartitionReplaySample> ReplaySamples;
	bool bEnabled = false;
};

