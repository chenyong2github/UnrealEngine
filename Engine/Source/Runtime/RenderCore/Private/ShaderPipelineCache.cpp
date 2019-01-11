// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/**
 * Shader Pipeline Precompilation Cache
 * Precompilation half of the shader pipeline cache, which builds on the runtime RHI pipeline cache.
 */
 
#include "ShaderPipelineCache.h"
#include "RenderingThread.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ArchiveSaveCompressedProxy.h"
#include "Serialization/ArchiveLoadCompressedProxy.h"
#include "Serialization/CustomVersion.h"
#include "Serialization/MemoryReader.h"
#include "Shader.h"
#include "Misc/EngineVersion.h"
#include "PipelineStateCache.h"
#include "PipelineFileCache.h"
#include "Misc/ScopeRWLock.h"
#include "Misc/CoreDelegates.h"
#include "ShaderCodeLibrary.h"
#include "TickableObjectRenderThread.h"
#include "Misc/ConfigCacheIni.h"
#include "Async/AsyncFileHandle.h"

DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Outstanding Tasks"), STAT_ShaderPipelineTaskCount, STATGROUP_PipelineStateCache );
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Waiting Tasks"), STAT_ShaderPipelineWaitingTaskCount, STATGROUP_PipelineStateCache );
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Active Tasks"), STAT_ShaderPipelineActiveTaskCount, STATGROUP_PipelineStateCache );
DECLARE_MEMORY_STAT(TEXT("Pre-Compile Memory"), STAT_PreCompileMemory, STATGROUP_PipelineStateCache);
DECLARE_CYCLE_STAT(TEXT("Pre-Compile Time"),STAT_PreCompileTime,STATGROUP_PipelineStateCache);
DECLARE_FLOAT_ACCUMULATOR_STAT(TEXT("Total Pre-Compile Time"),STAT_PreCompileTotalTime,STATGROUP_PipelineStateCache);
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Total Pipelines Pre-Compiled"), STAT_PreCompileShadersTotal, STATGROUP_PipelineStateCache);
DECLARE_DWORD_COUNTER_STAT(TEXT("# Pipelines Pre-Compiled"), STAT_PreCompileShadersNum, STATGROUP_PipelineStateCache);
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Total Batches Pre-Compiled"), STAT_PreCompileBatchTotal, STATGROUP_PipelineStateCache);
DECLARE_DWORD_COUNTER_STAT(TEXT("# Batches Pre-Compiled"), STAT_PreCompileBatchNum, STATGROUP_PipelineStateCache);

namespace FShaderPipelineCacheConstants
{
	static TCHAR const* SectionHeading = TEXT("ShaderPipelineCache.CacheFile");
	static TCHAR const* LastOpenedKey = TEXT("LastOpened");
	static TCHAR const* SortOrderKey = TEXT("SortOrder");
	static TCHAR const* GameVersionKey = TEXT("GameVersion");
}

static TAutoConsoleVariable<int32> CVarPSOFileCacheBackgroundBatchSize(
														  TEXT("r.ShaderPipelineCache.BackgroundBatchSize"),
														  1,
														  TEXT("Set the number of PipelineStateObjects to compile in a single batch operation when compiling in the background. Defaults to a maximum of 1 per frame, due to async. file IO it is less in practice."),
														  ECVF_Default | ECVF_RenderThreadSafe
														  );
static TAutoConsoleVariable<int32> CVarPSOFileCacheBatchSize(
														   TEXT("r.ShaderPipelineCache.BatchSize"),
#if PLATFORM_MAC
														   16, // On Mac, where we have many more PSOs to preload due to different video settings 16 works better than 50
#else
														   50,
#endif
														   TEXT("Set the number of PipelineStateObjects to compile in a single batch operation when compiling takes priority. Defaults to a maximum of 50 per frame, due to async. file IO it is less in practice."),
														   ECVF_Default | ECVF_RenderThreadSafe
														   );
static TAutoConsoleVariable<float> CVarPSOFileCacheBackgroundBatchTime(
														  TEXT("r.ShaderPipelineCache.BackgroundBatchTime"),
														  0.0f,
														  TEXT("The target time (in ms) to spend precompiling each frame when in the background or 0.0 to disable. When precompiling is faster the batch size will grow and when slower will shrink to attempt to occupy the full amount. Defaults to 0.0 (off)."),
														  ECVF_Default | ECVF_RenderThreadSafe
														  );
static TAutoConsoleVariable<float> CVarPSOFileCacheBatchTime(
														   TEXT("r.ShaderPipelineCache.BatchTime"),
														   16.0f,
														   TEXT("The target time (in ms) to spend precompiling each frame when compiling takes priority or 0.0 to disable. When precompiling is faster the batch size will grow and when slower will shrink to attempt to occupy the full amount. Defaults to 16.0 (max. ms per-frame of precompilation)."),
														   ECVF_Default | ECVF_RenderThreadSafe
														   );
static TAutoConsoleVariable<int32> CVarPSOFileCacheSaveAfterPSOsLogged(
														   TEXT("r.ShaderPipelineCache.SaveAfterPSOsLogged"),
#if !UE_BUILD_SHIPPING
														   100,
#else
														   0,
#endif
														   TEXT("Set the number of PipelineStateObjects to log before automatically saving. 0 will disable automatic saving. Shipping defaults to 0, otherwise default is 100."),
														   ECVF_Default | ECVF_RenderThreadSafe
														   );
 
static TAutoConsoleVariable<int32> CVarPSOFileCacheAutoSaveTime(
															TEXT("r.ShaderPipelineCache.AutoSaveTime"),
															30,
															TEXT("Set the time where any logged PSO's will be saved if the number is < r.ShaderPipelineCache.SaveAfterPSOsLogged. Disabled when r.ShaderPipelineCache.SaveAfterPSOsLogged is 0"),
															ECVF_Default | ECVF_RenderThreadSafe
														);

