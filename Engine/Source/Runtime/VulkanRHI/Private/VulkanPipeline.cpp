// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VulkanPipeline.cpp: Vulkan device RHI implementation.
=============================================================================*/

#include "VulkanRHIPrivate.h"
#include "VulkanPipeline.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "VulkanPendingState.h"
#include "VulkanContext.h"
#include "GlobalShader.h"
#include "VulkanLLM.h"
#include "Misc/ScopeRWLock.h"

#define LRU_DEBUG 0
#if !UE_BUILD_SHIPPING
#define LRUPRINT(...) FPlatformMisc::LowLevelOutputDebugStringf(__VA_ARGS__)
#if LRU_DEBUG
#define LRUPRINT_DEBUG(...) FPlatformMisc::LowLevelOutputDebugStringf(__VA_ARGS__)
#endif
#else
#define LRUPRINT(...) do{}while(0)
#endif

#ifndef LRUPRINT_DEBUG
#define LRUPRINT_DEBUG(...) do{}while(0)
#endif



#if PLATFORM_ANDROID
#define LRU_MAX_PIPELINE_SIZE 10
#define LRU_PIPELINE_CAPACITY 2048
#else
#define LRU_MAX_PIPELINE_SIZE 512 //needs to be super high to work on pc.
#define LRU_PIPELINE_CAPACITY 8192
#endif



#if !UE_BUILD_SHIPPING
static TAtomic<uint64> SGraphicsRHICount;
static TAtomic<uint64> SPipelineCount;
static TAtomic<uint64> SPipelineGfxCount;
#endif

static const double HitchTime = 1.0 / 1000.0;

static FCriticalSection FVulkanShaderHandleCS;


TAutoConsoleVariable<int32> CVarPipelineDebugForceEvictImmediately(
	TEXT("r.Vulkan.PipelineDebugForceEvictImmediately"),
	0,
	TEXT("1: Force all created PSOs to be evicted immediately. Only for debugging"),
	ECVF_RenderThreadSafe | ECVF_ReadOnly);



