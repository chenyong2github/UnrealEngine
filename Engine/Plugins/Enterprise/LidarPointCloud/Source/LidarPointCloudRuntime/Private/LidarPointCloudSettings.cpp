// Copyright Epic Games, Inc. All Rights Reserved.

#include "LidarPointCloudSettings.h"
#include "Math/UnrealMathUtility.h"

ULidarPointCloudSettings::ULidarPointCloudSettings()
	: DuplicateHandling(ELidarPointCloudDuplicateHandling::SelectBrighter)
	, MaxDistanceForDuplicate(KINDA_SMALL_NUMBER)
	, MaxBucketSize(200)
	, NodeGridResolution(96)
	, bUseMultithreading(true)
	, MultithreadingInsertionBatchSize(500000)
	, bUseAsyncImport(true)
	, MaxImportBufferSize(52428800)
	, ExportBatchSize(5000000)
	, bPrioritizeActiveViewport(true)
	, bUseCompression(true)
	, MeshingBatchSize(128)
	, bAutoCenterOnImport(true)
	, ImportScale(100)
	, bEnable8BitLASDetection(true)
	, MaxNumberOfPointsToScanLAS(1000000)
	, MaxNumberOfPointsToScanASCII(100000)
	, ExportScale(0.01f)
	, bUseIOCaching(false)
{
}