static TAutoConsoleVariable<int32> CVarPSOFileCacheAutoSaveTimeBoundPSO(
	TEXT("r.ShaderPipelineCache.AutoSaveTimeBoundPSO"),
	10,
	TEXT("Set the time where any logged PSO's will be saved when -logpso is on th ecommand line."),
	ECVF_Default | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarPSOFileCacheSaveBoundPSOLog(
																 TEXT("r.ShaderPipelineCache.SaveBoundPSOLog"),
																 (int32)0,
																 TEXT("If > 0 then a log of all bound PSOs for this run of the program will be saved to a writable user cache file. Defaults to 0 but is forced on with -logpso."),
																 ECVF_Default | ECVF_RenderThreadSafe
																 );

static TAutoConsoleVariable<int32> CVarPSOFileCacheGameFileMaskEnabled(
																TEXT("r.ShaderPipelineCache.GameFileMaskEnabled"),
																(int32)0,
																TEXT("Set non zero to use GameFileMask during PSO precompile - recording should always save out the usage masks to make that data availble when needed."),
																ECVF_ReadOnly | ECVF_RenderThreadSafe);


static TAutoConsoleVariable<int32> CVarPSOFileCacheMinBindCount(
                                                                TEXT("r.ShaderPipelineCache.MinBindCount"),
                                                                (int32)0,
                                                                TEXT("The minimum bind count to allow a PSO to be precompiled.  Changes to this value will not affect PSOs that have already been removed from consideration."),
                                                                ECVF_Default | ECVF_RenderThreadSafe
                                                                );

static bool GetShaderPipelineCacheSaveBoundPSOLog()
{
	static bool bOnce = false;
	static bool bCmdLineForce = false;
	if (!bOnce)
	{
		bOnce = true;
		bCmdLineForce = FParse::Param(FCommandLine::Get(), TEXT("logpso"));
	}
	return (bCmdLineForce || CVarPSOFileCacheSaveBoundPSOLog.GetValueOnAnyThread() == 1);
}

static bool GetPSOFileCacheSaveUserCache()
{
	static const auto CVarPSOFileCacheSaveUserCache = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ShaderPipelineCache.SaveUserCache"));
	return (CVarPSOFileCacheSaveUserCache && CVarPSOFileCacheSaveUserCache->GetInt() > 0);
}

void ConsoleCommandLoadPipelineFileCache(const TArray< FString >& Args)
{
	FShaderPipelineCache::ClosePipelineFileCache();
	FString Name = FApp::GetProjectName();
	if (Args.Num() > 0)
	{
		Name = Args[0];
	}
	FShaderPipelineCache::OpenPipelineFileCache(Name, GMaxRHIShaderPlatform);
}

void ConsoleCommandSavePipelineFileCache()
{
	if (GetShaderPipelineCacheSaveBoundPSOLog())
	{
		FShaderPipelineCache::SavePipelineFileCache(FPipelineFileCache::SaveMode::BoundPSOsOnly);
	}
	if (GetPSOFileCacheSaveUserCache())
	{
		FShaderPipelineCache::SavePipelineFileCache(FPipelineFileCache::SaveMode::SortedBoundPSOs);
	}
}

void ConsoleCommandClosePipelineFileCache()
{
	FShaderPipelineCache::ClosePipelineFileCache();
}

void ConsoleCommandSwitchModePipelineCacheCmd(const TArray< FString >& Args)
{
    if (Args.Num() > 0)
    {
        FString Mode = Args[0];
        if (Mode == TEXT("Pause"))
        {
            FShaderPipelineCache::PauseBatching();
        }
        else if (Mode == TEXT("Background"))
        {
            FShaderPipelineCache::SetBatchMode(FShaderPipelineCache::BatchMode::Background);
            FShaderPipelineCache::ResumeBatching();
        }
        else if (Mode == TEXT("Fast"))
        {
            FShaderPipelineCache::SetBatchMode(FShaderPipelineCache::BatchMode::Fast);
            FShaderPipelineCache::ResumeBatching();
        }
    }
}

static FAutoConsoleCommand LoadPipelineCacheCmd(
												TEXT("r.ShaderPipelineCache.Open"),
												TEXT("Takes the desired filename to open and then loads the pipeline file cache."),
												FConsoleCommandWithArgsDelegate::CreateStatic(ConsoleCommandLoadPipelineFileCache)
												);

static FAutoConsoleCommand SavePipelineCacheCmd(
										   		TEXT("r.ShaderPipelineCache.Save"),
										   		TEXT("Save the current pipeline file cache."),
										   		FConsoleCommandDelegate::CreateStatic(ConsoleCommandSavePipelineFileCache)
										   		);

static FAutoConsoleCommand ClosePipelineCacheCmd(
												TEXT("r.ShaderPipelineCache.Close"),
												TEXT("Close the current pipeline file cache."),
												FConsoleCommandDelegate::CreateStatic(ConsoleCommandClosePipelineFileCache)
												);

static FAutoConsoleCommand SwitchModePipelineCacheCmd(
                                                TEXT("r.ShaderPipelineCache.SetBatchMode"),
                                                TEXT("Sets the compilation batch mode, which should be one of:\n\tPause: Suspend precompilation.\n\tBackground: Low priority precompilation.\n\tFast: High priority precompilation."),
                                                FConsoleCommandWithArgsDelegate::CreateStatic(ConsoleCommandSwitchModePipelineCacheCmd)
                                                );
 
 
class FShaderPipelineCacheArchive final : public FArchive
{
public:
	FShaderPipelineCacheArchive()
	{
	}
	virtual ~FShaderPipelineCacheArchive()
	{
	}

	virtual bool AttachExternalReadDependency(FExternalReadCallback& ReadCallback) override final
	{
		ExternalReadDependencies.Add(ReadCallback);
		return true;
	}

	bool PollExternalReadDependencies()
	{
		for (uint32 i = 0; i < (uint32)ExternalReadDependencies.Num(); )
		{
			FExternalReadCallback& ReadCallback = ExternalReadDependencies[i];
			bool bFinished = ReadCallback(-1.0);
			if (bFinished)
			{
				ExternalReadDependencies.RemoveAt(i);
			}
			else
			{
				++i;
			}
		}
		return (ExternalReadDependencies.Num() == 0);
	}
	
	void BlockingWaitComplete()
	{
		for (uint32 i = 0; i < (uint32)ExternalReadDependencies.Num(); ++i)
		{
			FExternalReadCallback& ReadCallback = ExternalReadDependencies[i];
			ReadCallback(0.0);
		}
	}

private:
	/**
	*  List of external read dependencies that must be finished to load this package
	*/
	TArray<FExternalReadCallback> ExternalReadDependencies;
};

FShaderPipelineCache* FShaderPipelineCache::ShaderPipelineCache = nullptr;

FShaderPipelineCache::FShaderCacheOpenedDelegate FShaderPipelineCache::OnCachedOpened;
FShaderPipelineCache::FShaderCacheClosedDelegate FShaderPipelineCache::OnCachedClosed;
FShaderPipelineCache::FShaderPrecompilationBeginDelegate FShaderPipelineCache::OnPrecompilationBegin;
FShaderPipelineCache::FShaderPrecompilationCompleteDelegate FShaderPipelineCache::OnPrecompilationComplete;

static void PipelineStateCacheOnAppDeactivate()
{
	if (GetShaderPipelineCacheSaveBoundPSOLog())
	{
		FShaderPipelineCache::SavePipelineFileCache(FPipelineFileCache::SaveMode::BoundPSOsOnly);
	}
	if (GetPSOFileCacheSaveUserCache())
	{
		FShaderPipelineCache::SavePipelineFileCache(FPipelineFileCache::SaveMode::Incremental);
	}
}

int32 FShaderPipelineCache::GetGameVersionForPSOFileCache()
{
	int32 GameVersion = (int32)FEngineVersion::Current().GetChangelist();
	GConfig->GetInt(FShaderPipelineCacheConstants::SectionHeading, FShaderPipelineCacheConstants::GameVersionKey, GameVersion, *GGameIni);
	return GameVersion;
}

bool FShaderPipelineCache::SetGameUsageMaskWithComparison(uint64 InMask, FPSOMaskComparisonFn InComparisonFnPtr)
{
	bool bMaskChanged = false;
	
	if (ShaderPipelineCache != nullptr)
	{
		FScopeLock Lock(&ShaderPipelineCache->Mutex);
		
		uint64 OldMask = FPipelineFileCache::SetGameUsageMaskWithComparison(InMask, InComparisonFnPtr);
		bMaskChanged = OldMask != InMask;
		
		ShaderPipelineCache->bReady = true;
		
		if(bMaskChanged && ShaderPipelineCache->bOpened && CVarPSOFileCacheGameFileMaskEnabled.GetValueOnAnyThread())
		{
			// Mask has changed and we have an open file refetch PSO's for this Mask - leave the FPipelineFileCache file open - no need to close - just pull out the relevant PSOs.
			// If this PSO compile run has completed for this Mask in which case don't refetch + compile for that mask
			
			// Don't clear already compiled PSOHash list - this is not a full reset
			ShaderPipelineCache->Flush(false);
			
			if(!ShaderPipelineCache->CompletedMasks.Contains(InMask))
			{
				int32 Order = (int32)FPipelineFileCache::PSOOrder::Default;
				
				if(!GConfig->GetInt(FShaderPipelineCacheConstants::SectionHeading, FShaderPipelineCacheConstants::SortOrderKey, Order, *GGameUserSettingsIni))
				{
					GConfig->GetInt(FShaderPipelineCacheConstants::SectionHeading, FShaderPipelineCacheConstants::SortOrderKey, Order, *GGameIni);
				}
				
				TArray<FPipelineCachePSOHeader> LocalPreFetchedTasks;
				FPipelineFileCache::GetOrderedPSOHashes(LocalPreFetchedTasks, (FPipelineFileCache::PSOOrder)Order, (int64)CVarPSOFileCacheMinBindCount.GetValueOnAnyThread(), ShaderPipelineCache->CompiledHashes);
				// Iterate over all the tasks we haven't yet begun to read data for - these are the 'waiting' tasks
				int64 Count = 0;
				for (auto const& Task : LocalPreFetchedTasks)
				{
					bool bHasShaders = true;
					for (FSHAHash const& Hash : Task.Shaders)
					{
						bHasShaders &= FShaderCodeLibrary::ContainsShaderCode(Hash);
					}
					if (bHasShaders)
					{
						Count++;
					}
				}
				
				FPlatformAtomics::InterlockedAdd(&ShaderPipelineCache->TotalWaitingTasks, Count);
				
				if (OnCachedOpened.IsBound())
				{
					OnCachedOpened.Broadcast(ShaderPipelineCache->FileName, ShaderPipelineCache->CurrentPlatform, LocalPreFetchedTasks.Num(), ShaderPipelineCache->CacheFileGuid, ShaderPipelineCache->ShaderCachePrecompileContext);
				}
				
				ShaderPipelineCache->PreFetchedTasks = LocalPreFetchedTasks;
				
				UE_LOG(LogRHI, Display, TEXT("New ShaderPipelineCache GameUsageMask [%llu=>%llu], Enqueued %d of %d tasks for precompile."), OldMask, InMask, Count, ShaderPipelineCache->PreFetchedTasks.Num());
			}
			else
			{
				UE_LOG(LogRHI, Display, TEXT("New ShaderPipelineCache GameUsageMask [%llu=>%llu], Target mask already precompiled."), OldMask, InMask);
			}
		}
	}
	
	return bMaskChanged;
}

void FShaderPipelineCache::Initialize(EShaderPlatform Platform)
{
	check(ShaderPipelineCache == nullptr);
	
	if(FShaderCodeLibrary::IsEnabled())
	{
		FPipelineFileCache::Initialize(GetGameVersionForPSOFileCache());
		ShaderPipelineCache = new FShaderPipelineCache(Platform);
	}
}

void FShaderPipelineCache::Shutdown(void)
{
	if (ShaderPipelineCache)
	{
		delete ShaderPipelineCache;
		ShaderPipelineCache = nullptr;
	}
}

void FShaderPipelineCache::PauseBatching()
{
	if (ShaderPipelineCache)
    {
        ShaderPipelineCache->PausedCount++;
        UE_LOG(LogRHI, Display, TEXT("ShaderPipelineCache: Paused Batching. %d"), ShaderPipelineCache->PausedCount);
        if (ShaderPipelineCache->PausedCount > 0)
        {
            ShaderPipelineCache->bPaused = true;
        }
    }
}

bool FShaderPipelineCache::IsBatchingPaused()
{
    if (ShaderPipelineCache)
    {
        return ShaderPipelineCache->bPaused;
    }
    return true;
}

void FShaderPipelineCache::SetBatchMode(BatchMode Mode)
{
	if (ShaderPipelineCache)
	{
		switch (Mode)
		{
			case BatchMode::Fast:
			{
				ShaderPipelineCache->BatchSize = CVarPSOFileCacheBatchSize.GetValueOnAnyThread();
				ShaderPipelineCache->BatchTime = CVarPSOFileCacheBatchTime.GetValueOnAnyThread();
				break;
			}
			case BatchMode::Background:
			default:
			{
				ShaderPipelineCache->BatchSize = CVarPSOFileCacheBackgroundBatchSize.GetValueOnAnyThread();
				ShaderPipelineCache->BatchTime = CVarPSOFileCacheBackgroundBatchTime.GetValueOnAnyThread();
				break;
			}
		}
	}
}

void FShaderPipelineCache::ResumeBatching()
{
	if (ShaderPipelineCache)
    {
        ShaderPipelineCache->PausedCount--;
        UE_LOG(LogRHI, Display, TEXT("ShaderPipelineCache: Resumed Batching. %d"), ShaderPipelineCache->PausedCount);
        if (ShaderPipelineCache->PausedCount <= 0)
        {
            UE_LOG(LogRHI, Display, TEXT("ShaderPipelineCache: Batching Resumed."));
            ShaderPipelineCache->PausedCount = 0;
            ShaderPipelineCache->bPaused = false;
        }
    }
}

uint32 FShaderPipelineCache::NumPrecompilesRemaining()
{
	uint32 NumRemaining = 0;
	
	if (ShaderPipelineCache)
	{
        int64 NumActiveTasksRemaining = FMath::Max(0ll, FPlatformAtomics::AtomicRead(&ShaderPipelineCache->TotalActiveTasks));
        int64 NumWaitingTasksRemaining = FMath::Max(0ll, FPlatformAtomics::AtomicRead(&ShaderPipelineCache->TotalWaitingTasks));
		NumRemaining = (uint32)(NumActiveTasksRemaining + NumWaitingTasksRemaining);
	}

	return NumRemaining;
}

uint32 FShaderPipelineCache::NumPrecompilesActive()
{
    uint32 NumRemaining = 0;
    
    if (ShaderPipelineCache)
    {
        int64 NumTasksRemaining = FPlatformAtomics::AtomicRead(&ShaderPipelineCache->TotalActiveTasks);
        if(NumTasksRemaining > 0)
        {
            NumRemaining = (uint32) NumTasksRemaining;
        }
    }
    
    return NumRemaining;
}

bool FShaderPipelineCache::OpenPipelineFileCache(EShaderPlatform Platform)
{
	bool bFileOpen = false;
	if (GConfig)
	{
		FString LastOpenedName;
		if ((GConfig->GetString(FShaderPipelineCacheConstants::SectionHeading, FShaderPipelineCacheConstants::LastOpenedKey, LastOpenedName, *GGameUserSettingsIni) || GConfig->GetString(FShaderPipelineCacheConstants::SectionHeading, FShaderPipelineCacheConstants::LastOpenedKey, LastOpenedName, *GGameIni)) && LastOpenedName.Len())
		{
			bFileOpen = OpenPipelineFileCache(LastOpenedName, Platform);
		}
	}

	if (!bFileOpen)
	{
		bFileOpen = OpenPipelineFileCache(FApp::GetProjectName(), Platform);
	}

	return bFileOpen;
}

bool FShaderPipelineCache::OpenPipelineFileCache(FString const& Name, EShaderPlatform Platform)
{
	if (ShaderPipelineCache)
		return ShaderPipelineCache->Open(Name, Platform);
	else
		return false;
}

bool FShaderPipelineCache::SavePipelineFileCache(FPipelineFileCache::SaveMode Mode)
{
	if (ShaderPipelineCache)
		return ShaderPipelineCache->Save(Mode);
	else
		return false;
}

void FShaderPipelineCache::ClosePipelineFileCache()
{
	if (ShaderPipelineCache)
		ShaderPipelineCache->Close();
}

void FShaderPipelineCache::ShaderLibraryStateChanged(ELibraryState State, EShaderPlatform Platform, FString const& Name)
{
    if (ShaderPipelineCache)
        ShaderPipelineCache->OnShaderLibraryStateChanged(State, Platform, Name);
}

bool FShaderPipelineCache::Precompile(FRHICommandListImmediate& RHICmdList, EShaderPlatform Platform, FPipelineCacheFileFormatPSO const& PSO)
{
	INC_DWORD_STAT(STAT_PreCompileShadersTotal);
	INC_DWORD_STAT(STAT_PreCompileShadersNum);
    
    uint64 StartTime = FPlatformTime::Cycles64();
		
	bool bOk = false;
	
	TArray<uint8> DummyCode;
	
	if(FPipelineCacheFileFormatPSO::DescriptorType::Graphics == PSO.Type)
	{
		FGraphicsPipelineStateInitializer GraphicsInitializer;
		
		auto VertexDesc = RHICmdList.CreateVertexDeclaration(PSO.GraphicsDesc.VertexDescriptor);
		GraphicsInitializer.BoundShaderState.VertexDeclarationRHI = VertexDesc;
		
		FVertexShaderRHIRef VertexShader;
		if (PSO.GraphicsDesc.VertexShader != FSHAHash())
		{
			VertexShader = FShaderCodeLibrary::CreateVertexShader(Platform, PSO.GraphicsDesc.VertexShader, DummyCode);
			GraphicsInitializer.BoundShaderState.VertexShaderRHI = VertexShader;
		}

		FHullShaderRHIRef HullShader;
		if (PSO.GraphicsDesc.HullShader != FSHAHash())
		{
			HullShader = FShaderCodeLibrary::CreateHullShader(Platform, PSO.GraphicsDesc.HullShader, DummyCode);
			GraphicsInitializer.BoundShaderState.HullShaderRHI = HullShader;
		}

		FDomainShaderRHIRef DomainShader;
		if (PSO.GraphicsDesc.DomainShader != FSHAHash())
		{
			DomainShader = FShaderCodeLibrary::CreateDomainShader(Platform, PSO.GraphicsDesc.DomainShader, DummyCode);
			GraphicsInitializer.BoundShaderState.DomainShaderRHI = DomainShader;
		}

		FPixelShaderRHIRef FragmentShader;
		if (PSO.GraphicsDesc.FragmentShader != FSHAHash())
		{
			FragmentShader = FShaderCodeLibrary::CreatePixelShader(Platform, PSO.GraphicsDesc.FragmentShader, DummyCode);
			GraphicsInitializer.BoundShaderState.PixelShaderRHI = FragmentShader;
		}

		FGeometryShaderRHIRef GeometryShader;
		if (PSO.GraphicsDesc.GeometryShader != FSHAHash())
		{
			GeometryShader = FShaderCodeLibrary::CreateGeometryShader(Platform, PSO.GraphicsDesc.GeometryShader, DummyCode);
			GraphicsInitializer.BoundShaderState.GeometryShaderRHI = GeometryShader;
		}
		
		auto BlendState = RHICmdList.CreateBlendState(PSO.GraphicsDesc.BlendState);
		GraphicsInitializer.BlendState = BlendState;
		
		auto RasterState = RHICmdList.CreateRasterizerState(PSO.GraphicsDesc.RasterizerState);
		GraphicsInitializer.RasterizerState = RasterState;
		
		auto DepthState = RHICmdList.CreateDepthStencilState(PSO.GraphicsDesc.DepthStencilState);
		GraphicsInitializer.DepthStencilState = DepthState;
		
		for (uint32 i = 0; i < MaxSimultaneousRenderTargets; ++i)
		{
			GraphicsInitializer.RenderTargetFormats[i] = PSO.GraphicsDesc.RenderTargetFormats[i];
			GraphicsInitializer.RenderTargetFlags[i] = PSO.GraphicsDesc.RenderTargetFlags[i];
		}
		
		GraphicsInitializer.RenderTargetsEnabled = PSO.GraphicsDesc.RenderTargetsActive;
		GraphicsInitializer.NumSamples = PSO.GraphicsDesc.MSAASamples;
		
		GraphicsInitializer.DepthStencilTargetFormat = PSO.GraphicsDesc.DepthStencilFormat;
		GraphicsInitializer.DepthStencilTargetFlag = PSO.GraphicsDesc.DepthStencilFlags;
		GraphicsInitializer.DepthTargetLoadAction = PSO.GraphicsDesc.DepthLoad;
		GraphicsInitializer.StencilTargetLoadAction = PSO.GraphicsDesc.StencilLoad;
		GraphicsInitializer.DepthTargetStoreAction = PSO.GraphicsDesc.DepthStore;
		GraphicsInitializer.StencilTargetStoreAction = PSO.GraphicsDesc.StencilStore;
		
		GraphicsInitializer.PrimitiveType = PSO.GraphicsDesc.PrimitiveType;
		GraphicsInitializer.bFromPSOFileCache = true;
		
		// This indicates we do not want a fatal error if this compilation fails
		// (ie, if this entry in the file cache is bad)
		GraphicsInitializer.bFromPSOFileCache = 1;
		
		// Use SetGraphicsPipelineState to call down into PipelineStateCache and also handle the fallback case used by OpenGL.
		SetGraphicsPipelineState(RHICmdList, GraphicsInitializer, EApplyRendertargetOption::DoNothing);
		bOk = true;
	}
	else if(FPipelineCacheFileFormatPSO::DescriptorType::Compute == PSO.Type)
	{
		FComputeShaderRHIRef ComputeInitializer = FShaderCodeLibrary::CreateComputeShader(GMaxRHIShaderPlatform, PSO.ComputeDesc.ComputeShader, DummyCode);
		if(ComputeInitializer.IsValid())
		{
			FComputePipelineState* ComputeResult = PipelineStateCache::GetAndOrCreateComputePipelineState(RHICmdList, ComputeInitializer);
			bOk = ComputeResult != nullptr;
		}
	}
	else
	{
		check(false);
	}
    
    if (bOk)
    {
        uint64 TimeDelta = FPlatformTime::Cycles64() - StartTime;
        FPlatformAtomics::InterlockedIncrement(&TotalCompleteTasks);
        FPlatformAtomics::InterlockedAdd(&TotalPrecompileTime, TimeDelta);
    }
	
	return bOk;
}

inline bool InternalRequestShaderCode(FSHAHash Shader, FShaderPipelineCacheArchive* ReadRequest, TSet<FSHAHash>& ShaderCodeRequests)
{
	const bool bResult = FShaderCodeLibrary::RequestShaderCode(Shader, ReadRequest);
	if(bResult)
	{
		ShaderCodeRequests.Add(Shader);
	}
	return bResult;
}

void FShaderPipelineCache::PreparePipelineBatch(TDoubleLinkedList<FPipelineCacheFileFormatPSORead*>& PipelineBatch)
{
	TDoubleLinkedList<FPipelineCacheFileFormatPSORead*>::TDoubleLinkedListNode* CurrentNode = PipelineBatch.GetHead();
	while(CurrentNode)
	{
		FPipelineCacheFileFormatPSORead* PSORead = CurrentNode->GetValue();
		check(PSORead);
		FShaderPipelineCacheArchive* Archive = (FShaderPipelineCacheArchive*)(PSORead->Ar);
		check(Archive);
		
		bool bRemoveEntry = false;
		
		if (PSORead->bValid &&
			(PSORead->bReadCompleted || Archive->PollExternalReadDependencies()))
		{
			check(PSORead->bReadCompleted);
		
			FPipelineCacheFileFormatPSO PSO;
			
			FMemoryReader Ar(PSORead->Data);
			Ar.SetGameNetVer(FPipelineCacheFileFormatCurrentVersion);
			Ar << PSO;
			
			// Assume that the shader is present and the PSO can be compiled by default,
			bool bOK = true;
	
            // Shaders required.
            TSet<FSHAHash> RequiredShaders;
			
			CompileJob AsyncJob;
			AsyncJob.PSO = PSO;
			AsyncJob.ReadRequests = new FShaderPipelineCacheArchive;
			
            static FSHAHash EmptySHA;
            
            if (PSO.Type == FPipelineCacheFileFormatPSO::DescriptorType::Graphics)
			{
                // See if the shaders exist in the current code libraries, before trying to load the shader data
                if (PSO.GraphicsDesc.VertexShader != EmptySHA)
                {
                    RequiredShaders.Add(PSO.GraphicsDesc.VertexShader);
                    bOK &= FShaderCodeLibrary::ContainsShaderCode(PSO.GraphicsDesc.VertexShader);
                    UE_CLOG(!bOK, LogRHI, Verbose, TEXT("Failed to find VertexShader shader: %s"), *(PSO.GraphicsDesc.VertexShader.ToString()));
					
					if (PSO.GraphicsDesc.HullShader != EmptySHA)
					{
						RequiredShaders.Add(PSO.GraphicsDesc.HullShader);
						bOK &= FShaderCodeLibrary::ContainsShaderCode(PSO.GraphicsDesc.HullShader);
						UE_CLOG(!bOK, LogRHI, Verbose, TEXT("Failed to find HullShader shader: %s"), *(PSO.GraphicsDesc.HullShader.ToString()));
					}
					if (PSO.GraphicsDesc.DomainShader != EmptySHA)
					{
						RequiredShaders.Add(PSO.GraphicsDesc.DomainShader);
						bOK &= FShaderCodeLibrary::ContainsShaderCode(PSO.GraphicsDesc.DomainShader);
						UE_CLOG(!bOK, LogRHI, Verbose, TEXT("Failed to find DomainShader shader: %s"), *(PSO.GraphicsDesc.DomainShader.ToString()));
					}
					if (PSO.GraphicsDesc.FragmentShader != EmptySHA)
					{
						RequiredShaders.Add(PSO.GraphicsDesc.FragmentShader);
						bOK &= FShaderCodeLibrary::ContainsShaderCode(PSO.GraphicsDesc.FragmentShader);
						UE_CLOG(!bOK, LogRHI, Verbose, TEXT("Failed to find FragmentShader shader: %s"), *(PSO.GraphicsDesc.FragmentShader.ToString()));
					}
					if (PSO.GraphicsDesc.GeometryShader != EmptySHA)
					{
						RequiredShaders.Add(PSO.GraphicsDesc.GeometryShader);
						bOK &= FShaderCodeLibrary::ContainsShaderCode(PSO.GraphicsDesc.GeometryShader);
						UE_CLOG(!bOK, LogRHI, Verbose, TEXT("Failed to find GeometryShader shader: %s"), *(PSO.GraphicsDesc.GeometryShader.ToString()));
					}
				}
				else
				{
					// if we don't have a vertex shader then we won't bother to add any shaders to the list of outstanding shaders to load
					// Later on this PSO will be killed forever because it is truly bogus.
					UE_LOG(LogRHI, Error, TEXT("PSO Entry has no vertex shader: %u this is an invalid entry!"), PSORead->Hash);
					bOK = false;
				}
                
                // If everything is OK then we can issue reads of the actual shader code
				if (bOK && PSO.GraphicsDesc.VertexShader != FSHAHash())
				{
					bOK &= InternalRequestShaderCode(PSO.GraphicsDesc.VertexShader, AsyncJob.ReadRequests, AsyncJob.ShaderCodeReads);
                    UE_CLOG(!bOK, LogRHI, Verbose, TEXT("Failed to read VertexShader shader: %s"), *(PSO.GraphicsDesc.VertexShader.ToString()));
				}
		
				if (bOK && PSO.GraphicsDesc.HullShader != EmptySHA)
				{
					bOK &= InternalRequestShaderCode(PSO.GraphicsDesc.HullShader, AsyncJob.ReadRequests, AsyncJob.ShaderCodeReads);
                    UE_CLOG(!bOK, LogRHI, Verbose, TEXT("Failed to read HullShader shader: %s"), *(PSO.GraphicsDesc.HullShader.ToString()));
				}
		
				if (bOK && PSO.GraphicsDesc.DomainShader != EmptySHA)
				{
					bOK &= InternalRequestShaderCode(PSO.GraphicsDesc.DomainShader, AsyncJob.ReadRequests, AsyncJob.ShaderCodeReads);
                    UE_CLOG(!bOK, LogRHI, Verbose, TEXT("Failed to read DomainShader shader: %s"), *(PSO.GraphicsDesc.DomainShader.ToString()));
				}
		
				if (bOK && PSO.GraphicsDesc.FragmentShader != EmptySHA)
				{
					bOK &= InternalRequestShaderCode(PSO.GraphicsDesc.FragmentShader, AsyncJob.ReadRequests, AsyncJob.ShaderCodeReads);
                    UE_CLOG(!bOK, LogRHI, Verbose, TEXT("Failed to read FragmentShader shader: %s"), *(PSO.GraphicsDesc.FragmentShader.ToString()));
				}
		
				if (bOK && PSO.GraphicsDesc.GeometryShader != EmptySHA)
				{
					bOK &= InternalRequestShaderCode(PSO.GraphicsDesc.GeometryShader, AsyncJob.ReadRequests, AsyncJob.ShaderCodeReads);
                    UE_CLOG(!bOK, LogRHI, Verbose, TEXT("Failed to read GeometryShader shader: %s"), *(PSO.GraphicsDesc.GeometryShader.ToString()));
				}
			}
			else if (PSO.Type == FPipelineCacheFileFormatPSO::DescriptorType::Compute)
			{
				if (PSO.ComputeDesc.ComputeShader != EmptySHA)
				{
                    RequiredShaders.Add(PSO.ComputeDesc.ComputeShader);
					bOK &= InternalRequestShaderCode(PSO.ComputeDesc.ComputeShader, AsyncJob.ReadRequests, AsyncJob.ShaderCodeReads);
                    UE_CLOG(!bOK, LogRHI, Verbose, TEXT("Failed to find ComputeShader shader: %s"), *(PSO.ComputeDesc.ComputeShader.ToString()));
				}
				else
				{
					bOK = false;
					UE_LOG(LogRHI, Error, TEXT("Invalid PSO entry in pipeline cache!"));
				}
			}
			else
			{
				bOK = false;
				UE_LOG(LogRHI, Error, TEXT("Invalid PSO entry in pipeline cache!"));
			}
			
			// Then if and only if all shaders can be found do we schedule a compile job
			// Otherwise this job needs to be put in the shutdown list to correctly release shader code
			if (bOK)
			{
				ReadTasks.Add(AsyncJob);
			}
			else
			{
				if (RequiredShaders.Num())
				{
					// Re-add to the OrderedCompile tasks and process later
					// We can never know when this PSO might become valid so we can't ever drop it.
					FPipelineCachePSOHeader Hdr;
					Hdr.Hash = PSORead->Hash;
					Hdr.Shaders = RequiredShaders;
					OrderedCompileTasks.Insert(Hdr, 0);
				}
				else
				{
					UE_LOG(LogRHI, Error, TEXT("Invalid PSO entry in pipeline cache: %u!"), PSORead->Hash);
				}

				// Go to async shutdown instead - some shader code reads may have been requested - let it handle this
				ShutdownReadCompileTasks.Add(AsyncJob);
			}
			
			bRemoveEntry = true;
		}
        else if (!PSORead->bValid)
        {
            UE_LOG(LogRHI, Error, TEXT("Invalid PSO entry in pipeline cache: %u!"), PSORead->Hash);
            
            // Invalid PSOs can be deleted
            FPlatformAtomics::InterlockedDecrement(&TotalActiveTasks);
            bRemoveEntry = true;
        }
		
		TDoubleLinkedList<FPipelineCacheFileFormatPSORead*>::TDoubleLinkedListNode* PrevNode = CurrentNode;
		CurrentNode = CurrentNode->GetNextNode();
		if(bRemoveEntry)
		{
			delete PSORead;
			PipelineBatch.RemoveNode(PrevNode);
		}
	}
}

bool FShaderPipelineCache::ReadyForPrecompile()
{
	for(uint32 i = 0; i < (uint32)ReadTasks.Num();/*NOP*/)
	{
		check(ReadTasks[i].ReadRequests);
		if (ReadTasks[i].ReadRequests->PollExternalReadDependencies())
		{
			CompileTasks.Add(ReadTasks[i]);
			ReadTasks.RemoveAt(i);
		}
		else
		{
			++i;
		}
	}
	if (LastPrecompileRHIFence.GetReference() && LastPrecompileRHIFence->IsComplete())
	{
		LastPrecompileRHIFence = nullptr;
	}

	return (CompileTasks.Num() != 0) && !LastPrecompileRHIFence.GetReference();
}

void FShaderPipelineCache::PrecompilePipelineBatch()
{
	INC_DWORD_STAT(STAT_PreCompileBatchTotal);
	INC_DWORD_STAT(STAT_PreCompileBatchNum);
	
    int32 NumToPrecompile = FMath::Min<int32>(CompileTasks.Num(), BatchSize);
    
	for(uint32 i = 0; i < (uint32)NumToPrecompile; i++)
	{
		CompileJob& CompileTask = CompileTasks[i];
		
		check(CompileTask.ReadRequests && CompileTask.ReadRequests->PollExternalReadDependencies());
		
		FRHICommandListImmediate& RHICmdList = GRHICommandList.GetImmediateCommandList();
		
		Precompile(RHICmdList, GMaxRHIShaderPlatform, CompileTask.PSO);
		CompiledHashes.Add(GetTypeHash(CompileTask.PSO));
		
		// Free our code references that were actually made
		if(CompileTask.ShaderCodeReads.Num())
		{
			if(GRHILazyShaderCodeLoading && IsRunningRHIInSeparateThread())
			{
				TSet<FSHAHash> ShaderCodeReads = CompileTask.ShaderCodeReads;
				RHICmdList.EnqueueLambda([ShaderCodeReads](FRHICommandListImmediate&)
				{
					for (const FSHAHash& DelHash : ShaderCodeReads)
					{
						FShaderCodeLibrary::ReleaseShaderCode(DelHash);
					}
				});
			}
			else
			{
				for (const FSHAHash& DelHash : CompileTask.ShaderCodeReads)
				{
					FShaderCodeLibrary::ReleaseShaderCode(DelHash);
				}
			}
		}
		
		delete CompileTask.ReadRequests;
		CompileTask.ReadRequests = nullptr;
		
#if STATS
		switch(CompileTask.PSO.Type)
		{
			case FPipelineCacheFileFormatPSO::DescriptorType::Compute:
			{
				INC_DWORD_STAT(STAT_TotalComputePipelineStateCount);
				break;
			}
			case FPipelineCacheFileFormatPSO::DescriptorType::Graphics:
			{
				INC_DWORD_STAT(STAT_TotalGraphicsPipelineStateCount);
				break;
			}
			default:
			{
				check(false);
				break;
			}
		}
#endif
	}
	
    FPlatformAtomics::InterlockedAdd(&TotalActiveTasks, -NumToPrecompile);

#if PLATFORM_ANDROID
	if (NumToPrecompile > 0 && IsRunningRHIInSeparateThread())
	{
		LastPrecompileRHIFence = FRHICommandListExecutor::GetImmediateCommandList().RHIThreadFence(false);
	}
#endif

	CompileTasks.RemoveAt(0, NumToPrecompile);
}

bool FShaderPipelineCache::ReadyForNextBatch() const
{
	return ReadTasks.Num() == 0;
}

bool FShaderPipelineCache::ReadyForAutoSave() const
{
	bool bAutoSave = false;
	uint32 SaveAfterNum = CVarPSOFileCacheSaveAfterPSOsLogged.GetValueOnAnyThread();
	uint32 NumLogged = FPipelineFileCache::NumPSOsLogged();

	const float TimeSinceSave = FPlatformTime::Seconds() - LastAutoSaveTime;

	// autosave if its enabled, and we have more than the desired number, or it's been a while since our last save
	if (SaveAfterNum > 0 && 
			(NumLogged >= SaveAfterNum || (NumLogged > 0 && TimeSinceSave >= CVarPSOFileCacheAutoSaveTime.GetValueOnAnyThread()))
		)
	{
		bAutoSave = true;
	}
	return bAutoSave;
}

void FShaderPipelineCache::PollShutdownItems()
{
	int64 RemovedTaskCount = 0;
	
	const int64 InitialCompileTaskCount = ShutdownReadCompileTasks.Num();
	if(ShutdownReadCompileTasks.Num() > 0)
	{
		for(uint32 i = 0; i < (uint32)ShutdownReadCompileTasks.Num(); )
		{
			check(ShutdownReadCompileTasks[i].ReadRequests);
			if (ShutdownReadCompileTasks[i].ReadRequests->PollExternalReadDependencies())
			{
				// Free our code references that were actually made - maybe not all were
				for (const FSHAHash& DelHash : ShutdownReadCompileTasks[i].ShaderCodeReads)
				{
					FShaderCodeLibrary::ReleaseShaderCode(DelHash);
				}
				
                delete ShutdownReadCompileTasks[i].ReadRequests;
				ShutdownReadCompileTasks[i].ReadRequests = nullptr;
				
				ShutdownReadCompileTasks.RemoveAt(i, 1, false);
				++RemovedTaskCount;
			}
			else
			{
				++i;
			}
		}
		
		if(ShutdownReadCompileTasks.Num() == 0)
		{
			ShutdownReadCompileTasks.Shrink();
		}
	}
	
	if(ShutdownFetchTasks.Num() > 0)
	{
		TDoubleLinkedList<FPipelineCacheFileFormatPSORead*>::TDoubleLinkedListNode* CurrentNode = ShutdownFetchTasks.GetHead();
		while(CurrentNode)
		{
			FPipelineCacheFileFormatPSORead* PSORead = CurrentNode->GetValue();
			check(PSORead);
			FShaderPipelineCacheArchive* Archive = (FShaderPipelineCacheArchive*)(PSORead->Ar);
			check(Archive);
			
			TDoubleLinkedList<FPipelineCacheFileFormatPSORead*>::TDoubleLinkedListNode* PrevNode = CurrentNode;
			CurrentNode = CurrentNode->GetNextNode();
			
			if (PSORead->bReadCompleted || Archive->PollExternalReadDependencies())
			{
				delete PSORead;
				ShutdownFetchTasks.RemoveNode(PrevNode);
				++RemovedTaskCount;
			}
		}
	}
	
	if(RemovedTaskCount > 0)
    {
        FPlatformAtomics::InterlockedAdd(&TotalActiveTasks, - RemovedTaskCount);
	}
}

void FShaderPipelineCache::Flush(bool bClearCompiled /*= true*/)
{
	FScopeLock Lock(&Mutex);
	
	if(bClearCompiled)
	{
		CompiledHashes.Empty();
	}
	
	// reset everything
	// Abandon all the existing work.
	// This must be done on the render-thread to avoid locks.
	OrderedCompileTasks.Empty();
	
	// Marshall the current Compile Jobs into shutdown
	ShutdownReadCompileTasks.Append(ReadTasks);
	ReadTasks.Empty();
	
	ShutdownReadCompileTasks.Append(CompileTasks);
	CompileTasks.Empty();
	
	// Marshall the current fetch tasks into shutdown
	for (TDoubleLinkedList<FPipelineCacheFileFormatPSORead*>::TIterator It(FetchTasks.GetHead()); It; ++It)
	{
		FPipelineCacheFileFormatPSORead* Entry = *It;
		check(Entry->ReadRequest.IsValid());
		Entry->ReadRequest->Cancel();
		ShutdownFetchTasks.AddTail(Entry);
	}
	FetchTasks.Empty();
	
	int64 StartTaskCount = OrderedCompileTasks.Num() + ShutdownReadCompileTasks.Num() + ShutdownFetchTasks.Num();;
    FPlatformAtomics::InterlockedExchange(&TotalWaitingTasks, 0);
}

FShaderPipelineCache::FShaderPipelineCache(EShaderPlatform Platform)
: FTickableObjectRenderThread(true, false) // (RegisterNow, HighFrequency)
, BatchSize(0)
, BatchTime(0.0f)
, bPaused(false)
, bOpened(false)
, bReady(false)
, PausedCount(0)
, TotalActiveTasks(0)
, TotalWaitingTasks(0)
, TotalCompleteTasks(0)
, TotalPrecompileTime(0)
, PrecompileStartTime(0.0)
, LastAutoSaveTime(0)
, LastAutoSaveTimeLogBoundPSO(0)
, LastAutoSaveNum(-1)
{
	SET_DWORD_STAT(STAT_ShaderPipelineTaskCount, 0);
    SET_DWORD_STAT(STAT_ShaderPipelineWaitingTaskCount, 0);
    SET_DWORD_STAT(STAT_ShaderPipelineActiveTaskCount, 0);
	
	BatchSize = CVarPSOFileCacheBatchSize.GetValueOnAnyThread();
	BatchTime = CVarPSOFileCacheBatchTime.GetValueOnAnyThread();
	
	FCoreDelegates::ApplicationWillDeactivateDelegate.AddStatic(&PipelineStateCacheOnAppDeactivate);
	
	bReady = !CVarPSOFileCacheGameFileMaskEnabled.GetValueOnAnyThread();
}

FShaderPipelineCache::~FShaderPipelineCache()
{
	if (GetShaderPipelineCacheSaveBoundPSOLog())
	{
		FShaderPipelineCache::SavePipelineFileCache(FPipelineFileCache::SaveMode::BoundPSOsOnly);
	}
	if (GetPSOFileCacheSaveUserCache())
	{
		FShaderPipelineCache::SavePipelineFileCache(FPipelineFileCache::SaveMode::Incremental);
	}
	
	Close();
	
	// The render thread tick should be dead now and we are safe to destroy everything that needs to wait or manual destruction

	for(CompileJob& Entry: ReadTasks )
	{
		check(Entry.ReadRequests);
		Entry.ReadRequests->BlockingWaitComplete();
	}
	
	for(CompileJob& Entry: ShutdownReadCompileTasks )
	{
		check(Entry.ReadRequests);
		Entry.ReadRequests->BlockingWaitComplete();
	}
	
	for (FPipelineCacheFileFormatPSORead* Entry : FetchTasks)
	{
		if(Entry->ReadRequest.IsValid())
		{
			Entry->ReadRequest->WaitCompletion(0.0);
		}
		delete Entry;
	}
	
	for (FPipelineCacheFileFormatPSORead* Entry : ShutdownFetchTasks)
	{
		if(Entry->ReadRequest.IsValid())
		{
			Entry->ReadRequest->WaitCompletion(0.0);
		}
		delete Entry;
	}	
}

bool FShaderPipelineCache::IsTickable() const
{
	return FPlatformProperties::RequiresCookedData() && !bPaused &&
		(FPlatformAtomics::AtomicRead(&TotalActiveTasks) != 0 ||
			FPlatformAtomics::AtomicRead(&TotalWaitingTasks) != 0 ||
			FPlatformAtomics::AtomicRead(&TotalCompleteTasks) != 0 ||
			ReadyForAutoSave()||
			GetShaderPipelineCacheSaveBoundPSOLog());
}

void FShaderPipelineCache::Tick( float DeltaTime )
{
	FScopeLock Lock(&Mutex);
    
	if (LastPrecompileRHIFence.GetReference() && LastPrecompileRHIFence->IsComplete())
	{
		LastPrecompileRHIFence = nullptr;
	}

    if (FPlatformAtomics::AtomicRead(&TotalWaitingTasks) == 0 && FPlatformAtomics::AtomicRead(&TotalActiveTasks) == 0 && FPlatformAtomics::AtomicRead(&TotalCompleteTasks) != 0 && !LastPrecompileRHIFence.GetReference())
    {
		float WallTime = 0.0f;
		if (PrecompileStartTime > 0.0)
		{
			WallTime = float(FPlatformTime::Seconds() - PrecompileStartTime);
		}
		UE_LOG(LogRHI, Warning, TEXT("FShaderPipelineCache completed %u tasks in %.2fs (%.2fs wall time since intial open)."), (uint32)TotalCompleteTasks, FPlatformTime::ToSeconds64(TotalPrecompileTime), WallTime);
        if (OnPrecompilationComplete.IsBound())
        {
            OnPrecompilationComplete.Broadcast((uint32)TotalCompleteTasks, FPlatformTime::ToSeconds64(TotalPrecompileTime), ShaderCachePrecompileContext);
        }
        FPlatformAtomics::InterlockedExchange(&TotalCompleteTasks, 0);
        FPlatformAtomics::InterlockedExchange(&TotalPrecompileTime, 0);
    }
	
	if (ReadyForAutoSave())
	{
		if (GetPSOFileCacheSaveUserCache())
		{
			Save(FPipelineFileCache::SaveMode::Incremental);
		}
	}
	if (GetShaderPipelineCacheSaveBoundPSOLog())
	{
		if (LastAutoSaveNum < int32(FPipelineFileCache::NumPSOsLogged()))
		{
			const float TimeSinceSave = FPlatformTime::Seconds() - LastAutoSaveTimeLogBoundPSO;

			if (TimeSinceSave >= CVarPSOFileCacheAutoSaveTimeBoundPSO.GetValueOnAnyThread())
			{
				Save(FPipelineFileCache::SaveMode::BoundPSOsOnly);
				LastAutoSaveTimeLogBoundPSO = FPlatformTime::Seconds();
				LastAutoSaveNum = FPipelineFileCache::NumPSOsLogged();
			}
		}
	}

	PollShutdownItems();

	if (PrecompileStartTime == 0.0 && (PreFetchedTasks.Num() || FetchTasks.Num() || OrderedCompileTasks.Num()))
	{
		PrecompileStartTime = FPlatformTime::Seconds();
	}
	
	// Copy any new items over to our 'internal' safe array
	if (PreFetchedTasks.Num())
	{
		OrderedCompileTasks.Append(PreFetchedTasks);
		PreFetchedTasks.Empty();
	}
	
	if (ReadyForPrecompile())
	{
		SCOPE_SECONDS_ACCUMULATOR(STAT_PreCompileTotalTime);
		SCOPE_CYCLE_COUNTER(STAT_PreCompileTime);
		
		uint32 Start = FPlatformTime::Cycles();

		PrecompilePipelineBatch();

		uint32 End = FPlatformTime::Cycles();

		if (BatchTime > 0.0f)
		{
			float ElapsedMs = FPlatformTime::ToMilliseconds(End - Start);
			if (ElapsedMs < BatchTime)
			{
				BatchSize++;
			}
			else if (ElapsedMs > BatchTime)
			{
				if (BatchSize > 1)
				{
					BatchSize--;
				}
				else
				{
					UE_LOG(LogRHI, Warning, TEXT("FShaderPipelineCache: Cannot reduce BatchSize below 1 to meet target of %f ms, elapsed time was %f ms)"), BatchTime, ElapsedMs);
				}
			}
		}
	}
	
	if (ReadyForNextBatch() && (OrderedCompileTasks.Num() || FetchTasks.Num()))
	{
        uint32 Num = 0;
        if (BatchSize > static_cast<uint32>(FetchTasks.Num()))
        {
            Num = BatchSize - FetchTasks.Num();
        }
        Num = FMath::Min(Num, (uint32)OrderedCompileTasks.Num());
            
		if (FetchTasks.Num() < (int32)Num)
		{
			TDoubleLinkedList<FPipelineCacheFileFormatPSORead*> NewBatch;
		
			Num -= FetchTasks.Num();
            for (auto Iterator = OrderedCompileTasks.CreateIterator(); Iterator && Num > 0; ++Iterator)
            {
                bool bHasShaders = true;
                for (FSHAHash const& Hash : Iterator->Shaders)
                {
                    bHasShaders &= FShaderCodeLibrary::ContainsShaderCode(Hash);
                }
            	if (bHasShaders)
            	{
            		FPipelineCacheFileFormatPSORead* Entry = new FPipelineCacheFileFormatPSORead;
					Entry->Hash = Iterator->Hash;
					Entry->Ar = new FShaderPipelineCacheArchive;
					
					// Add to both new batch and fetch lists
					NewBatch.AddTail(Entry);
					FetchTasks.AddTail(Entry);
					
	            	Iterator.RemoveCurrent();
                    FPlatformAtomics::InterlockedIncrement(&TotalActiveTasks);
                    FPlatformAtomics::InterlockedDecrement(&TotalWaitingTasks);
                    --Num;
            	}
            }
			
			FPipelineFileCache::FetchPSODescriptors(NewBatch);
		}

        if (static_cast<uint32>(FetchTasks.Num()) > BatchSize)
        {
            UE_LOG(LogRHI, Warning, TEXT("FShaderPipelineCache: Attempting to pre-compile more jobs (%d) than the batch size (%d)"), FetchTasks.Num(), BatchSize);
        }
        
		PreparePipelineBatch(FetchTasks);
	}
	
	
	if(CVarPSOFileCacheGameFileMaskEnabled.GetValueOnAnyThread())
	{
		if(FPlatformAtomics::AtomicRead(&TotalActiveTasks) + FPlatformAtomics::AtomicRead(&TotalWaitingTasks) == 0)
		{
			const uint64 Mask = FPipelineFileCache::GetGameUsageMask();
			bool bAlreadyInSet = false;
			CompletedMasks.Add(Mask, &bAlreadyInSet);
			if(!bAlreadyInSet)
			{
				UE_LOG(LogRHI, Display, TEXT("ShaderPipelineCache: GameUsageMask [%llu] precompile complete."), Mask);
			}
		}
	}
	
#if STATS
	int64 ActiveTaskCount = FMath::Max((int64)0, FPlatformAtomics::AtomicRead(&TotalActiveTasks));
    int64 WaitingTaskCount = FMath::Max((int64)0, FPlatformAtomics::AtomicRead(&TotalWaitingTasks));
	SET_DWORD_STAT(STAT_ShaderPipelineTaskCount, ActiveTaskCount+WaitingTaskCount);
    SET_DWORD_STAT(STAT_ShaderPipelineWaitingTaskCount, WaitingTaskCount);
    SET_DWORD_STAT(STAT_ShaderPipelineActiveTaskCount, ActiveTaskCount);
	
	// Calc in one place otherwise this computation will be splattered all over the place - this will not be exact but counts the expensive bits
	int64 InUseMemory = OrderedCompileTasks.GetAllocatedSize() + 
						CompiledHashes.GetAllocatedSize() + 
						ReadTasks.GetAllocatedSize() + 
						CompileTasks.GetAllocatedSize() + 
						ShutdownReadCompileTasks.GetAllocatedSize();
	if(ActiveTaskCount+WaitingTaskCount > 0)
	{
		InUseMemory += (ReadTasks.Num() + CompileTasks.Num() + ShutdownReadCompileTasks.Num()) * (sizeof(FShaderPipelineCacheArchive));
		InUseMemory += (FetchTasks.Num() + ShutdownFetchTasks.Num()) * (sizeof(FPipelineCacheFileFormatPSORead));
		for (TDoubleLinkedList<FPipelineCacheFileFormatPSORead*>::TIterator It(FetchTasks.GetHead()); It; ++It)
		{
			FPipelineCacheFileFormatPSORead* Entry = *It;
			InUseMemory += Entry->Data.Num();
		}
		for (TDoubleLinkedList<FPipelineCacheFileFormatPSORead*>::TIterator It(ShutdownFetchTasks.GetHead()); It; ++It)
		{
			FPipelineCacheFileFormatPSORead* Entry = *It;
			InUseMemory += Entry->Data.Num();
		}
	}
	SET_MEMORY_STAT(STAT_PreCompileMemory, InUseMemory);
#endif
}

bool FShaderPipelineCache::NeedsRenderingResumedForRenderingThreadTick() const
{
	return true;
}

TStatId FShaderPipelineCache::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FShaderPipelineBatchCompiler, STATGROUP_Tickables);
}