TAutoConsoleVariable<int32> CVarPipelineLRUCacheEvictBinaryPreloadScreen(
	TEXT("r.Vulkan.PipelineLRUCacheEvictBinaryPreloadScreen"),
	0,
	TEXT("1: Use a preload screen while loading preevicted PSOs ala r.Vulkan.PipelineLRUCacheEvictBinary"),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarEnableLRU(
	TEXT("r.Vulkan.EnablePipelineLRUCache"),
	0,
	TEXT("Pipeline LRU cache.\n")
	TEXT("0: disable LRU\n")
	TEXT("1: Enable LRU"),
	ECVF_RenderThreadSafe | ECVF_ReadOnly);

TAutoConsoleVariable<int32> CVarPipelineLRUCacheEvictBinary(
	TEXT("r.Vulkan.PipelineLRUCacheEvictBinary"),
	0,
	TEXT("0: create pipelines in from the binary PSO cache and binary shader cache and evict them only as it fills up.\n")
	TEXT("1: don't create pipelines....just immediately evict them"),
	ECVF_RenderThreadSafe | ECVF_ReadOnly);


TAutoConsoleVariable<int32> CVarLRUMaxPipelineSize(
	TEXT("r.Vulkan.PipelineLRUSize"),
	LRU_MAX_PIPELINE_SIZE * 1024 * 1024,
	TEXT("Maximum size of shader memory ."),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarLRUPipelineCapacity(
	TEXT("r.Vulkan.PipelineLRUCapactiy"),
	LRU_PIPELINE_CAPACITY,
	TEXT("Maximum no. of PSOs in LRU."),
	ECVF_RenderThreadSafe| ECVF_ReadOnly);





static TAutoConsoleVariable<int32> GEnablePipelineCacheLoadCvar(
	TEXT("r.Vulkan.PipelineCacheLoad"),
	1,
	TEXT("0 to disable loading the pipeline cache")
	TEXT("1 to enable using pipeline cache")
);

static TAutoConsoleVariable<int32> GPipelineCacheFromShaderPipelineCacheCvar(
	TEXT("r.Vulkan.PipelineCacheFromShaderPipelineCache"),
	PLATFORM_ANDROID && !(PLATFORM_LUMIN || PLATFORM_LUMINGL4),
	TEXT("0 look for a pipeline cache in the normal locations with the normal names.")
	TEXT("1 tie the vulkan pipeline cache to the shader pipeline cache, use the PSOFC guid as part of the filename, etc."),
	ECVF_ReadOnly
);


static int32 GEnablePipelineCacheCompression = 1;
static FAutoConsoleVariableRef GEnablePipelineCacheCompressionCvar(
	TEXT("r.Vulkan.PipelineCacheCompression"),
	GEnablePipelineCacheCompression,
	TEXT("Enable/disable compression on the Vulkan pipeline cache disk file\n"),
	ECVF_Default | ECVF_RenderThreadSafe
);


static int32 GVulkanPSOForceSingleThreaded = 0;
static FAutoConsoleVariableRef GVulkanPSOForceSingleThreadedCVar(
	TEXT("r.Vulkan.ForcePSOSingleThreaded"),
	GVulkanPSOForceSingleThreaded,
	TEXT("Enable to force singlethreaded creation of PSOs. Only intended as a workaround for buggy drivers\n"),
	ECVF_ReadOnly | ECVF_RenderThreadSafe
);



template <typename TRHIType, typename TVulkanType>
static inline FSHAHash GetShaderHash(TRHIType* RHIShader)
{
	if (RHIShader)
	{
		const TVulkanType* VulkanShader = ResourceCast<TRHIType>(RHIShader);
		const FVulkanShader* Shader = static_cast<const FVulkanShader*>(VulkanShader);
		check(Shader);
		return Shader->GetCodeHeader().SourceHash;
	}

	FSHAHash Dummy;
	return Dummy;
}

static inline FSHAHash GetShaderHashForStage(const FGraphicsPipelineStateInitializer& PSOInitializer, ShaderStage::EStage Stage)
{
	switch (Stage)
	{
	case ShaderStage::Vertex:		return GetShaderHash<FRHIVertexShader, FVulkanVertexShader>(PSOInitializer.BoundShaderState.VertexShaderRHI);
	case ShaderStage::Pixel:		return GetShaderHash<FRHIPixelShader, FVulkanPixelShader>(PSOInitializer.BoundShaderState.PixelShaderRHI);
#if VULKAN_SUPPORTS_GEOMETRY_SHADERS
	case ShaderStage::Geometry:		return GetShaderHash<FRHIGeometryShader, FVulkanGeometryShader>(PSOInitializer.BoundShaderState.GeometryShaderRHI);
#endif
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
	case ShaderStage::Hull:			return GetShaderHash<FRHIHullShader, FVulkanHullShader>(PSOInitializer.BoundShaderState.HullShaderRHI);
	case ShaderStage::Domain:		return GetShaderHash<FRHIDomainShader, FVulkanDomainShader>(PSOInitializer.BoundShaderState.DomainShaderRHI);
#endif
	default:			check(0);	break;
	}

	FSHAHash Dummy;
	return Dummy;
}

FVulkanPipeline::FVulkanPipeline(FVulkanDevice* InDevice)
	: Device(InDevice)
	, Pipeline(VK_NULL_HANDLE)
	, Layout(nullptr)
{
#if !UE_BUILD_SHIPPING
	SPipelineCount++;
#endif
}

FVulkanPipeline::~FVulkanPipeline()
{
#if !UE_BUILD_SHIPPING
	SPipelineCount--;
#endif	
	if (Pipeline != VK_NULL_HANDLE)
	{
		Device->GetDeferredDeletionQueue().EnqueueResource(VulkanRHI::FDeferredDeletionQueue2::EType::Pipeline, Pipeline);
		Pipeline = VK_NULL_HANDLE;
	}
	/* we do NOT own Layout !*/
}

FVulkanComputePipeline::FVulkanComputePipeline(FVulkanDevice* InDevice)
	: FVulkanPipeline(InDevice)
	, ComputeShader(nullptr)
{
	INC_DWORD_STAT(STAT_VulkanNumComputePSOs);
}

FVulkanComputePipeline::~FVulkanComputePipeline()
{
	if (ComputeShader)
	{
		ComputeShader->Release();
	}

	Device->NotifyDeletedComputePipeline(this);
	DEC_DWORD_STAT(STAT_VulkanNumComputePSOs);
}


FVulkanRHIGraphicsPipelineState::~FVulkanRHIGraphicsPipelineState()
{
#if !UE_BUILD_SHIPPING
	SGraphicsRHICount--;
#endif	
	DEC_DWORD_STAT(STAT_VulkanNumGraphicsPSOs);

	for (int ShaderStageIndex = 0; ShaderStageIndex < ShaderStage::NumStages; ShaderStageIndex++)
	{
		if (VulkanShaders[ShaderStageIndex] != nullptr)
		{
			VulkanShaders[ShaderStageIndex]->Release();
		}
	}

	Device->PipelineStateCache->NotifyDeletedGraphicsPSO(this);
	if (bShaderModulesLoaded)
	{
		PurgeLoadedShaderModules(Device);
	}
}

void FVulkanRHIGraphicsPipelineState::GetOrCreateShaderModules(FVulkanShader*const* Shaders)
{
	FScopeLock Lock(&FVulkanShaderHandleCS);
	for (int32 Index = 0; Index < ShaderStage::NumStages; ++Index)
	{
		FVulkanShader* Shader = Shaders[Index];
		if (Shader)
		{
			ShaderModules[Index] = Shader->GetOrCreateHandle(Desc, Layout, Layout->GetDescriptorSetLayoutHash());
		}
	}
}

void FVulkanRHIGraphicsPipelineState::PurgeShaderModules(FVulkanShader*const* Shaders)
{
	check(!bShaderModulesLoaded);

	for (int32 Index = 0; Index < ShaderStage::NumStages; ++Index)
	{
		FVulkanShader* Shader = Shaders[Index];
		if (Shader)
		{
			Shader->PurgeShaderModules();
			ShaderModules[Index] = VK_NULL_HANDLE;
		}
	}
}

void FVulkanRHIGraphicsPipelineState::PurgeLoadedShaderModules(FVulkanDevice* InDevice)
{
	check(bShaderModulesLoaded);

	for (int32 Index = 0; Index < ShaderStage::NumStages; ++Index)
	{
		if (ShaderModules[Index] != VK_NULL_HANDLE)
		{
			VulkanRHI::vkDestroyShaderModule(InDevice->GetInstanceHandle(), ShaderModules[Index], VULKAN_CPU_ALLOCATOR);
			ShaderModules[Index] = VK_NULL_HANDLE;
		}
	}

	bShaderModulesLoaded = false;
}


FVulkanPipelineStateCacheManager::FVulkanPipelineStateCacheManager(FVulkanDevice* InDevice)
	: Device(InDevice)
	, bEvictImmediately(false)
	, bLinkedToPSOFC(false)
	, bLinkedToPSOFCSucessfulLoaded(false)
	, PipelineCache(VK_NULL_HANDLE)
{
	bUseLRU = (int32)CVarEnableLRU.GetValueOnAnyThread() != 0;
	LRUUsedPipelineMax = CVarLRUPipelineCapacity.GetValueOnAnyThread();
}


FVulkanPipelineStateCacheManager::~FVulkanPipelineStateCacheManager()
{

	if (bLinkedToPSOFC)
	{
		if (OnShaderPipelineCacheOpenedDelegate.IsValid())
		{
			FShaderPipelineCache::GetCacheOpenedDelegate().Remove(OnShaderPipelineCacheOpenedDelegate);
		}

		if (OnShaderPipelineCachePrecompilationCompleteDelegate.IsValid())
		{
			FShaderPipelineCache::GetPrecompilationCompleteDelegate().Remove(OnShaderPipelineCachePrecompilationCompleteDelegate);
		}
	}
	DestroyCache();

	// Only destroy layouts when quitting
	for (auto& Pair : LayoutMap)
	{
		delete Pair.Value;
	}
	for (auto& Pair : DSetLayoutMap)
	{
		VulkanRHI::vkDestroyDescriptorSetLayout(Device->GetInstanceHandle(), Pair.Value.Handle, VULKAN_CPU_ALLOCATOR);
	}

	VulkanRHI::vkDestroyPipelineCache(Device->GetInstanceHandle(), PipelineCache, VULKAN_CPU_ALLOCATOR);
	PipelineCache = VK_NULL_HANDLE;
}

bool FVulkanPipelineStateCacheManager::Load(const TArray<FString>& CacheFilenames)
{
	bool bResult = false;
	// Try to load device cache first
	for (const FString& CacheFilename : CacheFilenames)
	{
		double BeginTime = FPlatformTime::Seconds();
		FString BinaryCacheFilename = FVulkanPlatform::CreatePSOBinaryCacheFilename(Device, CacheFilename);

		TArray<uint8> DeviceCache;
		if (FFileHelper::LoadFileToArray(DeviceCache, *BinaryCacheFilename, FILEREAD_Silent))
		{
			if (FVulkanPlatform::PSOBinaryCacheMatches(Device, DeviceCache))
			{
				VkPipelineCacheCreateInfo PipelineCacheInfo;
				ZeroVulkanStruct(PipelineCacheInfo, VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO);
				PipelineCacheInfo.initialDataSize = DeviceCache.Num();
				PipelineCacheInfo.pInitialData = DeviceCache.GetData();

				if (PipelineCache == VK_NULL_HANDLE)
				{
					// if we don't have one already, then create our main cache (PipelineCache)
					VERIFYVULKANRESULT(VulkanRHI::vkCreatePipelineCache(Device->GetInstanceHandle(), &PipelineCacheInfo, VULKAN_CPU_ALLOCATOR, &PipelineCache));
				}
				else
				{
					// if we have one already, create a temp one and merge into the main cache
					VkPipelineCache TempPipelineCache;
					VERIFYVULKANRESULT(VulkanRHI::vkCreatePipelineCache(Device->GetInstanceHandle(), &PipelineCacheInfo, VULKAN_CPU_ALLOCATOR, &TempPipelineCache));
					VERIFYVULKANRESULT(VulkanRHI::vkMergePipelineCaches(Device->GetInstanceHandle(), PipelineCache, 1, &TempPipelineCache));
					VulkanRHI::vkDestroyPipelineCache(Device->GetInstanceHandle(), TempPipelineCache, VULKAN_CPU_ALLOCATOR);
				}

				double EndTime = FPlatformTime::Seconds();
				UE_LOG(LogVulkanRHI, Display, TEXT("FVulkanPipelineStateCacheManager: Loaded binary pipeline cache %s in %.3f seconds"), *BinaryCacheFilename, (float)(EndTime - BeginTime));
				bResult = true;
			}
			else
			{
				UE_LOG(LogVulkanRHI, Error, TEXT("FVulkanPipelineStateCacheManager: Mismatched binary pipeline cache %s"), *BinaryCacheFilename);
			}
		}
		else
		{
			UE_LOG(LogVulkanRHI, Display, TEXT("FVulkanPipelineStateCacheManager: Binary pipeline cache '%s' not found."), *BinaryCacheFilename);
		}
	}

	if(CVarEnableLRU.GetValueOnAnyThread() != 0)
	{
		for (const FString& CacheFilename : CacheFilenames)
		{
			double BeginTime = FPlatformTime::Seconds();
			FString LruCacheFilename = FVulkanPlatform::CreatePSOBinaryCacheFilename(Device, CacheFilename);
			LruCacheFilename += TEXT(".lru");
			LruCacheFilename.ReplaceInline(TEXT("TempScanVulkanPSO_"), TEXT("VulkanPSO_"));  //lru files do not use the rename trick...but are still protected against corruption indirectly

			TArray<uint8> MemFile;
			if (FFileHelper::LoadFileToArray(MemFile, *LruCacheFilename, FILEREAD_Silent))
			{
				FMemoryReader Ar(MemFile);

				FVulkanLRUCacheFile File;
				bool Valid = File.Load(Ar);
				if (!Valid)
				{
					UE_LOG(LogVulkanRHI, Warning, TEXT("Unable to load lru pipeline cache '%s'"), *LruCacheFilename);
					bResult = false;
				}

				for (int32 Index = 0; Index < File.PipelineSizes.Num(); ++Index)
				{
					LRU2SizeList.Add(File.PipelineSizes[Index].ShaderHash, File.PipelineSizes[Index]);
				}
				UE_LOG(LogVulkanRHI, Display, TEXT("Loaded %d LRU size entries for '%s'"), File.PipelineSizes.Num(), *LruCacheFilename);
			}
			else
			{
				UE_LOG(LogVulkanRHI, Warning, TEXT("Unable to load lru pipeline cache '%s'"), *LruCacheFilename);
				bResult = false;
			}
		}
	}

	// Lazily create the cache in case the load failed
	if (PipelineCache == VK_NULL_HANDLE)
	{
		VkPipelineCacheCreateInfo PipelineCacheInfo;
		ZeroVulkanStruct(PipelineCacheInfo, VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO);
		VERIFYVULKANRESULT(VulkanRHI::vkCreatePipelineCache(Device->GetInstanceHandle(), &PipelineCacheInfo, VULKAN_CPU_ALLOCATOR, &PipelineCache));
	}

	return bResult;
}

void FVulkanPipelineStateCacheManager::InitAndLoad(const TArray<FString>& CacheFilenames)
{
	if (GEnablePipelineCacheLoadCvar.GetValueOnAnyThread() == 0)
	{
		UE_LOG(LogVulkanRHI, Display, TEXT("Not loading pipeline cache per r.Vulkan.PipelineCacheLoad=0"));
	}
	else
	{
		if (GPipelineCacheFromShaderPipelineCacheCvar.GetValueOnAnyThread() == 0)
		{
			Load(CacheFilenames);
		}
		else
		{
			bLinkedToPSOFC = true;
			UE_LOG(LogVulkanRHI, Log, TEXT("FVulkanPipelineStateCacheManager will check for loading, etc when ShaderPipelineCache opens its file"));


#if PLATFORM_ANDROID && USE_ANDROID_FILE
			// @todo Lumin: Use that GetPathForExternalWrite or something?
			// BTW, this is totally bad. We should not platform ifdefs like this, rather the HAL needs to be extended!
			extern FString GExternalFilePath;
			LinkedToPSOFCCacheFolderPath = GExternalFilePath / TEXT("VulkanProgramBinaryCache");

#else
			LinkedToPSOFCCacheFolderPath = FPaths::ProjectSavedDir() / TEXT("VulkanProgramBinaryCache");
#endif

			// Remove entire ProgramBinaryCache folder if -ClearOpenGLBinaryProgramCache is specified on command line
			if (FParse::Param(FCommandLine::Get(), TEXT("ClearVulkanBinaryProgramCache")))
			{
				UE_LOG(LogVulkanRHI, Log, TEXT("FVulkanPipelineStateCacheManager: Deleting binary program cache folder for -ClearVulkanBinaryProgramCache: %s"), *LinkedToPSOFCCacheFolderPath);
				FPlatformFileManager::Get().GetPlatformFile().DeleteDirectoryRecursively(*LinkedToPSOFCCacheFolderPath);
			}

			OnShaderPipelineCacheOpenedDelegate = FShaderPipelineCache::GetCacheOpenedDelegate().AddRaw(this, &FVulkanPipelineStateCacheManager::OnShaderPipelineCacheOpened);
			OnShaderPipelineCachePrecompilationCompleteDelegate = FShaderPipelineCache::GetPrecompilationCompleteDelegate().AddRaw(this, &FVulkanPipelineStateCacheManager::OnShaderPipelineCachePrecompilationComplete);
		}
	}

	// Lazily create the cache in case the load failed
	if (PipelineCache == VK_NULL_HANDLE)
	{
		VkPipelineCacheCreateInfo PipelineCacheInfo;
		ZeroVulkanStruct(PipelineCacheInfo, VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO);
		VERIFYVULKANRESULT(VulkanRHI::vkCreatePipelineCache(Device->GetInstanceHandle(), &PipelineCacheInfo, VULKAN_CPU_ALLOCATOR, &PipelineCache));
	}
}

void FVulkanPipelineStateCacheManager::OnShaderPipelineCacheOpened(FString const& Name, EShaderPlatform Platform, uint32 Count, const FGuid& VersionGuid, FShaderPipelineCache::FShaderCachePrecompileContext& ShaderCachePrecompileContext)
{
	check(bLinkedToPSOFC);
	UE_LOG(LogVulkanRHI, Log, TEXT("FVulkanPipelineStateCacheManager::OnShaderPipelineCacheOpened %s %d %s"), *Name, Count, *VersionGuid.ToString());

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	const VkPhysicalDeviceProperties& DeviceProperties = Device->GetDeviceProperties();
	FString BinaryCacheAppendage = FString::Printf(TEXT(".%x.%x"), DeviceProperties.vendorID, DeviceProperties.deviceID);

	LinkedToPSOFCCacheFolderFilename = LinkedToPSOFCCacheFolderPath / TEXT("VulkanPSO_") + VersionGuid.ToString() + BinaryCacheAppendage;
	FString TempName = LinkedToPSOFCCacheFolderPath / TEXT("TempScanVulkanPSO_") + VersionGuid.ToString() + BinaryCacheAppendage;

	bool bSuccess = false;

	if (PlatformFile.FileExists(*LinkedToPSOFCCacheFolderFilename))
	{
		// Try to move the file to a temporary filename before the scan, so we won't try to read it again if it's corrupted
		PlatformFile.DeleteFile(*TempName);
		PlatformFile.MoveFile(*TempName, *LinkedToPSOFCCacheFolderFilename);

		TArray<FString> CacheFilenames;
		CacheFilenames.Add(TempName);
		bSuccess = Load(CacheFilenames);

		// Rename the file back after a successful scan.
		if (bSuccess)
		{
			bLinkedToPSOFCSucessfulLoaded = true;
			PlatformFile.MoveFile(*LinkedToPSOFCCacheFolderFilename, *TempName);

			if (CVarPipelineLRUCacheEvictBinary.GetValueOnAnyThread())
			{
				bEvictImmediately = true;
			}
		}
	}
	else
	{
		UE_LOG(LogVulkanRHI, Log, TEXT("FVulkanPipelineStateCacheManager: %s does not exist."), *LinkedToPSOFCCacheFolderFilename);
	}
	if (!bSuccess)
	{
		UE_LOG(LogVulkanRHI, Log, TEXT("FVulkanPipelineStateCacheManager: No matching vulkan PSO cache found or it failed to load, deleting binary program cache folder: %s"), *LinkedToPSOFCCacheFolderPath);
		FPlatformFileManager::Get().GetPlatformFile().DeleteDirectoryRecursively(*LinkedToPSOFCCacheFolderPath);
	}

	{
		if (!bLinkedToPSOFCSucessfulLoaded || (bEvictImmediately && CVarPipelineLRUCacheEvictBinaryPreloadScreen.GetValueOnAnyThread()))
		{
			ShaderCachePrecompileContext.SetPrecompilationIsSlowTask();
		}
	}
}

void FVulkanPipelineStateCacheManager::OnShaderPipelineCachePrecompilationComplete(uint32 Count, double Seconds, const FShaderPipelineCache::FShaderCachePrecompileContext& ShaderCachePrecompileContext)
{
	check(bLinkedToPSOFC);
	UE_LOG(LogVulkanRHI, Log, TEXT("FVulkanPipelineStateCacheManager::OnShaderPipelineCachePrecompilationComplete"));

	bEvictImmediately = false;
	if (!bLinkedToPSOFCSucessfulLoaded)
	{
		Save(LinkedToPSOFCCacheFolderFilename, true);
	}

	// Want to ignore any subsequent Shader Pipeline Cache opening/closing, eg when loading modules
	FShaderPipelineCache::GetCacheOpenedDelegate().Remove(OnShaderPipelineCacheOpenedDelegate);
	FShaderPipelineCache::GetPrecompilationCompleteDelegate().Remove(OnShaderPipelineCachePrecompilationCompleteDelegate);
	OnShaderPipelineCacheOpenedDelegate.Reset();
	OnShaderPipelineCachePrecompilationCompleteDelegate.Reset();
}

void FVulkanPipelineStateCacheManager::Save(const FString& CacheFilename, bool bFromPSOFC)
{
	if (bLinkedToPSOFC && !bFromPSOFC)
	{
		UE_LOG(LogVulkanRHI, Log, TEXT("FVulkanPipelineStateCacheManager: skipped saving because we only save if the PSOFC based one failed to load."));
		return;
	}
	FScopeLock Lock1(&GraphicsPSOLockedCS);
	FScopeLock Lock2(&LRUCS);



	// First save Device Cache
	size_t Size = 0;
	VERIFYVULKANRESULT(VulkanRHI::vkGetPipelineCacheData(Device->GetInstanceHandle(), PipelineCache, &Size, nullptr));
	// 16 is HeaderSize + HeaderVersion
	if (Size >= 16 + VK_UUID_SIZE)
	{
		TArray<uint8> DeviceCache;
		DeviceCache.AddUninitialized(Size);
		VkResult Result = VulkanRHI::vkGetPipelineCacheData(Device->GetInstanceHandle(), PipelineCache, &Size, DeviceCache.GetData());
		if (Result == VK_SUCCESS)
		{
			FString BinaryCacheFilename = FVulkanPlatform::CreatePSOBinaryCacheFilename(Device, CacheFilename);

			if (FFileHelper::SaveArrayToFile(DeviceCache, *BinaryCacheFilename))
			{
				UE_LOG(LogVulkanRHI, Display, TEXT("FVulkanPipelineStateCacheManager: Saved device pipeline cache file '%s', %d bytes"), *BinaryCacheFilename, DeviceCache.Num());
			}
			else
			{
				UE_LOG(LogVulkanRHI, Error, TEXT("FVulkanPipelineStateCacheManager: Failed to save device pipeline cache file '%s', %d bytes"), *BinaryCacheFilename, DeviceCache.Num());
			}
		}
		else if (Result == VK_INCOMPLETE || Result == VK_ERROR_OUT_OF_HOST_MEMORY)
		{
			UE_LOG(LogVulkanRHI, Warning, TEXT("Failed to get Vulkan pipeline cache data. Error %d, %d bytes"), Result, Size);

			VulkanRHI::vkDestroyPipelineCache(Device->GetInstanceHandle(), PipelineCache, VULKAN_CPU_ALLOCATOR);
			VkPipelineCacheCreateInfo PipelineCacheInfo;
			ZeroVulkanStruct(PipelineCacheInfo, VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO);
			VERIFYVULKANRESULT(VulkanRHI::vkCreatePipelineCache(Device->GetInstanceHandle(), &PipelineCacheInfo, VULKAN_CPU_ALLOCATOR, &PipelineCache));
		}
		else
		{
			VERIFYVULKANRESULT(Result);
		}
	}

	if (CVarEnableLRU.GetValueOnAnyThread() != 0)
	{
		// LRU cache file
		TArray<uint8> MemFile;
		FMemoryWriter Ar(MemFile);
		FVulkanLRUCacheFile File;
		File.Header.Version = FVulkanLRUCacheFile::LRU_CACHE_VERSION;
		File.Header.SizeOfPipelineSizes = (int32)sizeof(FVulkanPipelineSize);
		LRU2SizeList.GenerateValueArray(File.PipelineSizes);
		File.Save(Ar);

		FString LruCacheFilename = FVulkanPlatform::CreatePSOBinaryCacheFilename(Device, CacheFilename);
		LruCacheFilename += TEXT(".lru");

		if (FFileHelper::SaveArrayToFile(MemFile, *LruCacheFilename))
		{
			UE_LOG(LogVulkanRHI, Display, TEXT("FVulkanPipelineStateCacheManager: Saved pipeline lru pipeline cache file '%s', %d hashes, %d bytes"), *LruCacheFilename, LRU2SizeList.Num(), MemFile.Num());
		}
		else
		{
			UE_LOG(LogVulkanRHI, Error, TEXT("FVulkanPipelineStateCacheManager: Failed to save pipeline lru pipeline cache file '%s', %d hashes, %d bytes"), *LruCacheFilename, LRU2SizeList.Num(), MemFile.Num());
		}
	}
}

FArchive& operator << (FArchive& Ar, FGfxPipelineDesc::FBlendAttachment& Attachment)
{
	// Modify VERSION if serialization changes
	Ar << Attachment.bBlend;
	Ar << Attachment.ColorBlendOp;
	Ar << Attachment.SrcColorBlendFactor;
	Ar << Attachment.DstColorBlendFactor;
	Ar << Attachment.AlphaBlendOp;
	Ar << Attachment.SrcAlphaBlendFactor;
	Ar << Attachment.DstAlphaBlendFactor;
	Ar << Attachment.ColorWriteMask;
	return Ar;
}

void FGfxPipelineDesc::FBlendAttachment::ReadFrom(const VkPipelineColorBlendAttachmentState& InState)
{
	bBlend =				InState.blendEnable != VK_FALSE;
	ColorBlendOp =			(uint8)InState.colorBlendOp;
	SrcColorBlendFactor =	(uint8)InState.srcColorBlendFactor;
	DstColorBlendFactor =	(uint8)InState.dstColorBlendFactor;
	AlphaBlendOp =			(uint8)InState.alphaBlendOp;
	SrcAlphaBlendFactor =	(uint8)InState.srcAlphaBlendFactor;
	DstAlphaBlendFactor =	(uint8)InState.dstAlphaBlendFactor;
	ColorWriteMask =		(uint8)InState.colorWriteMask;
}

void FGfxPipelineDesc::FBlendAttachment::WriteInto(VkPipelineColorBlendAttachmentState& Out) const
{
	Out.blendEnable =			bBlend ? VK_TRUE : VK_FALSE;
	Out.colorBlendOp =			(VkBlendOp)ColorBlendOp;
	Out.srcColorBlendFactor =	(VkBlendFactor)SrcColorBlendFactor;
	Out.dstColorBlendFactor =	(VkBlendFactor)DstColorBlendFactor;
	Out.alphaBlendOp =			(VkBlendOp)AlphaBlendOp;
	Out.srcAlphaBlendFactor =	(VkBlendFactor)SrcAlphaBlendFactor;
	Out.dstAlphaBlendFactor =	(VkBlendFactor)DstAlphaBlendFactor;
	Out.colorWriteMask =		(VkColorComponentFlags)ColorWriteMask;
}


void FDescriptorSetLayoutBinding::ReadFrom(const VkDescriptorSetLayoutBinding& InState)
{
	Binding =			InState.binding;
	ensure(InState.descriptorCount == 1);
	//DescriptorCount =	InState.descriptorCount;
	DescriptorType =	InState.descriptorType;
	StageFlags =		InState.stageFlags;
}

void FDescriptorSetLayoutBinding::WriteInto(VkDescriptorSetLayoutBinding& Out) const
{
	Out.binding = Binding;
	//Out.descriptorCount = DescriptorCount;
	Out.descriptorType = (VkDescriptorType)DescriptorType;
	Out.stageFlags = StageFlags;
}

FArchive& operator << (FArchive& Ar, FDescriptorSetLayoutBinding& Binding)
{
	// Modify VERSION if serialization changes
	Ar << Binding.Binding;
	//Ar << Binding.DescriptorCount;
	Ar << Binding.DescriptorType;
	Ar << Binding.StageFlags;
	return Ar;
}

void FGfxPipelineDesc::FVertexBinding::ReadFrom(const VkVertexInputBindingDescription& InState)
{
	Binding =	InState.binding;
	InputRate =	(uint16)InState.inputRate;
	Stride =	InState.stride;
}

void FGfxPipelineDesc::FVertexBinding::WriteInto(VkVertexInputBindingDescription& Out) const
{
	Out.binding =	Binding;
	Out.inputRate =	(VkVertexInputRate)InputRate;
	Out.stride =	Stride;
}

FArchive& operator << (FArchive& Ar, FGfxPipelineDesc::FVertexBinding& Binding)
{
	// Modify VERSION if serialization changes
	Ar << Binding.Stride;
	Ar << Binding.Binding;
	Ar << Binding.InputRate;
	return Ar;
}

void FGfxPipelineDesc::FVertexAttribute::ReadFrom(const VkVertexInputAttributeDescription& InState)
{
	Binding =	InState.binding;
	Format =	(uint32)InState.format;
	Location =	InState.location;
	Offset =	InState.offset;
}

void FGfxPipelineDesc::FVertexAttribute::WriteInto(VkVertexInputAttributeDescription& Out) const
{
	Out.binding =	Binding;
	Out.format =	(VkFormat)Format;
	Out.location =	Location;
	Out.offset =	Offset;
}

FArchive& operator << (FArchive& Ar, FGfxPipelineDesc::FVertexAttribute& Attribute)
{
	// Modify VERSION if serialization changes
	Ar << Attribute.Location;
	Ar << Attribute.Binding;
	Ar << Attribute.Format;
	Ar << Attribute.Offset;
	return Ar;
}

void FGfxPipelineDesc::FRasterizer::ReadFrom(const VkPipelineRasterizationStateCreateInfo& InState)
{
	PolygonMode =				InState.polygonMode;
	CullMode =					InState.cullMode;
	DepthBiasSlopeScale =		InState.depthBiasSlopeFactor;
	DepthBiasConstantFactor =	InState.depthBiasConstantFactor;
}

void FGfxPipelineDesc::FRasterizer::WriteInto(VkPipelineRasterizationStateCreateInfo& Out) const
{
	Out.polygonMode =				(VkPolygonMode)PolygonMode;
	Out.cullMode =					(VkCullModeFlags)CullMode;
	Out.frontFace =					VK_FRONT_FACE_CLOCKWISE;
	Out.depthClampEnable =			VK_FALSE;
	Out.depthBiasEnable =			DepthBiasConstantFactor != 0.0f ? VK_TRUE : VK_FALSE;
	Out.rasterizerDiscardEnable =	VK_FALSE;
	Out.depthBiasSlopeFactor =		DepthBiasSlopeScale;
	Out.depthBiasConstantFactor =	DepthBiasConstantFactor;
}

FArchive& operator << (FArchive& Ar, FGfxPipelineDesc::FRasterizer& Rasterizer)
{
	// Modify VERSION if serialization changes
	Ar << Rasterizer.PolygonMode;
	Ar << Rasterizer.CullMode;
	Ar << Rasterizer.DepthBiasSlopeScale;
	Ar << Rasterizer.DepthBiasConstantFactor;
	return Ar;
}

void FGfxPipelineDesc::FDepthStencil::ReadFrom(const VkPipelineDepthStencilStateCreateInfo& InState)
{
	DepthCompareOp =			(uint8)InState.depthCompareOp;
	bDepthTestEnable =			InState.depthTestEnable != VK_FALSE;
	bDepthWriteEnable =			InState.depthWriteEnable != VK_FALSE;
	bDepthBoundsTestEnable =	InState.depthBoundsTestEnable != VK_FALSE;
	bStencilTestEnable =		InState.stencilTestEnable != VK_FALSE;
	FrontFailOp =				(uint8)InState.front.failOp;
	FrontPassOp =				(uint8)InState.front.passOp;
	FrontDepthFailOp =			(uint8)InState.front.depthFailOp;
	FrontCompareOp =			(uint8)InState.front.compareOp;
	FrontCompareMask =			(uint8)InState.front.compareMask;
	FrontWriteMask =			InState.front.writeMask;
	FrontReference =			InState.front.reference;
	BackFailOp =				(uint8)InState.back.failOp;
	BackPassOp =				(uint8)InState.back.passOp;
	BackDepthFailOp =			(uint8)InState.back.depthFailOp;
	BackCompareOp =				(uint8)InState.back.compareOp;
	BackCompareMask =			(uint8)InState.back.compareMask;
	BackWriteMask =				InState.back.writeMask;
	BackReference =				InState.back.reference;
}

void FGfxPipelineDesc::FDepthStencil::WriteInto(VkPipelineDepthStencilStateCreateInfo& Out) const
{
	Out.depthCompareOp =		(VkCompareOp)DepthCompareOp;
	Out.depthTestEnable =		bDepthTestEnable;
	Out.depthWriteEnable =		bDepthWriteEnable;
	Out.depthBoundsTestEnable =	bDepthBoundsTestEnable;
	Out.stencilTestEnable =		bStencilTestEnable;
	Out.front.failOp =			(VkStencilOp)FrontFailOp;
	Out.front.passOp =			(VkStencilOp)FrontPassOp;
	Out.front.depthFailOp =		(VkStencilOp)FrontDepthFailOp;
	Out.front.compareOp =		(VkCompareOp)FrontCompareOp;
	Out.front.compareMask =		FrontCompareMask;
	Out.front.writeMask =		FrontWriteMask;
	Out.front.reference =		FrontReference;
	Out.back.failOp =			(VkStencilOp)BackFailOp;
	Out.back.passOp =			(VkStencilOp)BackPassOp;
	Out.back.depthFailOp =		(VkStencilOp)BackDepthFailOp;
	Out.back.compareOp =		(VkCompareOp)BackCompareOp;
	Out.back.writeMask =		BackWriteMask;
	Out.back.compareMask =		BackCompareMask;
	Out.back.reference =		BackReference;
}

FArchive& operator << (FArchive& Ar, FGfxPipelineDesc::FDepthStencil& DepthStencil)
{
	// Modify VERSION if serialization changes
	Ar << DepthStencil.DepthCompareOp;
	Ar << DepthStencil.bDepthTestEnable;
	Ar << DepthStencil.bDepthWriteEnable;
	Ar << DepthStencil.bDepthBoundsTestEnable;
	Ar << DepthStencil.bStencilTestEnable;
	Ar << DepthStencil.FrontFailOp;
	Ar << DepthStencil.FrontPassOp;
	Ar << DepthStencil.FrontDepthFailOp;
	Ar << DepthStencil.FrontCompareOp;
	Ar << DepthStencil.FrontCompareMask;
	Ar << DepthStencil.FrontWriteMask;
	Ar << DepthStencil.FrontReference;
	Ar << DepthStencil.BackFailOp;
	Ar << DepthStencil.BackPassOp;
	Ar << DepthStencil.BackDepthFailOp;
	Ar << DepthStencil.BackCompareOp;
	Ar << DepthStencil.BackCompareMask;
	Ar << DepthStencil.BackWriteMask;
	Ar << DepthStencil.BackReference;
	return Ar;
}

void FGfxPipelineDesc::FRenderTargets::FAttachmentRef::ReadFrom(const VkAttachmentReference& InState)
{
	Attachment =	InState.attachment;
	Layout =		(uint64)InState.layout;
}

void FGfxPipelineDesc::FRenderTargets::FAttachmentRef::WriteInto(VkAttachmentReference& Out) const
{
	Out.attachment =	Attachment;
	Out.layout =		(VkImageLayout)Layout;
}

FArchive& operator << (FArchive& Ar, FGfxPipelineDesc::FRenderTargets::FAttachmentRef& AttachmentRef)
{
	// Modify VERSION if serialization changes
	Ar << AttachmentRef.Attachment;
	Ar << AttachmentRef.Layout;
	return Ar;
}

void FGfxPipelineDesc::FRenderTargets::FAttachmentDesc::ReadFrom(const VkAttachmentDescription &InState)
{
	Format =			(uint32)InState.format;
	Flags =				(uint8)InState.flags;
	Samples =			(uint8)InState.samples;
	LoadOp =			(uint8)InState.loadOp;
	StoreOp =			(uint8)InState.storeOp;
	StencilLoadOp =		(uint8)InState.stencilLoadOp;
	StencilStoreOp =	(uint8)InState.stencilStoreOp;
	InitialLayout =		(uint64)InState.initialLayout;
	FinalLayout =		(uint64)InState.finalLayout;
}

void FGfxPipelineDesc::FRenderTargets::FAttachmentDesc::WriteInto(VkAttachmentDescription& Out) const
{
	Out.format =			(VkFormat)Format;
	Out.flags =				Flags;
	Out.samples =			(VkSampleCountFlagBits)Samples;
	Out.loadOp =			(VkAttachmentLoadOp)LoadOp;
	Out.storeOp =			(VkAttachmentStoreOp)StoreOp;
	Out.stencilLoadOp =		(VkAttachmentLoadOp)StencilLoadOp;
	Out.stencilStoreOp =	(VkAttachmentStoreOp)StencilStoreOp;
	Out.initialLayout =		(VkImageLayout)InitialLayout;
	Out.finalLayout =		(VkImageLayout)FinalLayout;
}

FArchive& operator << (FArchive& Ar, FGfxPipelineDesc::FRenderTargets::FAttachmentDesc& AttachmentDesc)
{
	// Modify VERSION if serialization changes
	Ar << AttachmentDesc.Format;
	Ar << AttachmentDesc.Flags;
	Ar << AttachmentDesc.Samples;
	Ar << AttachmentDesc.LoadOp;
	Ar << AttachmentDesc.StoreOp;
	Ar << AttachmentDesc.StencilLoadOp;
	Ar << AttachmentDesc.StencilStoreOp;
	Ar << AttachmentDesc.InitialLayout;
	Ar << AttachmentDesc.FinalLayout;

	return Ar;
}

void FGfxPipelineDesc::FRenderTargets::ReadFrom(const FVulkanRenderTargetLayout& RTLayout)
{
	NumAttachments =			RTLayout.NumAttachmentDescriptions;
	NumColorAttachments =		RTLayout.NumColorAttachments;

	bHasDepthStencil =			RTLayout.bHasDepthStencil != 0;
	bHasResolveAttachments =	RTLayout.bHasResolveAttachments != 0;
	bHasFragmentDensityAttachment =	RTLayout.bHasFragmentDensityAttachment != 0;
	NumUsedClearValues =		RTLayout.NumUsedClearValues;

	RenderPassCompatibleHash =	RTLayout.GetRenderPassCompatibleHash();

	Extent3D.X = RTLayout.Extent.Extent3D.width;
	Extent3D.Y = RTLayout.Extent.Extent3D.height;
	Extent3D.Z = RTLayout.Extent.Extent3D.depth;

	auto CopyAttachmentRefs = [&](TArray<FGfxPipelineDesc::FRenderTargets::FAttachmentRef>& Dest, const VkAttachmentReference* Source, uint32 Count)
	{
		for (uint32 Index = 0; Index < Count; ++Index)
		{
			FGfxPipelineDesc::FRenderTargets::FAttachmentRef* New = new(Dest) FGfxPipelineDesc::FRenderTargets::FAttachmentRef;
			New->ReadFrom(Source[Index]);
		}
	};
	CopyAttachmentRefs(ColorAttachments, RTLayout.ColorReferences, UE_ARRAY_COUNT(RTLayout.ColorReferences));
	CopyAttachmentRefs(ResolveAttachments, RTLayout.ResolveReferences, UE_ARRAY_COUNT(RTLayout.ResolveReferences));
	DepthStencil.ReadFrom(RTLayout.DepthStencilReference);
	FragmentDensity.ReadFrom(RTLayout.FragmentDensityReference);

	Descriptions.AddZeroed(UE_ARRAY_COUNT(RTLayout.Desc));
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(RTLayout.Desc); ++Index)
	{
		Descriptions[Index].ReadFrom(RTLayout.Desc[Index]);
	}
}

void FGfxPipelineDesc::FRenderTargets::WriteInto(FVulkanRenderTargetLayout& Out) const
{
	Out.NumAttachmentDescriptions =	NumAttachments;
	Out.NumColorAttachments =		NumColorAttachments;

	Out.bHasDepthStencil =			bHasDepthStencil;
	Out.bHasResolveAttachments =	bHasResolveAttachments;
	Out.bHasFragmentDensityAttachment =	bHasFragmentDensityAttachment;
	Out.NumUsedClearValues =		NumUsedClearValues;

	ensure(0);
	Out.RenderPassCompatibleHash =	RenderPassCompatibleHash;

	Out.Extent.Extent3D.width =		Extent3D.X;
	Out.Extent.Extent3D.height =	Extent3D.Y;
	Out.Extent.Extent3D.depth =		Extent3D.Z;

	auto CopyAttachmentRefs = [&](const TArray<FGfxPipelineDesc::FRenderTargets::FAttachmentRef>& Source, VkAttachmentReference* Dest, uint32 Count)
	{
		for (uint32 Index = 0; Index < Count; ++Index, ++Dest)
		{
			Source[Index].WriteInto(*Dest);
		}
	};
	CopyAttachmentRefs(ColorAttachments, Out.ColorReferences, UE_ARRAY_COUNT(Out.ColorReferences));
	CopyAttachmentRefs(ResolveAttachments, Out.ResolveReferences, UE_ARRAY_COUNT(Out.ResolveReferences));
	DepthStencil.WriteInto(Out.DepthStencilReference);
	FragmentDensity.WriteInto(Out.FragmentDensityReference);


	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Out.Desc); ++Index)
	{
		Descriptions[Index].WriteInto(Out.Desc[Index]);
	}
}

