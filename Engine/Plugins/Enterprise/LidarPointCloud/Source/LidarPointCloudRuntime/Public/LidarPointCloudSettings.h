// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "LidarPointCloudSettings.generated.h"

UENUM(BlueprintType)
enum class ELidarPointCloudDuplicateHandling : uint8
{
	/** Keeps any duplicates found */
	Ignore,
	/** Keeps the first point and skips any further duplicates */
	SelectFirst,
	/** Selects the brightest of the duplicates */
	SelectBrighter
};

UCLASS(config=Engine, defaultconfig)
class LIDARPOINTCLOUDRUNTIME_API ULidarPointCloudSettings : public UObject
{
	GENERATED_BODY()

public:
	/** Determines how to handle duplicate points (distance < 0.0001). */
	UPROPERTY(config, EditAnywhere, Category=Octree)
	ELidarPointCloudDuplicateHandling DuplicateHandling;

	/** Maximum distance between points, within which they are considered to be duplicates */
	UPROPERTY(config, EditAnywhere, Category=Octree, meta = (ClampMin = "0"))
	float MaxDistanceForDuplicate;

	/**
	 * Maximum number of unallocated points to keep inside the node before they need to be converted in to a full child node.
	 * Lower values will provide finer LOD control at the expense of system RAM and CPU time.
	 */
	UPROPERTY(config, EditAnywhere, Category=Octree)
	int32 MaxBucketSize;

	/**
	 * Virtual grid resolution to divide the node into.
	 * Lower values will provide finer LOD control at the expense of system RAM and CPU time.
	 */
	UPROPERTY(config, EditAnywhere, Category=Octree)
	int32 NodeGridResolution;

	/** Enabling this will allow usage of multiple threads during import and processing. */
	UPROPERTY(config, EditAnywhere, Category=Performance)
	bool bUseMultithreading;

	/** Determines the maximum amount of points to process in a single batch when using multi-threading. */
	UPROPERTY(config, EditAnywhere, Category=Performance)
	int32 MultithreadingInsertionBatchSize;

	/** Enabling this will allow editor to import the point clouds in the background, without blocking the main thread. */
	UPROPERTY(config, EditAnywhere, Category=Performance)
	bool bUseAsyncImport;

	/** Determines the maximum size of the buffer to use during importing. */
	UPROPERTY(config, EditAnywhere, Category=Performance)
	int32 MaxImportBufferSize;

	/** Determines the maximum size of the buffer to use during exporting. */
	UPROPERTY(config, EditAnywhere, Category=Performance)
	int32 ExportBatchSize;

	/**
	 * Enabling this will allocate larger portion of the available point budget to the viewport with focus.
	 * May improve asset editing experience, if the scenes are busy.
	 * Disable, if you are experiencing visual glitches.
	 */
	UPROPERTY(config, EditAnywhere, Category=Performance)
	bool bPrioritizeActiveViewport;

	/**
	 * Enabling this will compress data when saving the assets.
	 * May introduce delay when streaming points on slower machines.
	 */
	UPROPERTY(config, EditAnywhere, Category=IO)
	bool bUseCompression;

	/** Affects the size of per-thread data for the meshing algorithm. */
	UPROPERTY(config, EditAnywhere, Category=Collision)
	int32 MeshingBatchSize;

	/**
	 * Automatically centers the cloud on import.
	 * Caution: Preserving original coordinates may cause noticeable precision loss, if the values are too large.
	 * Should you experience point 'banding' effect, please re-import your cloud with centering enabled.
	 */
	UPROPERTY(config, EditAnywhere, Category=Import)
	bool bAutoCenterOnImport;

	/** Scale to apply during import */
	UPROPERTY(config, EditAnywhere, Category=Import, meta = (ClampMin = "0.0001"))
	float ImportScale;

	/**
	 * Enables 8-bit color detection in LAS files.
	 * This will slow down the import a little.
	 */
	UPROPERTY(config, EditAnywhere, Category=Import, meta = (DisplayName = "Enable 8-Bit LAS Detection"))
	bool bEnable8BitLASDetection;

	/** Determines the maximum number of points to scan when analyzing the LAS data */
	UPROPERTY(config, EditAnywhere, Category=Import, meta = (DisplayName = "Max Number Of Points To Scan (LAS)"))
	int32 MaxNumberOfPointsToScanLAS;

	/** Determines the maximum number of points to scan when analyzing the ASCII data */
	UPROPERTY(config, EditAnywhere, Category=Import, meta = (DisplayName = "Max Number Of Points To Scan (ASCII)"))
	int32 MaxNumberOfPointsToScanASCII;

	/** Scale to apply during export. In most cases, this should be equal to an inverted ImportScale */
	UPROPERTY(config, EditAnywhere, Category=Export, meta = (ClampMin = "0.0001"))
	float ExportScale;

	/**
	 * Enabling this will store pre-processed version of the import data as a *.tmp file, and will attempt to use it upon re-import.
	 * Useful for faster debug iteration on large and slow to import (especially ASCII based) cloud assets.
	 */
	UPROPERTY(config, EditAnywhere, Category=Debug)
	bool bUseIOCaching;

public:
	ULidarPointCloudSettings();
};