bool FShaderPipelineCache::Open(FString const& Name, EShaderPlatform Platform)
{
	FileName = Name;
    CurrentPlatform = Platform;
	
	bool bOK = FPipelineFileCache::OpenPipelineFileCache(Name, Platform, CacheFileGuid);
	if (bOK)
	{
		FScopeLock Lock(&Mutex);
		
		Flush();
		
		if(bReady)
		{
			int32 Order = (int32)FPipelineFileCache::PSOOrder::Default;
			
			if(!GConfig->GetInt(FShaderPipelineCacheConstants::SectionHeading, FShaderPipelineCacheConstants::SortOrderKey, Order, *GGameUserSettingsIni))
			{
				GConfig->GetInt(FShaderPipelineCacheConstants::SectionHeading, FShaderPipelineCacheConstants::SortOrderKey, Order, *GGameIni);
			}

			TArray<FPipelineCachePSOHeader> LocalPreFetchedTasks;
			FPipelineFileCache::GetOrderedPSOHashes(LocalPreFetchedTasks, (FPipelineFileCache::PSOOrder)Order, (int64)CVarPSOFileCacheMinBindCount.GetValueOnAnyThread(), CompiledHashes);
			// Iterate over all the tasks we haven't yet begun to read data for - these are the 'waiting' tasks
			int64 Count = 0;
			for (auto const& Task : LocalPreFetchedTasks)
			{
				bool bHasShaders = true;
				for (FSHAHash const& Hash : Task.Shaders)
				{
					bHasShaders &= FShaderCodeLibrary::ContainsShaderCode(Hash);
				}
				if (bHasShaders)
				{
					Count++;
				}
			}

			FPlatformAtomics::InterlockedAdd(&TotalWaitingTasks, Count);

			if (OnCachedOpened.IsBound())
			{
				OnCachedOpened.Broadcast(Name, Platform, LocalPreFetchedTasks.Num(), CacheFileGuid, ShaderCachePrecompileContext);
			}

			PreFetchedTasks = LocalPreFetchedTasks;

			UE_LOG(LogRHI, Display, TEXT("Opened pipeline cache and enqueued %d of %d tasks for precompile."), Count, PreFetchedTasks.Num());
		}
		else
		{
			UE_LOG(LogRHI, Display, TEXT("Opened pipeline cache - precompile deferred on UsageMask."));
		}
	}

	UE_CLOG(!bOK, LogRHI, Display, TEXT("Failed to open default shader pipeline cache for %s using shader platform %d."), *Name, (uint32)Platform);
	
	bOpened = bOK;

	// OnPrecompilationBegin can bring up a modal loading screen, so we call it outside the scope lock to prevent deadlocks
	if (bOK && OnPrecompilationBegin.IsBound())
	{
		OnPrecompilationBegin.Broadcast(PreFetchedTasks.Num(), ShaderCachePrecompileContext);
	}

	return bOK;
}