FArchive& operator << (FArchive& Ar, FGfxPipelineDesc::FRenderTargets& RTs)
{
	// Modify VERSION if serialization changes
	Ar << RTs.NumAttachments;
	Ar << RTs.NumColorAttachments;
	Ar << RTs.NumUsedClearValues;
	Ar << RTs.ColorAttachments;
	Ar << RTs.ResolveAttachments;
	Ar << RTs.DepthStencil;
	Ar << RTs.FragmentDensity;

	Ar << RTs.Descriptions;

	Ar << RTs.bHasDepthStencil;
	Ar << RTs.bHasResolveAttachments;
	Ar << RTs.RenderPassCompatibleHash;
	Ar << RTs.Extent3D;

	return Ar;
}

FArchive& operator << (FArchive& Ar, FGfxPipelineDesc& Entry)
{
	// Modify VERSION if serialization changes
	Ar << Entry.VertexInputKey;
	Ar << Entry.RasterizationSamples;
	Ar << Entry.ControlPoints;
	Ar << Entry.Topology;

	Ar << Entry.ColorAttachmentStates;

	Ar << Entry.DescriptorSetLayoutBindings;

	Ar << Entry.VertexBindings;
	Ar << Entry.VertexAttributes;
	Ar << Entry.Rasterizer;

	Ar << Entry.DepthStencil;

#if VULKAN_USE_SHADERKEYS
	for (uint64& ShaderKey : Entry.ShaderKeys)
	{
		Ar << ShaderKey;
	}
#else
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Entry.ShaderHashes.Stages); ++Index)
	{
		Ar << Entry.ShaderHashes.Stages[Index];
	}
