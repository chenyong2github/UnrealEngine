// Copyright Epic Games, Inc. All Rights Reserved.

#include "IrradianceCaching.h"
#include "GPULightmassModule.h"
#include "RenderGraphUtils.h"
#include "Containers/DynamicRHIResourceArray.h"
#include "RayTracingDefinitions.h"
#include "MeshPassProcessor.h"
#include "GPUSort.h"

IMPLEMENT_UNIFORM_BUFFER_STRUCT(FIrradianceCachingParameters, "IrradianceCachingParameters");

int32 GGPULightmassIrradianceCachingQuality = 128;
static FAutoConsoleVariableRef CVarGPULightmassIrradianceCachingQuality(
	TEXT("r.GPULightmass.IrradianceCaching.Quality"),
	GGPULightmassIrradianceCachingQuality,
	TEXT("\n"),
	ECVF_Default
);

float GGPULightmassIrradianceCachingSpacing = 32.0f;
static FAutoConsoleVariableRef CVarGPULightmassIrradianceCachingSpacing(
	TEXT("r.GPULightmass.IrradianceCaching.Spacing"),
	GGPULightmassIrradianceCachingSpacing,
	TEXT("\n"),
	ECVF_Default
);

FIrradianceCache::FIrradianceCache()
{
	uint32 IrradianceCacheTotalBytes = 0;

	{
		TResourceArray<FIrradianceCacheRecord> AABBMinMax;
		AABBMinMax.AddZeroed(IrradianceCacheMaxSize);
		FRHIResourceCreateInfo CreateInfo(&AABBMinMax);
		IrradianceCacheRecords = RHICreateStructuredBuffer(sizeof(FIrradianceCacheRecord), sizeof(FIrradianceCacheRecord) * IrradianceCacheMaxSize, BUF_UnorderedAccess | BUF_ShaderResource, CreateInfo);
		IrradianceCacheRecordsUAV = RHICreateUnorderedAccessView(IrradianceCacheRecords, false, false);

		IrradianceCacheTotalBytes += sizeof(FIrradianceCacheRecord) * IrradianceCacheMaxSize;
	}

	int32 HashTableSize = IrradianceCacheMaxSize * 4;

	{
		{
			TResourceArray<uint32> EmptyHashTable;
			EmptyHashTable.AddZeroed(HashTableSize);
			HashTable.Initialize(sizeof(uint32), HashTableSize, PF_R32_UINT, BUF_UnorderedAccess | BUF_ShaderResource, TEXT("ICHashTable"), &EmptyHashTable);
			HashToIndex.Initialize(sizeof(uint32), HashTableSize, PF_R32_UINT, BUF_UnorderedAccess | BUF_ShaderResource, TEXT("ICHashToIndex"), &EmptyHashTable);
			IndexToHash.Initialize(sizeof(uint32), HashTableSize, PF_R32_UINT, BUF_UnorderedAccess | BUF_ShaderResource, TEXT("ICIndexToHash"), &EmptyHashTable);

			IrradianceCacheTotalBytes += HashTable.NumBytes;
			IrradianceCacheTotalBytes += HashToIndex.NumBytes;
			IrradianceCacheTotalBytes += IndexToHash.NumBytes;
		}
		{
			TResourceArray<uint32> ZeroRecordAllocator;
			ZeroRecordAllocator.AddZeroed(1);
			RecordAllocator.Initialize(sizeof(uint32), 1, PF_R32_UINT, BUF_UnorderedAccess | BUF_ShaderResource, TEXT("ICAllocator"), &ZeroRecordAllocator);

			IrradianceCacheTotalBytes += 4;
		}
	}

	UE_LOG(LogGPULightmass, Log, TEXT("Irradiance cache initialized with %.2fMB"), IrradianceCacheTotalBytes / 1024.0f / 1024.0f);

	FIrradianceCachingParameters IrradianceCachingParameters;
	IrradianceCachingParameters.IrradianceCacheRecords = IrradianceCacheRecordsUAV;
	IrradianceCachingParameters.Quality = GGPULightmassIrradianceCachingQuality;
	IrradianceCachingParameters.Spacing = GGPULightmassIrradianceCachingSpacing;
	IrradianceCachingParameters.HashTableSize = HashTableSize;
	IrradianceCachingParameters.CacheSize = IrradianceCacheMaxSize;
	IrradianceCachingParameters.RWHashTable = HashTable.UAV;
	IrradianceCachingParameters.RWHashToIndex = HashToIndex.UAV;
	IrradianceCachingParameters.RWIndexToHash = IndexToHash.UAV;
	IrradianceCachingParameters.RecordAllocator = RecordAllocator.UAV;
	IrradianceCachingParametersUniformBuffer = TUniformBufferRef<FIrradianceCachingParameters>::CreateUniformBufferImmediate(IrradianceCachingParameters, EUniformBufferUsage::UniformBuffer_MultiFrame);
}