bool FShaderPipelineCache::Save(FPipelineFileCache::SaveMode Mode)
{
	FScopeLock Lock(&Mutex);
		
	bool bOK = FPipelineFileCache::SavePipelineFileCache(FileName, Mode);
	UE_CLOG(!bOK, LogRHI, Warning, TEXT("Failed to save shader pipeline cache for %s using save mode %d."), *FileName, (uint32)Mode);

	LastAutoSaveTime = FPlatformTime::Seconds();

	return bOK;
}

void FShaderPipelineCache::Close()
{
	FScopeLock Lock(&Mutex);
		
	if(GConfig)
	{
		GConfig->SetString(FShaderPipelineCacheConstants::SectionHeading, FShaderPipelineCacheConstants::LastOpenedKey, *FileName, *GGameUserSettingsIni);
		GConfig->Flush(false, *GGameUserSettingsIni);
	}
	
	// Log all bound PSOs
	if (GetShaderPipelineCacheSaveBoundPSOLog())
	{
		Save(FPipelineFileCache::SaveMode::BoundPSOsOnly);
	}
	
	// Force a fast save, just in case
	if (GetPSOFileCacheSaveUserCache())
	{
		Save(FPipelineFileCache::SaveMode::Incremental);
	}
	
	// Signal flush of outstanding work to allow restarting for a new cache file
	Flush();
    
    if (OnCachedClosed.IsBound())
    {
        OnCachedClosed.Broadcast(FileName, CurrentPlatform);
    }
	
	bOpened = false;

	FPipelineFileCache::ClosePipelineFileCache();
}

