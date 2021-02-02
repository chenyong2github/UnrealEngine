// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/**
 * Structure containing all properties required to stream from a source
 */
struct ENGINE_API FWorldPartitionStreamingSource
{
	FWorldPartitionStreamingSource()
	{}

	FWorldPartitionStreamingSource(FName InName, const FVector& InLocation, const FRotator& InRotation)
		: Name(InName)
		, Location(InLocation)
		, Rotation(InRotation)
	{}

	FName Name;
	FVector Location;
	FRotator Rotation;
};

/**
 * Interface for world partition streaming sources
 */
struct ENGINE_API IWorldPartitionStreamingSourceProvider
{
	virtual bool GetStreamingSource(FWorldPartitionStreamingSource& StreamingSource) = 0;
};