#endif
	Ar << Entry.RenderTargets;

#if VULKAN_SUPPORTS_COLOR_CONVERSIONS
	for (uint32 Index = 0; Index < MaxImmutableSamplers; ++Index)
	{
		uint64 Sampler = (uint64)Entry.ImmutableSamplers[Index];
		Ar << Sampler;
		Entry.ImmutableSamplers[Index] = (SIZE_T)Sampler;
	}
#endif

	Ar << Entry.UseAlphaToCoverage;

	return Ar;
}

FArchive& operator << (FArchive& Ar, FGfxPipelineDesc* Entry)
{
	return Ar << (*Entry);
}

FArchive& operator << (FArchive& Ar, FVulkanPipelineSize& PS)
{
	Ar << PS.ShaderHash;
	Ar << PS.PipelineSize;

	return Ar;
}


FVulkanPSOKey FGfxPipelineDesc::CreateKey2() const
{
	FVulkanPSOKey Result;
	Result.GenerateFromArchive([this](FArchive& Ar)
	{
		Ar << const_cast<FGfxPipelineDesc&>(*this);
	});
	return Result;
}



bool FVulkanPipelineStateCacheManager::CreateGfxPipelineFromEntry(FVulkanRHIGraphicsPipelineState* PSO, FVulkanShader* Shaders[ShaderStage::NumStages], VkPipeline* Pipeline)
{
	FGfxPipelineDesc* GfxEntry = &PSO->Desc;
	if (Shaders[ShaderStage::Pixel] == nullptr && !FVulkanPlatform::SupportsNullPixelShader())
	{
		Shaders[ShaderStage::Pixel] = ResourceCast(TShaderMapRef<FNULLPS>(GetGlobalShaderMap(GMaxRHIFeatureLevel)).GetPixelShader());
	}

	if (!PSO->bShaderModulesLoaded)
	{
		PSO->GetOrCreateShaderModules(Shaders);
	}

	// Pipeline
	VkGraphicsPipelineCreateInfo PipelineInfo;
	ZeroVulkanStruct(PipelineInfo, VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
	PipelineInfo.layout = PSO->Layout->GetPipelineLayout();

	// Color Blend
	VkPipelineColorBlendStateCreateInfo CBInfo;
	ZeroVulkanStruct(CBInfo, VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
	CBInfo.attachmentCount = GfxEntry->ColorAttachmentStates.Num();
	VkPipelineColorBlendAttachmentState BlendStates[MaxSimultaneousRenderTargets];
	FMemory::Memzero(BlendStates);
	uint32 ColorWriteMask = 0xffffffff;
	if(Shaders[ShaderStage::Pixel])
	{
		ColorWriteMask = Shaders[ShaderStage::Pixel]->CodeHeader.InOutMask;
	}
	for (int32 Index = 0; Index < GfxEntry->ColorAttachmentStates.Num(); ++Index)
	{
		GfxEntry->ColorAttachmentStates[Index].WriteInto(BlendStates[Index]);
		
		if(0 == (ColorWriteMask & 1)) //clear write mask of rendertargets not written by pixelshader.
		{
			BlendStates[Index].colorWriteMask = 0;
		}
		ColorWriteMask >>= 1;		
	}
	CBInfo.pAttachments = BlendStates;
	CBInfo.blendConstants[0] = 1.0f;
	CBInfo.blendConstants[1] = 1.0f;
	CBInfo.blendConstants[2] = 1.0f;
	CBInfo.blendConstants[3] = 1.0f;

	// Viewport
	VkPipelineViewportStateCreateInfo VPInfo;
	ZeroVulkanStruct(VPInfo, VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
	VPInfo.viewportCount = 1;
	VPInfo.scissorCount = 1;

	// Multisample
	VkPipelineMultisampleStateCreateInfo MSInfo;
	ZeroVulkanStruct(MSInfo, VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
	MSInfo.rasterizationSamples = (VkSampleCountFlagBits)FMath::Max<uint16>(1u, GfxEntry->RasterizationSamples);
	MSInfo.alphaToCoverageEnable = GfxEntry->UseAlphaToCoverage;

	VkPipelineShaderStageCreateInfo ShaderStages[ShaderStage::NumStages];
	FMemory::Memzero(ShaderStages);
	PipelineInfo.stageCount = 0;
	PipelineInfo.pStages = ShaderStages;
	// main_00000000_00000000
	ANSICHAR EntryPoints[ShaderStage::NumStages][24];
	bool bHasTessellation = false;
	for (int32 ShaderStage = 0; ShaderStage < ShaderStage::NumStages; ++ShaderStage)
	{
		if (!PSO->ShaderModules[ShaderStage])
		{
			continue;
		}
		const ShaderStage::EStage CurrStage = (ShaderStage::EStage)ShaderStage;

		ShaderStages[PipelineInfo.stageCount].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		VkShaderStageFlagBits Stage = UEFrequencyToVKStageBit(ShaderStage::GetFrequencyForGfxStage(CurrStage));
		ShaderStages[PipelineInfo.stageCount].stage = Stage;
		bHasTessellation = bHasTessellation || ((Stage & (VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT)) != 0);
		ShaderStages[PipelineInfo.stageCount].module = PSO->ShaderModules[CurrStage];
		Shaders[ShaderStage]->GetEntryPoint(EntryPoints[PipelineInfo.stageCount], 24);
		ShaderStages[PipelineInfo.stageCount].pName = EntryPoints[PipelineInfo.stageCount];
		PipelineInfo.stageCount++;
	}

	check(PipelineInfo.stageCount != 0);

	// Vertex Input. The structure is mandatory even without vertex attributes.
	VkPipelineVertexInputStateCreateInfo VBInfo;
	ZeroVulkanStruct(VBInfo, VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
	TArray<VkVertexInputBindingDescription> VBBindings;
	for (const FGfxPipelineDesc::FVertexBinding& SourceBinding : GfxEntry->VertexBindings)
	{
		VkVertexInputBindingDescription* Binding = new(VBBindings) VkVertexInputBindingDescription;
		SourceBinding.WriteInto(*Binding);
	}
	VBInfo.vertexBindingDescriptionCount = VBBindings.Num();
	VBInfo.pVertexBindingDescriptions = VBBindings.GetData();
	TArray<VkVertexInputAttributeDescription> VBAttributes;
	for (const FGfxPipelineDesc::FVertexAttribute& SourceAttr : GfxEntry->VertexAttributes)
	{
		VkVertexInputAttributeDescription* Attr = new(VBAttributes) VkVertexInputAttributeDescription;
		SourceAttr.WriteInto(*Attr);
	}
	VBInfo.vertexAttributeDescriptionCount = VBAttributes.Num();
	VBInfo.pVertexAttributeDescriptions = VBAttributes.GetData();
	PipelineInfo.pVertexInputState = &VBInfo;

	PipelineInfo.pColorBlendState = &CBInfo;
	PipelineInfo.pMultisampleState = &MSInfo;
	PipelineInfo.pViewportState = &VPInfo;

	PipelineInfo.renderPass = PSO->RenderPass->GetHandle();
	PipelineInfo.subpass = GfxEntry->SubpassIndex;

	VkPipelineInputAssemblyStateCreateInfo InputAssembly;
	ZeroVulkanStruct(InputAssembly, VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
	InputAssembly.topology = (VkPrimitiveTopology)GfxEntry->Topology;

	PipelineInfo.pInputAssemblyState = &InputAssembly;

	VkPipelineRasterizationStateCreateInfo RasterizerState;
	FVulkanRasterizerState::ResetCreateInfo(RasterizerState);
	GfxEntry->Rasterizer.WriteInto(RasterizerState);

	VkPipelineDepthStencilStateCreateInfo DepthStencilState;
	ZeroVulkanStruct(DepthStencilState, VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
	GfxEntry->DepthStencil.WriteInto(DepthStencilState);

	PipelineInfo.pRasterizationState = &RasterizerState;
	PipelineInfo.pDepthStencilState = &DepthStencilState;

	VkPipelineDynamicStateCreateInfo DynamicState;
	ZeroVulkanStruct(DynamicState, VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
	VkDynamicState DynamicStatesEnabled[VK_DYNAMIC_STATE_RANGE_SIZE];
	DynamicState.pDynamicStates = DynamicStatesEnabled;
	FMemory::Memzero(DynamicStatesEnabled);
	DynamicStatesEnabled[DynamicState.dynamicStateCount++] = VK_DYNAMIC_STATE_VIEWPORT;
	DynamicStatesEnabled[DynamicState.dynamicStateCount++] = VK_DYNAMIC_STATE_SCISSOR;
	DynamicStatesEnabled[DynamicState.dynamicStateCount++] = VK_DYNAMIC_STATE_STENCIL_REFERENCE;
	DynamicStatesEnabled[DynamicState.dynamicStateCount++] = VK_DYNAMIC_STATE_DEPTH_BOUNDS;

	PipelineInfo.pDynamicState = &DynamicState;

	VkPipelineTessellationStateCreateInfo TessState;
	if (bHasTessellation)
	{
		ZeroVulkanStruct(TessState, VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO);
		PipelineInfo.pTessellationState = &TessState;
		check(InputAssembly.topology == VK_PRIMITIVE_TOPOLOGY_PATCH_LIST);
		TessState.patchControlPoints = GfxEntry->ControlPoints;
	}

	VkResult Result = VK_ERROR_INITIALIZATION_FAILED;
	double BeginTime = FPlatformTime::Seconds();
	if(bUseLRU)
	{
#if VULKAN_USE_SHADERKEYS
		const uint64 ShaderHash = GfxEntry->ShaderKeyShared;
#else
		const uint64 ShaderHash = GfxEntry->ShaderHashes.Hash;
#endif
		FVulkanPipelineSize* Found;
		{
			FScopeLock Lock(&LRUCS); 
			Found = LRU2SizeList.Find(ShaderHash);
		}
		size_t PreSize = 0, AfterSize = 0;
		uint32 FoundSize = 0;
		if (Found)
		{
			FoundSize = Found->PipelineSize;
		}
		else
		{
			VulkanRHI::vkGetPipelineCacheData(Device->GetInstanceHandle(), PipelineCache, &PreSize, nullptr);
		}


		{
			SCOPE_CYCLE_COUNTER(STAT_VulkanPSOVulkanCreationTime);
			Result = VulkanRHI::vkCreateGraphicsPipelines(Device->GetInstanceHandle(), PipelineCache, 1, &PipelineInfo, VULKAN_CPU_ALLOCATOR, Pipeline);
		}

		if (!Found && Result == VK_SUCCESS)
		{
			VulkanRHI::vkGetPipelineCacheData(Device->GetInstanceHandle(), PipelineCache, &AfterSize, nullptr);
			uint32 Diff = AfterSize - PreSize;
			if (!Diff)
			{
				UE_LOG(LogVulkanRHI, Warning, TEXT("Shader size was computed as zero, using 20k instead."));
				Diff = 20 * 1024;
			}
			FVulkanPipelineSize PipelineSize;
			PipelineSize.ShaderHash = ShaderHash;
			PipelineSize.PipelineSize = Diff;
			{
				FScopeLock Lock(&LRUCS);
				LRU2SizeList.Add(ShaderHash, PipelineSize);
			}
			FoundSize = Diff;
		}
		if(Result == VK_SUCCESS)
		{
			PSO->PipelineCacheSize = FoundSize;
		}
	}
	else
	{
		SCOPE_CYCLE_COUNTER(STAT_VulkanPSOVulkanCreationTime);
		Result = VulkanRHI::vkCreateGraphicsPipelines(Device->GetInstanceHandle(), PipelineCache, 1, &PipelineInfo, VULKAN_CPU_ALLOCATOR, Pipeline);
	}


	if (Result != VK_SUCCESS)
	{
		UE_LOG(LogVulkanRHI, Error, TEXT("Failed to create graphics pipeline."));
		return false;
	}

	double EndTime = FPlatformTime::Seconds();
	double Delta = EndTime - BeginTime;
	if (Delta > HitchTime)
	{
		UE_LOG(LogVulkanRHI, Verbose, TEXT("Hitchy gfx pipeline key CS (%.3f ms)"), (float)(Delta * 1000.0));
	}

	INC_DWORD_STAT(STAT_VulkanNumPSOs);
	return true;
}


void FVulkanPipelineStateCacheManager::DestroyCache()
{
	VkDevice DeviceHandle = Device->GetInstanceHandle();

	FScopeLock Lock1(&GraphicsPSOLockedCS);
	int idx = 0;
	for (auto& Pair : GraphicsPSOLockedMap)
	{
		FVulkanRHIGraphicsPipelineState* Pipeline = Pair.Value;
		LRUPRINT_DEBUG(TEXT("LIVE PSO %05d  REF %d %p :: %p\n"), idx++, Pipeline->GetRefCount(), Pipeline, Pipeline->LRUNode);
	}
	LRU2SizeList.Reset();

#if LRU_DEBUG
	LRUDump();
#endif

	// Compute pipelines already deleted...
	ComputePipelineEntries.Reset();
	check(GraphicsPSOLockedMap.Num() == 0);
}

void FVulkanPipelineStateCacheManager::RebuildCache()
{
	if (IsInGameThread())
	{
		FlushRenderingCommands();
	}
	DestroyCache();
}

FVulkanShaderHashes::FVulkanShaderHashes(const FGraphicsPipelineStateInitializer& PSOInitializer)
{
	Stages[ShaderStage::Vertex] = GetShaderHash<FRHIVertexShader, FVulkanVertexShader>(PSOInitializer.BoundShaderState.VertexShaderRHI);
	Stages[ShaderStage::Pixel] = GetShaderHash<FRHIPixelShader, FVulkanPixelShader>(PSOInitializer.BoundShaderState.PixelShaderRHI);
#if VULKAN_SUPPORTS_GEOMETRY_SHADERS
	Stages[ShaderStage::Geometry] = GetShaderHash<FRHIGeometryShader, FVulkanGeometryShader>(PSOInitializer.BoundShaderState.GeometryShaderRHI);
#endif
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
	Stages[ShaderStage::Hull] = GetShaderHash<FRHIHullShader, FVulkanHullShader>(PSOInitializer.BoundShaderState.HullShaderRHI);
	Stages[ShaderStage::Domain] = GetShaderHash<FRHIDomainShader, FVulkanDomainShader>(PSOInitializer.BoundShaderState.DomainShaderRHI);
#endif
	Finalize();
}

FVulkanShaderHashes::FVulkanShaderHashes()
{
	FMemory::Memzero(Stages);
	Hash = 0;
}

FVulkanLayout* FVulkanPipelineStateCacheManager::FindOrAddLayout(const FVulkanDescriptorSetsLayoutInfo& DescriptorSetLayoutInfo, bool bGfxLayout)
{
	FScopeLock Lock(&LayoutMapCS);
	if (FVulkanLayout** FoundLayout = LayoutMap.Find(DescriptorSetLayoutInfo))
	{
		check(bGfxLayout == (*FoundLayout)->IsGfxLayout());
		return *FoundLayout;
	}

	FVulkanLayout* Layout = nullptr;
	FVulkanGfxLayout* GfxLayout = nullptr;

	if (bGfxLayout)
	{
		GfxLayout = new FVulkanGfxLayout(Device);
		Layout = GfxLayout;
	}
	else
	{
		Layout = new FVulkanComputeLayout(Device);
	}

	Layout->DescriptorSetLayout.CopyFrom(DescriptorSetLayoutInfo);
	Layout->Compile(DSetLayoutMap);

	if(GfxLayout)
	{
		GfxLayout->GfxPipelineDescriptorInfo.Initialize(GfxLayout->GetDescriptorSetsLayout().RemappingInfo);
	}


	LayoutMap.Add(DescriptorSetLayoutInfo, Layout);
	return Layout;
}

static inline VkPrimitiveTopology UEToVulkanTopologyType(const FVulkanDevice* InDevice, EPrimitiveType PrimitiveType, bool bHasTessellation, uint16& OutControlPoints)
{
	if (bHasTessellation)
	{
		switch (PrimitiveType)
		{
		case PT_TriangleList:
			// This is the case for tessellation without AEN or other buffers, so just flip to 3 CPs
			OutControlPoints = 3;
			return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
		case PT_1_ControlPointPatchList:
		case PT_2_ControlPointPatchList:
		case PT_3_ControlPointPatchList:
		case PT_4_ControlPointPatchList:
		case PT_5_ControlPointPatchList:
		case PT_6_ControlPointPatchList:
		case PT_7_ControlPointPatchList:
		case PT_8_ControlPointPatchList:
		case PT_9_ControlPointPatchList:
		case PT_10_ControlPointPatchList:
		case PT_12_ControlPointPatchList:
		case PT_13_ControlPointPatchList:
		case PT_14_ControlPointPatchList:
		case PT_15_ControlPointPatchList:
		case PT_16_ControlPointPatchList:
		case PT_17_ControlPointPatchList:
		case PT_18_ControlPointPatchList:
		case PT_19_ControlPointPatchList:
		case PT_20_ControlPointPatchList:
		case PT_22_ControlPointPatchList:
		case PT_23_ControlPointPatchList:
		case PT_24_ControlPointPatchList:
		case PT_25_ControlPointPatchList:
		case PT_26_ControlPointPatchList:
		case PT_27_ControlPointPatchList:
		case PT_28_ControlPointPatchList:
		case PT_29_ControlPointPatchList:
		case PT_30_ControlPointPatchList:
		case PT_31_ControlPointPatchList:
		case PT_32_ControlPointPatchList:
			OutControlPoints = (PrimitiveType - PT_1_ControlPointPatchList + 1);
			checkf(
				OutControlPoints <= InDevice->GetLimits().maxTessellationPatchSize,
				TEXT("OutControlPoints (%d) exceeded limit of maximal patch size (%d)"),
				OutControlPoints,
				InDevice->GetLimits().maxTessellationPatchSize
			);
			return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
		default:
			checkf(false, TEXT("Unsupported tessellation EPrimitiveType %d; probably missing a case in FStaticMeshSceneProxy::GetMeshElement()!"), (uint32)PrimitiveType);
			break;
		}
		OutControlPoints = 0;
	}
	else
	{
		OutControlPoints = 0;
		switch (PrimitiveType)
		{
		case PT_PointList:
			return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
		case PT_LineList:
			return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
		case PT_TriangleList:
			return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		case PT_TriangleStrip:
			return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
		case PT_1_ControlPointPatchList:
		case PT_2_ControlPointPatchList:
		case PT_3_ControlPointPatchList:
		case PT_4_ControlPointPatchList:
		case PT_5_ControlPointPatchList:
		case PT_6_ControlPointPatchList:
		case PT_7_ControlPointPatchList:
		case PT_8_ControlPointPatchList:
		case PT_9_ControlPointPatchList:
		case PT_10_ControlPointPatchList:
		case PT_12_ControlPointPatchList:
		case PT_13_ControlPointPatchList:
		case PT_14_ControlPointPatchList:
		case PT_15_ControlPointPatchList:
		case PT_16_ControlPointPatchList:
		case PT_17_ControlPointPatchList:
		case PT_18_ControlPointPatchList:
		case PT_19_ControlPointPatchList:
		case PT_20_ControlPointPatchList:
		case PT_22_ControlPointPatchList:
		case PT_23_ControlPointPatchList:
		case PT_24_ControlPointPatchList:
		case PT_25_ControlPointPatchList:
		case PT_26_ControlPointPatchList:
		case PT_27_ControlPointPatchList:
		case PT_28_ControlPointPatchList:
		case PT_29_ControlPointPatchList:
		case PT_30_ControlPointPatchList:
		case PT_31_ControlPointPatchList:
		case PT_32_ControlPointPatchList:
			OutControlPoints = (PrimitiveType - PT_1_ControlPointPatchList + 1);
			checkf(false, TEXT("Missing tessellation shaders, however tried to use EPrimitiveType %d (%d control points)"), (uint32)PrimitiveType, OutControlPoints);
			break;
		default:
			checkf(false, TEXT("Unsupported EPrimitiveType %d"), (uint32)PrimitiveType);
			break;
		}
	}

	return VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
}

void FVulkanPipelineStateCacheManager::CreateGfxEntry(const FGraphicsPipelineStateInitializer& PSOInitializer, FVulkanDescriptorSetsLayoutInfo& DescriptorSetLayoutInfo, FGfxPipelineDesc* Desc)
{
	FGfxPipelineDesc* OutGfxEntry = Desc;

	FVulkanShader* Shaders[ShaderStage::NumStages];
	GetVulkanShaders(PSOInitializer.BoundShaderState, Shaders);

	FVulkanVertexInputStateInfo VertexInputState;
	
	{

			const FBoundShaderStateInput& BSI = PSOInitializer.BoundShaderState;

			const FVulkanShaderHeader& VSHeader = Shaders[ShaderStage::Vertex]->GetCodeHeader();
			VertexInputState.Generate(ResourceCast(PSOInitializer.BoundShaderState.VertexDeclarationRHI), VSHeader.InOutMask);

			FUniformBufferGatherInfo UBGatherInfo;

			DescriptorSetLayoutInfo.ProcessBindingsForStage(VK_SHADER_STAGE_VERTEX_BIT, ShaderStage::Vertex, VSHeader, UBGatherInfo);

			if (Shaders[ShaderStage::Pixel])
			{
				const FVulkanShaderHeader& PSHeader = Shaders[ShaderStage::Pixel]->GetCodeHeader();
				DescriptorSetLayoutInfo.ProcessBindingsForStage(VK_SHADER_STAGE_FRAGMENT_BIT, ShaderStage::Pixel, PSHeader, UBGatherInfo);
			}

#if VULKAN_SUPPORTS_GEOMETRY_SHADERS
			if (Shaders[ShaderStage::Geometry])
			{
				const FVulkanShaderHeader& GSHeader = Shaders[ShaderStage::Geometry]->GetCodeHeader();
				DescriptorSetLayoutInfo.ProcessBindingsForStage(VK_SHADER_STAGE_GEOMETRY_BIT, ShaderStage::Geometry, GSHeader, UBGatherInfo);
			}
#endif

#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
			if (Shaders[ShaderStage::Hull])
			{
				const FVulkanShaderHeader& HSHeader = Shaders[ShaderStage::Hull]->GetCodeHeader();
				const FVulkanShaderHeader& DSHeader = Shaders[ShaderStage::Domain]->GetCodeHeader();
				DescriptorSetLayoutInfo.ProcessBindingsForStage(VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, ShaderStage::Hull, HSHeader, UBGatherInfo);
				DescriptorSetLayoutInfo.ProcessBindingsForStage(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, ShaderStage::Domain, DSHeader, UBGatherInfo);
			}
#endif
			// Second pass
			const int32 NumImmutableSamplers = PSOInitializer.ImmutableSamplerState.ImmutableSamplers.Num();
			TArrayView<FRHISamplerState*> ImmutableSamplers(NumImmutableSamplers > 0 ? &(FRHISamplerState*&)PSOInitializer.ImmutableSamplerState.ImmutableSamplers[0] : nullptr, NumImmutableSamplers);
			DescriptorSetLayoutInfo.FinalizeBindings<false>(UBGatherInfo, ImmutableSamplers);
	}

	FDescriptorSetRemappingInfo& RemappingInfo = DescriptorSetLayoutInfo.RemappingInfo;

	if (RemappingInfo.InputAttachmentData.Num())
	{
		// input attachements can't exist in a first sub-pass
		check(PSOInitializer.SubpassHint != ESubpassHint::None); 
		check(PSOInitializer.SubpassIndex != 0);
	}
	OutGfxEntry->SubpassIndex = PSOInitializer.SubpassIndex;

	FVulkanBlendState* BlendState = ResourceCast(PSOInitializer.BlendState);

	OutGfxEntry->UseAlphaToCoverage = PSOInitializer.NumSamples > 1 && BlendState->Initializer.bUseAlphaToCoverage ? 1 : 0;

	const bool bHasTessellation = (PSOInitializer.BoundShaderState.DomainShaderRHI != nullptr);

	OutGfxEntry->RasterizationSamples = PSOInitializer.NumSamples;
	OutGfxEntry->Topology = (uint32)UEToVulkanTopologyType(Device, PSOInitializer.PrimitiveType, bHasTessellation, OutGfxEntry->ControlPoints);
	uint32 NumRenderTargets = PSOInitializer.ComputeNumValidRenderTargets();
	
	if (PSOInitializer.SubpassHint == ESubpassHint::DeferredShadingSubpass && PSOInitializer.SubpassIndex >= 2)
	{
		// GBuffer attachements are not used as output in a shading sub-pass
		// Only SceneColor is used as a color attachment
		NumRenderTargets = 1;
	}

	OutGfxEntry->ColorAttachmentStates.AddUninitialized(NumRenderTargets);
	for (int32 Index = 0; Index < OutGfxEntry->ColorAttachmentStates.Num(); ++Index)
	{
		OutGfxEntry->ColorAttachmentStates[Index].ReadFrom(BlendState->BlendStates[Index]);
	}

	{
		const VkPipelineVertexInputStateCreateInfo& VBInfo = VertexInputState.GetInfo();
		OutGfxEntry->VertexBindings.AddUninitialized(VBInfo.vertexBindingDescriptionCount);
		for (uint32 Index = 0; Index < VBInfo.vertexBindingDescriptionCount; ++Index)
		{
			OutGfxEntry->VertexBindings[Index].ReadFrom(VBInfo.pVertexBindingDescriptions[Index]);
		}

		OutGfxEntry->VertexAttributes.AddUninitialized(VBInfo.vertexAttributeDescriptionCount);
		for (uint32 Index = 0; Index < VBInfo.vertexAttributeDescriptionCount; ++Index)
		{
			OutGfxEntry->VertexAttributes[Index].ReadFrom(VBInfo.pVertexAttributeDescriptions[Index]);
		}
	}

	const TArray<FVulkanDescriptorSetsLayout::FSetLayout>& Layouts = DescriptorSetLayoutInfo.GetLayouts();
	OutGfxEntry->DescriptorSetLayoutBindings.AddDefaulted(Layouts.Num());
	for (int32 Index = 0; Index < Layouts.Num(); ++Index)
	{
		for (int32 SubIndex = 0; SubIndex < Layouts[Index].LayoutBindings.Num(); ++SubIndex)
		{
			FDescriptorSetLayoutBinding* Binding = new(OutGfxEntry->DescriptorSetLayoutBindings[Index]) FDescriptorSetLayoutBinding;
			Binding->ReadFrom(Layouts[Index].LayoutBindings[SubIndex]);
		}
	}

	OutGfxEntry->Rasterizer.ReadFrom(ResourceCast(PSOInitializer.RasterizerState)->RasterizerState);
	{
		VkPipelineDepthStencilStateCreateInfo DSInfo;
		ResourceCast(PSOInitializer.DepthStencilState)->SetupCreateInfo(PSOInitializer, DSInfo);
		OutGfxEntry->DepthStencil.ReadFrom(DSInfo);
	}

	int32 NumShaders = 0;
#if VULKAN_USE_SHADERKEYS
	uint64 SharedKey = 0;
	uint64 Primes[] = {
		6843488303525203279llu,
		3095754086865563867llu,
		8242695776924673527llu,
		7556751872809527943llu,
		8278265491465149053llu,
	};
	check(sizeof(Primes) / sizeof(Primes[0]) >= ShaderStage::NumStages);
	for (int32 Index = 0; Index < ShaderStage::NumStages; ++Index)
	{
		FVulkanShader* Shader = Shaders[Index];
		uint64 Key = 0;
		if (Shader)
		{
			Key = Shader->GetShaderKey();
			++NumShaders;
		}
		OutGfxEntry->ShaderKeys[Index] = Key;
		SharedKey += Key * Primes[Index];
	}
	OutGfxEntry->ShaderKeyShared = SharedKey;
#else
	for (int32 Index = 0; Index < ShaderStage::NumStages; ++Index)
	{
		FVulkanShader* Shader = Shaders[Index];
		if (Shader)
		{
			check(Shader->Spirv.Num() != 0);

			FSHAHash Hash = GetShaderHashForStage(PSOInitializer, (ShaderStage::EStage)Index);
			OutGfxEntry->ShaderHashes.Stages[Index] = Hash;

			++NumShaders;
		}
	}
	OutGfxEntry->ShaderHashes.Finalize();
#endif
	check(NumShaders > 0);

#if VULKAN_SUPPORTS_COLOR_CONVERSIONS
	for (uint32 Index = 0; Index < MaxImmutableSamplers; ++Index)
	{
		OutGfxEntry->ImmutableSamplers[Index] = reinterpret_cast<SIZE_T>(PSOInitializer.ImmutableSamplerState.ImmutableSamplers[Index]);
	}
#endif


	FVulkanRenderTargetLayout RTLayout(PSOInitializer);
	OutGfxEntry->RenderTargets.ReadFrom(RTLayout);
}





FVulkanRHIGraphicsPipelineState::FVulkanRHIGraphicsPipelineState(FVulkanDevice* Device, const FGraphicsPipelineStateInitializer& PSOInitializer_, FGfxPipelineDesc& Desc, FVulkanPSOKey* VulkanKey)
	: bIsRegistered(false)
	, PrimitiveType(PSOInitializer_.PrimitiveType)
	, VulkanPipeline(0)
	, Device(Device)
	, Desc(Desc)
	, VulkanKey(VulkanKey->CopyDeep())
{
#if !UE_BUILD_SHIPPING
	SGraphicsRHICount++;
#endif

	FMemory::Memset(VulkanShaders, 0, sizeof(VulkanShaders));
	VulkanShaders[ShaderStage::Vertex] = static_cast<FVulkanVertexShader*>(PSOInitializer_.BoundShaderState.VertexShaderRHI);
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
	VulkanShaders[ShaderStage::Hull] = static_cast<FVulkanHullShader*>(PSOInitializer_.BoundShaderState.HullShaderRHI);
	VulkanShaders[ShaderStage::Domain] = static_cast<FVulkanDomainShader*>(PSOInitializer_.BoundShaderState.DomainShaderRHI);
#endif
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
	VulkanShaders[ShaderStage::Geometry] = static_cast<FVulkanGeometryShader*>(PSOInitializer_.BoundShaderState.GeometryShaderRHI);
#endif
	VulkanShaders[ShaderStage::Pixel] = static_cast<FVulkanPixelShader*>(PSOInitializer_.BoundShaderState.PixelShaderRHI);

	for (int ShaderStageIndex = 0; ShaderStageIndex < ShaderStage::NumStages; ShaderStageIndex++)
	{
		if (VulkanShaders[ShaderStageIndex] != nullptr)
		{
			VulkanShaders[ShaderStageIndex]->AddRef();
		}
	}

#if VULKAN_PSO_CACHE_DEBUG
	PixelShaderRHI = PSOInitializer_.BoundShaderState.PixelShaderRHI;
	VertexShaderRHI = PSOInitializer_.BoundShaderState.VertexShaderRHI;
	VertexDeclarationRHI = PSOInitializer_.BoundShaderState.VertexDeclarationRHI;

#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
	DomainShaderRHI = PSOInitializer_.BoundShaderState.DomainShaderRHI;
	HullShaderRHI = PSOInitializer_.BoundShaderState.HullShaderRHI;
#endif 

#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
	GeometryShaderRHI = PSOInitializer_.BoundShaderState.GeometryShaderRHI;
#endif

	PSOInitializer = PSOInitializer_;
#endif
	FMemory::Memset(ShaderModules, 0, sizeof(ShaderModules));
	INC_DWORD_STAT(STAT_VulkanNumGraphicsPSOs);
	INC_DWORD_STAT_BY(STAT_VulkanPSOKeyMemory, this->VulkanKey.GetDataRef().Num());
}

void FVulkanPipelineStateCacheManager::NotifyDeletedGraphicsPSO(FRHIGraphicsPipelineState* PSO)
{
	FVulkanRHIGraphicsPipelineState* VkPSO = (FVulkanRHIGraphicsPipelineState*)PSO;
	Device->NotifyDeletedGfxPipeline(VkPSO);
	FVulkanPSOKey& Key = VkPSO->VulkanKey;
	DEC_DWORD_STAT_BY(STAT_VulkanPSOKeyMemory, Key.GetDataRef().Num());
	if(VkPSO->bIsRegistered)
	{
		FScopeLock Lock(&GraphicsPSOLockedCS);
		FVulkanRHIGraphicsPipelineState** Contained = GraphicsPSOLockedMap.Find(Key);
		check(Contained && *Contained == PSO);
		GraphicsPSOLockedMap.Remove(Key);
		VkPSO->bIsRegistered = false;
		if(bUseLRU)
		{
			LRURemove(*Contained);
			check((*Contained)->LRUNode == 0);
		}
		else
		{
			(*Contained)->DeleteVkPipeline(true);
			check(VkPSO->GetVulkanPipeline() == 0 );
		}
		
	}
	else
	{
		FScopeLock Lock(&GraphicsPSOLockedCS);
		FVulkanRHIGraphicsPipelineState** Contained = GraphicsPSOLockedMap.Find(Key);
		if (Contained && *Contained == VkPSO)
		{
			check(0);
		}
		VkPSO->DeleteVkPipeline(true);
	}
}


//Global lock for PSO creation, only enabled if GVulkanPSOForceSingleThreaded is 1
struct FPSOGlobalLock
{
	FCriticalSection* CriticalSection;
	FPSOGlobalLock(FCriticalSection* InSynchObject)
	{
		
		CriticalSection = GVulkanPSOForceSingleThreaded ? InSynchObject : nullptr;
		if (CriticalSection)
		{
			CriticalSection->Lock();
		}
	}
	~FPSOGlobalLock()
	{
		if (CriticalSection)
		{
			CriticalSection->Unlock();
		}
	}

};

FVulkanRHIGraphicsPipelineState* FVulkanPipelineStateCacheManager::RHICreateGraphicsPipelineState(const FGraphicsPipelineStateInitializer& Initializer)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_Vulkan_RHICreateGraphicsPipelineState_NEW);
	FPSOGlobalLock GlobalLock(&GraphicsPSOLockedCS);
	FVulkanPSOKey Key;
	FGfxPipelineDesc Desc;
	FVulkanDescriptorSetsLayoutInfo DescriptorSetLayoutInfo;
	{

		SCOPE_CYCLE_COUNTER(STAT_VulkanPSOHeaderInitTime);
		CreateGfxEntry(Initializer, DescriptorSetLayoutInfo, &Desc);
		Key = Desc.CreateKey2();
	}


	FVulkanRHIGraphicsPipelineState* NewPSO = 0;
	{
		SCOPE_CYCLE_COUNTER(STAT_VulkanPSOLookupTime);
		FScopeLock Lock(&GraphicsPSOLockedCS);
		{
			FVulkanRHIGraphicsPipelineState** PSO = GraphicsPSOLockedMap.Find(Key);
			if(PSO)
			{
				check(*PSO);
				LRUTouch(*PSO);
				return *PSO;
			}
		}
	}



	{
		SCOPE_CYCLE_COUNTER(STAT_VulkanPSOCreationTime);
		NewPSO = new FVulkanRHIGraphicsPipelineState(Device, Initializer, Desc, &Key);
		{

			FVulkanLayout* Layout = FindOrAddLayout(DescriptorSetLayoutInfo, true);
			FVulkanGfxLayout* GfxLayout = (FVulkanGfxLayout*)Layout;
			check(GfxLayout->GfxPipelineDescriptorInfo.IsInitialized());
			NewPSO->Layout = GfxLayout;
			NewPSO->bHasInputAttachments = GfxLayout->GetDescriptorSetsLayout().HasInputAttachments();
		}
		NewPSO->RenderPass = Device->GetImmediateContext().PrepareRenderPassForPSOCreation(Initializer);
		{
			const FBoundShaderStateInput& BSI = Initializer.BoundShaderState;
			for (int32 StageIdx = 0; StageIdx < ShaderStage::NumStages; ++StageIdx)
			{
				NewPSO->ShaderKeys[StageIdx] = GetShaderKeyForGfxStage(BSI, (ShaderStage::EStage)StageIdx);
			}

			check(BSI.VertexShaderRHI);
			FVulkanVertexShader* VS = ResourceCast(BSI.VertexShaderRHI);
			const FVulkanShaderHeader& VSHeader = VS->GetCodeHeader();
			NewPSO->VertexInputState.Generate(ResourceCast(Initializer.BoundShaderState.VertexDeclarationRHI), VSHeader.InOutMask);

			if((!Initializer.bFromPSOFileCache || !LRUEvictImmediately()) 
	#if !UE_BUILD_SHIPPING
				&& 0 == CVarPipelineDebugForceEvictImmediately.GetValueOnAnyThread()
	#endif
				)
			{

				// Create the pipeline
				double BeginTime = FPlatformTime::Seconds();
				FVulkanShader* VulkanShaders[ShaderStage::NumStages];
				GetVulkanShaders(Initializer.BoundShaderState, VulkanShaders);

				for (int32 StageIdx = 0; StageIdx < ShaderStage::NumStages; ++StageIdx)
				{
					uint64 key = GetShaderKeyForGfxStage(BSI, (ShaderStage::EStage)StageIdx);
					check(key == NewPSO->ShaderKeys[StageIdx]);
				}

			
				QUICK_SCOPE_CYCLE_COUNTER(STAT_Vulkan_RHICreateGraphicsPipelineState_CREATE_PART0);

				if(!CreateGfxPipelineFromEntry(NewPSO, VulkanShaders, &NewPSO->VulkanPipeline))
				{
					delete NewPSO;
					return nullptr;
				}
				// Recover if we failed to create the pipeline.
				double EndTime = FPlatformTime::Seconds();
				double Delta = EndTime - BeginTime;
				if (Delta > HitchTime)
				{
					UE_LOG(LogVulkanRHI, Verbose, TEXT("Hitchy gfx pipeline (%.3f ms)"), (float)(Delta * 1000.0));
				}
			}
			FScopeLock Lock(&GraphicsPSOLockedCS); 
			FVulkanRHIGraphicsPipelineState** MapPSO = GraphicsPSOLockedMap.Find(Key);
			if(MapPSO)//another thread could end up creating it.
			{
				delete NewPSO;
				NewPSO = *MapPSO;
			}
			else
			{
				GraphicsPSOLockedMap.Add(MoveTemp(Key), NewPSO);
				if (bUseLRU)
				{
					FScopeLock LockRU(&LRUCS);
					NewPSO->bIsRegistered = true;
					LRUTrim(NewPSO->PipelineCacheSize);
					LRUAdd(NewPSO);
				}
				else
				{
					NewPSO->bIsRegistered = true;
				}


			}
		}
	}
	return NewPSO;
}


FGraphicsPipelineStateRHIRef FVulkanDynamicRHI::RHICreateGraphicsPipelineState(const FGraphicsPipelineStateInitializer& PSOInitializer)
{
#if VULKAN_ENABLE_AGGRESSIVE_STATS
	SCOPE_CYCLE_COUNTER(STAT_VulkanGetOrCreatePipeline);
#endif
	QUICK_SCOPE_CYCLE_COUNTER(STAT_Vulkan_RHICreateGraphicsPipelineState);
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanShaders);

	return Device->PipelineStateCache->RHICreateGraphicsPipelineState(PSOInitializer);
}




FVulkanComputePipeline* FVulkanPipelineStateCacheManager::GetOrCreateComputePipeline(FVulkanComputeShader* ComputeShader)
{
	check(ComputeShader);
	const uint64 Key = ComputeShader->GetShaderKey();
	{
		FRWScopeLock ScopeLock(ComputePipelineLock, SLT_ReadOnly);
		FVulkanComputePipeline** ComputePipelinePtr = ComputePipelineEntries.Find(Key);
		if (ComputePipelinePtr)
		{
			return *ComputePipelinePtr;
		}
	}

	// create pipeline of entry + store entry
	double BeginTime = FPlatformTime::Seconds();

	FVulkanComputePipeline* ComputePipeline = CreateComputePipelineFromShader(ComputeShader);

	double EndTime = FPlatformTime::Seconds();
	double Delta = EndTime - BeginTime;
	if (Delta > HitchTime)
	{
		UE_LOG(LogVulkanRHI, Verbose, TEXT("Hitchy compute pipeline key CS (%.3f ms)"), (float)(Delta * 1000.0));
	}

	{
		FRWScopeLock ScopeLock(ComputePipelineLock, SLT_Write);
		if(0 == ComputePipelineEntries.Find(Key))
		{
			ComputePipelineEntries.FindOrAdd(Key) = ComputePipeline;
		}
	}

	return ComputePipeline;
}

FVulkanComputePipeline* FVulkanPipelineStateCacheManager::CreateComputePipelineFromShader(FVulkanComputeShader* Shader)
{
	FVulkanComputePipeline* Pipeline = new FVulkanComputePipeline(Device);

	Pipeline->ComputeShader = Shader;
	Pipeline->ComputeShader->AddRef();

	FVulkanDescriptorSetsLayoutInfo DescriptorSetLayoutInfo;
	const FVulkanShaderHeader& CSHeader = Shader->GetCodeHeader();
	FUniformBufferGatherInfo UBGatherInfo;
	DescriptorSetLayoutInfo.ProcessBindingsForStage(VK_SHADER_STAGE_COMPUTE_BIT, ShaderStage::Compute, CSHeader, UBGatherInfo);
	DescriptorSetLayoutInfo.FinalizeBindings<true>(UBGatherInfo, TArrayView<FRHISamplerState*>());
	FVulkanLayout* Layout = FindOrAddLayout(DescriptorSetLayoutInfo, false);
	FVulkanComputeLayout* ComputeLayout = (FVulkanComputeLayout*)Layout;
	if (!ComputeLayout->ComputePipelineDescriptorInfo.IsInitialized())
	{
		ComputeLayout->ComputePipelineDescriptorInfo.Initialize(Layout->GetDescriptorSetsLayout().RemappingInfo);
	}

	VkShaderModule ShaderModule = Shader->GetOrCreateHandle(Layout, Layout->GetDescriptorSetLayoutHash());

	VkComputePipelineCreateInfo PipelineInfo;
	ZeroVulkanStruct(PipelineInfo, VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);
	PipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	PipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	PipelineInfo.stage.module = ShaderModule;
	// main_00000000_00000000
	ANSICHAR EntryPoint[24];
	Shader->GetEntryPoint(EntryPoint, 24);
	PipelineInfo.stage.pName = EntryPoint;
	PipelineInfo.layout = ComputeLayout->GetPipelineLayout();
		
	VERIFYVULKANRESULT(VulkanRHI::vkCreateComputePipelines(Device->GetInstanceHandle(), PipelineCache, 1, &PipelineInfo, VULKAN_CPU_ALLOCATOR, &Pipeline->Pipeline));

	Pipeline->Layout = ComputeLayout;

	INC_DWORD_STAT(STAT_VulkanNumPSOs);

	return Pipeline;
}

template<typename T>
static void SerializeArray(FArchive& Ar, TArray<T>& Array)
{
	int32 Num = Array.Num();
	Ar << Num;
	if (Ar.IsLoading())
	{
		Array.SetNum(Num);
		for (int32 Index = 0; Index < Num; ++Index)
		{
			Ar << Array[Index];
		}
	}
	else
	{
		for (int32 Index = 0; Index < Num; ++Index)
		{
			Ar << Array[Index];
		}
	}
}


void FVulkanPipelineStateCacheManager::FVulkanLRUCacheFile::Save(FArchive& Ar)
{
	// Modify VERSION if serialization changes
	Ar << Header.Version;
	Ar << Header.SizeOfPipelineSizes;

	SerializeArray(Ar, PipelineSizes);
}

bool FVulkanPipelineStateCacheManager::FVulkanLRUCacheFile::Load(FArchive& Ar)
{
	// Modify VERSION if serialization changes
	Ar << Header.Version;
	if (Header.Version != LRU_CACHE_VERSION)
	{
		UE_LOG(LogVulkanRHI, Warning, TEXT("Unable to load lru pipeline cache due to mismatched Version %d != %d"), Header.Version, (int32)LRU_CACHE_VERSION);
		return false;
	}

	Ar << Header.SizeOfPipelineSizes;
	if (Header.SizeOfPipelineSizes != (int32)(sizeof(FVulkanPipelineSize)))
	{
		UE_LOG(LogVulkanRHI, Warning, TEXT("Unable to load lru pipeline cache due to mismatched size of FVulkanPipelineSize %d != %d; forgot to bump up LRU_CACHE_VERSION?"), Header.SizeOfPipelineSizes, (int32)sizeof(FVulkanPipelineSize));
		return false;
	}

	SerializeArray(Ar, PipelineSizes);
	return true;
}



void GetVulkanShaders(const FBoundShaderStateInput& BSI, FVulkanShader* OutShaders[ShaderStage::NumStages])
{
	FMemory::Memzero(OutShaders, ShaderStage::NumStages * sizeof(*OutShaders));

	OutShaders[ShaderStage::Vertex] = ResourceCast(BSI.VertexShaderRHI);

	if (BSI.PixelShaderRHI)
	{
		OutShaders[ShaderStage::Pixel] = ResourceCast(BSI.PixelShaderRHI);
	}

#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
	if (BSI.GeometryShaderRHI)
	{
#if VULKAN_SUPPORTS_GEOMETRY_SHADERS
		OutShaders[ShaderStage::Geometry] = ResourceCast(BSI.GeometryShaderRHI);
#else
		ensureMsgf(0, TEXT("Geometry not supported!"));
#endif
	}
#endif

#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
	if (BSI.HullShaderRHI)
	{
		// Can't have Hull w/o Domain
		check(BSI.DomainShaderRHI);
		OutShaders[ShaderStage::Hull] = ResourceCast(BSI.HullShaderRHI);
		OutShaders[ShaderStage::Domain] = ResourceCast(BSI.DomainShaderRHI);
	}
	else
	{
		// Can't have Domain w/o Hull
		check(BSI.DomainShaderRHI == nullptr);
	}
#endif
}

void GetVulkanShaders(FVulkanDevice* Device, const FVulkanRHIGraphicsPipelineState& GfxPipelineState, FVulkanShader* OutShaders[ShaderStage::NumStages])
{
	FMemory::Memzero(OutShaders, ShaderStage::NumStages * sizeof(*OutShaders));
	Device->GetShaderFactory().LookupShaders(GfxPipelineState.ShaderKeys, OutShaders);
}



void FVulkanPipelineStateCacheManager::LRUDump()
{
#if !UE_BUILD_SHIPPING
	uint32 tid = FPlatformTLS::GetCurrentThreadId();
	LRUPRINT(TEXT("//***** LRU DUMP *****\\\\\n"));
	FVulkanRHIGraphicsPipelineStateLRUNode* Node= LRU.GetHead();
	uint32_t Size = 0;
	uint32_t Index = 0;
	while(Node)
	{
		FVulkanRHIGraphicsPipelineState* PSO = Node->GetValue();
		Size += PSO->PipelineCacheSize;
		LRUPRINT(TEXT("\t%08x PSO %p :: %d  :: %06d \\ %06d\n"), tid, PSO, PSO->LRUFrame, PSO->PipelineCacheSize, Size);
		Node = Node->GetNextNode();
		Index++;
	}
	LRUPRINT(TEXT("\\\\***** LRU DUMP *****//\n"));
#endif
}



bool FVulkanPipelineStateCacheManager::LRUEvictImmediately()
{
	return bEvictImmediately && CVarEnableLRU.GetValueOnAnyThread() != 0;
}


void FVulkanPipelineStateCacheManager::LRUTrim(uint32 nSpaceNeeded)
{
	if(!bUseLRU)
	{
		return;
	}
	uint32 tid = FPlatformTLS::GetCurrentThreadId();
	uint32 MaxSize = (uint32)CVarLRUMaxPipelineSize.GetValueOnAnyThread();
	while (LRUUsedPipelineSize + nSpaceNeeded > MaxSize || LRUUsedPipelineCount > LRUUsedPipelineMax)
	{
		LRUPRINT_DEBUG(TEXT("%d EVICTING %d + %d > %d || %d > %d\n"), tid, LRUUsedPipelineSize , nSpaceNeeded, MaxSize ,LRUUsedPipelineCount ,LRUUsedPipelineMax);
		LRUEvictOne();
	}
}

void FVulkanPipelineStateCacheManager::LRUDebugEvictAll()
{
	check(bUseLRU);
	FScopeLock Lock(&LRUCS);
	int Count = 0;
	while(LRUEvictOne(true))
		Count++;

	LRUPRINT_DEBUG(TEXT("Evicted %d\n"), Count);
}

void FVulkanPipelineStateCacheManager::LRUAdd(FVulkanRHIGraphicsPipelineState* PSO)
{
	if(!bUseLRU)
	{
		return;
	}

	FScopeLock Lock(&LRUCS);
	check(PSO->LRUNode == 0);
	check(PSO->GetVulkanPipeline());
	uint32 MaxSize = (uint32)CVarLRUMaxPipelineSize.GetValueOnAnyThread();
	uint32 PSOSize = PSO->PipelineCacheSize;

	LRUUsedPipelineSize += PSOSize;
	LRUUsedPipelineCount += 1;

	SET_DWORD_STAT(STAT_VulkanNumPSOLRUSize, LRUUsedPipelineSize);
	SET_DWORD_STAT(STAT_VulkanNumPSOLRU, LRUUsedPipelineCount);

	check(LRUUsedPipelineSize <= MaxSize); //should always be trimmed before.
	LRU.AddHead(PSO);
	PSO->LRUNode = LRU.GetHead();
	PSO->LRUFrame = GFrameNumberRenderThread;
	LRUPRINT_DEBUG(TEXT("LRUADD %p .. Frame %d :: %d    VKPSO %08x\n"), PSO, PSO->LRUFrame, GFrameNumberRenderThread, PSO->GetVulkanPipeline());

}

void FVulkanPipelineStateCacheManager::LRUTouch(FVulkanRHIGraphicsPipelineState* PSO)
{
	if(!bUseLRU)
	{
		return;
	}
	FScopeLock Lock(&LRUCS);
	check((PSO->GetVulkanPipeline() == 0) == (PSO->LRUNode == 0));
	
	if(PSO->LRUNode)
	{
		check(PSO->GetVulkanPipeline());
		if(PSO->LRUNode != LRU.GetHead())
		{
			LRU.RemoveNode(PSO->LRUNode, false);
			LRU.AddHead(PSO->LRUNode);
		}
		PSO->LRUFrame = GFrameNumberRenderThread;
	}
	else
	{
		PSO->LRUFrame = GFrameNumberRenderThread;
		if(!PSO->GetVulkanPipeline())
		{
			// Create the pipeline
			double BeginTime = FPlatformTime::Seconds();
			FVulkanShader* VulkanShaders[ShaderStage::NumStages];

			GetVulkanShaders(Device, *PSO, VulkanShaders);

			QUICK_SCOPE_CYCLE_COUNTER(STAT_Vulkan_RHICreateGraphicsPipelineState_CREATE_PART0);

			if (!CreateGfxPipelineFromEntry(PSO, VulkanShaders, &PSO->VulkanPipeline))
			{
				check(0);
			}
			double EndTime = FPlatformTime::Seconds();
			double Delta = EndTime - BeginTime;
			if (Delta > HitchTime)
			{
				UE_LOG(LogVulkanRHI, Verbose, TEXT("Hitchy gfx pipeline (%.3f ms)"), (float)(Delta * 1000.0));
			}

			if(bUseLRU)
			{
				LRUTrim(PSO->PipelineCacheSize);
				LRUAdd(PSO);
			}
		}
		else
		{
			check(PSO->LRUNode);
		}
	}
}

void FVulkanRHIGraphicsPipelineState::DeleteVkPipeline(bool bImmediate)
{
	if (VulkanPipeline != VK_NULL_HANDLE)
	{
		if (bImmediate)
		{
			VulkanRHI::vkDestroyPipeline(Device->GetInstanceHandle(), VulkanPipeline, VULKAN_CPU_ALLOCATOR);
		}
		else
		{
			Device->GetDeferredDeletionQueue().EnqueueResource(VulkanRHI::FDeferredDeletionQueue2::EType::Pipeline, VulkanPipeline);
		}
		VulkanPipeline = VK_NULL_HANDLE;
	}

	Device->PipelineStateCache->LRUCheckNotInside(this);
}
void FVulkanPipelineStateCacheManager::LRUCheckNotInside(FVulkanRHIGraphicsPipelineState* PSO)
{
	FScopeLock Lock(&LRUCS);


	FVulkanRHIGraphicsPipelineStateLRUNode* Node = LRU.GetHead();
	uint32_t Size = 0;
	uint32_t Index = 0;
	while (Node)
	{
		FVulkanRHIGraphicsPipelineState* foo = Node->GetValue();
		if (foo == PSO)
		{
			check(0 == foo->LRUNode);

		}
		check(foo != PSO);
		Node = Node->GetNextNode();
	}
	check(0 == PSO->LRUNode);
}

void FVulkanPipelineStateCacheManager::LRURemove(FVulkanRHIGraphicsPipelineState* PSO)
{
	check(bUseLRU);
	if (PSO->LRUNode != 0)
	{
		bool bImmediate = PSO->LRUFrame + 3 < GFrameNumberRenderThread;
		LRU.RemoveNode(PSO->LRUNode);
		PSO->LRUNode = 0;

		LRUUsedPipelineSize -= PSO->PipelineCacheSize;
		LRUUsedPipelineCount--;

		PSO->DeleteVkPipeline(bImmediate);
		SET_DWORD_STAT(STAT_VulkanNumPSOLRUSize, LRUUsedPipelineSize);
		SET_DWORD_STAT(STAT_VulkanNumPSOLRU, LRUUsedPipelineCount);
	}
	else
	{
		check(0 == PSO->GetVulkanPipeline());
	}
}

bool FVulkanPipelineStateCacheManager::LRUEvictOne(bool bOnlyOld)
{
	check(bUseLRU);
	uint32 tid = FPlatformTLS::GetCurrentThreadId();
	FVulkanRHIGraphicsPipelineStateLRUNode* Node = LRU.GetTail();
	check(Node != 0);
	TRefCountPtr<FVulkanRHIGraphicsPipelineState> PSO = Node->GetValue();

	bool bImmediate = PSO->LRUFrame + 3 < GFrameNumberRenderThread;
	if(bOnlyOld && !bImmediate)
	{
		return false;
	}
	check(PSO->LRUFrame != GFrameNumberRenderThread);

	LRURemove(PSO);
	return true;
}

void FVulkanPipelineStateCacheManager::LRURemoveAll()
{
	if (!bUseLRU)
	{
		return;
	}
	check(0);
}