void FShaderPipelineCache::OnShaderLibraryStateChanged(ELibraryState State, EShaderPlatform Platform, FString const& Name)
{
    FScopeLock Lock(&Mutex);
	
	if (State == FShaderPipelineCache::Opened && Name == FApp::GetProjectName() && Platform == CurrentPlatform && !bOpened)
	{
		Close();
		FString LastOpenedName;
		if ((!GConfig->GetString(FShaderPipelineCacheConstants::SectionHeading, FShaderPipelineCacheConstants::LastOpenedKey, LastOpenedName, *GGameUserSettingsIni) && !GConfig->GetString(FShaderPipelineCacheConstants::SectionHeading, FShaderPipelineCacheConstants::LastOpenedKey, LastOpenedName, *GGameIni)) && !LastOpenedName.Len())
		{
			LastOpenedName = FApp::GetProjectName();
		}
		Open(LastOpenedName, Platform);
	}
	
    // Copy any new items over to our 'internal' safe array
    if (PreFetchedTasks.Num())
    {
        OrderedCompileTasks = PreFetchedTasks;
        PreFetchedTasks.Empty();
    }
    
    // Iterate over all the tasks we haven't yet begun to read data for - these are the 'waiting' tasks
    int64 Count = 0;
    for (auto const& Task : OrderedCompileTasks)
    {
        bool bHasShaders = true;
        for (FSHAHash const& Hash : Task.Shaders)
        {
            bHasShaders &= FShaderCodeLibrary::ContainsShaderCode(Hash);
        }
        if (bHasShaders)
        {
            Count++;
        }
    }
    
    // Set the new waiting count that we can actually process.
    FPlatformAtomics::InterlockedExchange(&TotalWaitingTasks, Count);
	UE_LOG(LogRHI, Display, TEXT("Opened pipeline cache after state change and enqueued %d of %d tasks for precompile."), Count, OrderedCompileTasks.Num());
}
