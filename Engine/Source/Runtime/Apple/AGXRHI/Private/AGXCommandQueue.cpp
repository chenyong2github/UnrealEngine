// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXCommandQueue.cpp: AGX RHI command queue wrapper.
=============================================================================*/

#include "AGXRHIPrivate.h"

#include "AGXCommandQueue.h"
#include "AGXCommandBuffer.h"
#include "AGXCommandList.h"
#include "AGXProfiler.h"
#include "Misc/ConfigCacheIni.h"
#include "command_buffer.hpp"

#pragma mark - Private C++ Statics -
NSUInteger FAGXCommandQueue::PermittedOptions = 0;
uint64 FAGXCommandQueue::Features = 0;
extern mtlpp::VertexFormat GAGXFColorVertexFormat;
bool GAGXCommandBufferDebuggingEnabled = 0;

#pragma mark - Public C++ Boilerplate -

FAGXCommandQueue::FAGXCommandQueue(uint32 const MaxNumCommandBuffers /* = 0 */)
: ParallelCommandLists(0)
, RuntimeDebuggingLevel(EAGXDebugLevelOff)
{
	int32 MaxShaderVersion = 0;
	int32 IndirectArgumentTier = 0;
#if PLATFORM_MAC
	int32 DefaultMaxShaderVersion = 5; // MSL v2.2
	int32 MinShaderVersion = 4; // MSL v2.1
    const TCHAR* const Settings = TEXT("/Script/MacTargetPlatform.MacTargetSettings");
#else
	int32 DefaultMaxShaderVersion = 2;
	int32 MinShaderVersion = 2;
    const TCHAR* const Settings = TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings");
#endif
    if(!GConfig->GetInt(Settings, TEXT("MaxShaderLanguageVersion"), MaxShaderVersion, GEngineIni))
    {
        MaxShaderVersion = DefaultMaxShaderVersion;
    }
	if(!GConfig->GetInt(Settings, TEXT("IndirectArgumentTier"), IndirectArgumentTier, GEngineIni))
	{
		IndirectArgumentTier = 0;
	}
	MaxShaderVersion = FMath::Max(MinShaderVersion, MaxShaderVersion);
	AGXValidateVersion(MaxShaderVersion);

	if(MaxNumCommandBuffers == 0)
	{
		CommandQueue = [GMtlDevice newCommandQueue];
	}
	else
	{
		CommandQueue = [GMtlDevice newCommandQueueWithMaxCommandBufferCount:MaxNumCommandBuffers];
	}
	check(CommandQueue);
#if PLATFORM_IOS
	NSOperatingSystemVersion Vers = [[NSProcessInfo processInfo] operatingSystemVersion];
	if(Vers.majorVersion >= 9)
	{
		Features = EAGXFeaturesSetBufferOffset | EAGXFeaturesSetBytes;

#if PLATFORM_TVOS
        Features &= ~(EAGXFeaturesSetBytes);
		
		if ([GMtlDevice supportsFeatureSet:MTLFeatureSet_tvOS_GPUFamily2_v1])
		{
			Features |= EAGXFeaturesCountingQueries | EAGXFeaturesBaseVertexInstance | EAGXFeaturesIndirectBuffer | EAGXFeaturesMSAADepthResolve | EAGXFeaturesMSAAStoreAndResolve;
		}
		
		if(Vers.majorVersion > 10)
		{
			Features |= EAGXFeaturesPrivateBufferSubAllocation;
			
			if(Vers.majorVersion >= 11)
			{
				Features |= EAGXFeaturesGPUCaptureManager | EAGXFeaturesBufferSubAllocation | EAGXFeaturesParallelRenderEncoders | EAGXFeaturesPipelineBufferMutability;
				
				if (MaxShaderVersion >= 3)
				{
					GAGXFColorVertexFormat = mtlpp::VertexFormat::UChar4Normalized_BGRA;
				}

				if (Vers.majorVersion >= 12)
				{
					Features |= EAGXFeaturesMaxThreadsPerThreadgroup;
					
					if (FParse::Param(FCommandLine::Get(),TEXT("metalfence")))
					{
					Features |= EAGXFeaturesFences;
					}
					
					if (FParse::Param(FCommandLine::Get(),TEXT("metalheap")))
					{
					Features |= EAGXFeaturesHeaps;
					}
					
					if (MaxShaderVersion >= 4)
					{
						Features |= EAGXFeaturesTextureBuffers;
					}
				}
			}
		}
#else
		if ([GMtlDevice supportsFeatureSet:MTLFeatureSet_iOS_GPUFamily3_v1])
		{
			Features |= EAGXFeaturesCountingQueries | EAGXFeaturesBaseVertexInstance | EAGXFeaturesIndirectBuffer | EAGXFeaturesMSAADepthResolve;
		}
		
		if ([GMtlDevice supportsFeatureSet:MTLFeatureSet_iOS_GPUFamily3_v2] || [GMtlDevice supportsFeatureSet:MTLFeatureSet_iOS_GPUFamily2_v3] || [GMtlDevice supportsFeatureSet:MTLFeatureSet_iOS_GPUFamily1_v3])
		{
			if (FParse::Param(FCommandLine::Get(),TEXT("metalfence")))
			{
				Features |= EAGXFeaturesFences;
			}
			
			if (FParse::Param(FCommandLine::Get(),TEXT("metalheap")))
			{
				Features |= EAGXFeaturesHeaps;
			}
		}
		
		if ([GMtlDevice supportsFeatureSet:MTLFeatureSet_iOS_GPUFamily3_v2])
		{
			Features |= EAGXFeaturesMSAAStoreAndResolve;
		}
		
		if(Vers.majorVersion > 10 || (Vers.majorVersion == 10 && Vers.minorVersion >= 3))
        {
			// Turning the below option on will allocate more buffer memory which isn't generally desirable on iOS
			// Features |= EAGXFeaturesEfficientBufferBlits;
			
			// These options are fine however as thye just change how we allocate small buffers
            Features |= EAGXFeaturesBufferSubAllocation;
			Features |= EAGXFeaturesPrivateBufferSubAllocation;
			
			if(Vers.majorVersion >= 11)
			{
				if (MaxShaderVersion >= 3)
				{
					GAGXFColorVertexFormat = mtlpp::VertexFormat::UChar4Normalized_BGRA;
				}
				
				Features |= EAGXFeaturesPresentMinDuration | EAGXFeaturesGPUCaptureManager | EAGXFeaturesBufferSubAllocation | EAGXFeaturesParallelRenderEncoders | EAGXFeaturesPipelineBufferMutability;
                
				// Turn on Texture Buffers! These are faster on the GPU as we don't need to do out-of-bounds tests but require Metal 2.1 and macOS 10.14
				if (Vers.majorVersion >= 12)
				{
					Features |= EAGXFeaturesMaxThreadsPerThreadgroup;
                    if (!FParse::Param(FCommandLine::Get(),TEXT("nometalfence")))
                    {
                        Features |= EAGXFeaturesFences;
                    }
                    
                    if (!FParse::Param(FCommandLine::Get(),TEXT("nometalheap")))
                    {
                        Features |= EAGXFeaturesHeaps;
                    }
					
					if (MaxShaderVersion >= 4)
					{
						Features |= EAGXFeaturesTextureBuffers;
					}
                    
                    if ([GMtlDevice supportsFeatureSet:MTLFeatureSet_iOS_GPUFamily4_v1])
                    {
                        Features |= EAGXFeaturesTileShaders;
                        
                        // The below implies tile shaders which are necessary to order the draw calls and generate a buffer that shows what PSOs/draws ran on each tile.
                        IConsoleVariable* GPUCrashDebuggingCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.GPUCrashDebugging"));
                        GAGXCommandBufferDebuggingEnabled = (GPUCrashDebuggingCVar && GPUCrashDebuggingCVar->GetInt() != 0) || FParse::Param(FCommandLine::Get(),TEXT("metalgpudebug"));
                    }
                    
					if ([GMtlDevice supportsFeatureSet:MTLFeatureSet_iOS_GPUFamily5_v1])
					{
						Features |= EAGXFeaturesLayeredRendering;
					}
				}
			}
        }
#endif
	}
	else if(Vers.majorVersion == 8 && Vers.minorVersion >= 3)
	{
		Features = EAGXFeaturesSetBufferOffset;
	}
#else // Assume that Mac & other platforms all support these from the start. They can diverge later.
	const bool bIsNVIDIA = [[GMtlDevice name] rangeOfString:@"Nvidia" options:NSCaseInsensitiveSearch].location != NSNotFound;
	Features = EAGXFeaturesCountingQueries | EAGXFeaturesBaseVertexInstance | EAGXFeaturesIndirectBuffer | EAGXFeaturesLayeredRendering | EAGXFeaturesCubemapArrays;
	if (!bIsNVIDIA)
	{
		Features |= EAGXFeaturesSetBufferOffset;
	}
	if ([GMtlDevice supportsFeatureSet:MTLFeatureSet_macOS_GPUFamily1_v2])
    {
        Features |= (   EAGXFeaturesSetBytes
					  | EAGXFeaturesMSAADepthResolve
					  | EAGXFeaturesMSAAStoreAndResolve
					  | EAGXFeaturesEfficientBufferBlits
					  | EAGXFeaturesBufferSubAllocation
					  | EAGXFeaturesPrivateBufferSubAllocation
					  | EAGXFeaturesMaxThreadsPerThreadgroup );
		
		GAGXFColorVertexFormat = mtlpp::VertexFormat::UChar4Normalized_BGRA;
		
		if ([[GMtlDevice name] rangeOfString:@"Nvidia" options:NSCaseInsensitiveSearch].location == NSNotFound && !FParse::Param(FCommandLine::Get(),TEXT("nometalparallelencoder")))
		{
			Features |= EAGXFeaturesParallelRenderEncoders;
		}

		{
			if (MaxShaderVersion >= 4)
			{
				Features |= EAGXFeaturesTextureBuffers;
            }
            if (IndirectArgumentTier >= 1)
            {
                Features |= EAGXFeaturesIABs;
				
				if (IndirectArgumentTier >= 2)
				{
					Features |= EAGXFeaturesTier2IABs;
				}
            }
            
            IConsoleVariable* GPUCrashDebuggingCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.GPUCrashDebugging"));
            GAGXCommandBufferDebuggingEnabled = (GPUCrashDebuggingCVar && GPUCrashDebuggingCVar->GetInt() != 0) || FParse::Param(FCommandLine::Get(),TEXT("metalgpudebug"));
            
            // The editor spawns so many viewports and preview icons that we can run out of hardware fences!
			// Need to figure out a way to safely flush the rendering and reuse the fences when that happens.
#if WITH_EDITORONLY_DATA
			if (!GIsEditor)
#endif
			{
				if (FParse::Param(FCommandLine::Get(),TEXT("metalfence")))
				{
					Features |= EAGXFeaturesFences;
				}
				
				// There are still too many driver bugs to use MTLHeap on macOS - nothing works without causing random, undebuggable GPU hangs that completely deadlock the Mac and don't generate any validation errors or command-buffer failures
				if (FParse::Param(FCommandLine::Get(),TEXT("forcemetalheap")))
				{
					Features |= EAGXFeaturesHeaps;
				}
			}
		}
    }
    else if ([[GMtlDevice name] rangeOfString:@"Nvidia" options:NSCaseInsensitiveSearch].location != NSNotFound)
    {
		// Using set*Bytes fixes bugs on Nvidia for 10.11 so we should use it...
    	Features |= EAGXFeaturesSetBytes;
    }
    
    if ([GMtlDevice supportsFeatureSet:MTLFeatureSet_macOS_GPUFamily1_v3])
    {
        Features |= EAGXFeaturesMultipleViewports | EAGXFeaturesPipelineBufferMutability | EAGXFeaturesGPUCaptureManager;
		
		if (FParse::Param(FCommandLine::Get(),TEXT("metalfence")))
		{
			Features |= EAGXFeaturesFences;
		}
		
		if (FParse::Param(FCommandLine::Get(),TEXT("metalheap")))
		{
			Features |= EAGXFeaturesHeaps;
		}
		
		if (FParse::Param(FCommandLine::Get(),TEXT("metaliabs")))
		{
			Features |= EAGXFeaturesIABs;
		}
    }
#endif
	
#if !UE_BUILD_SHIPPING
	Class MTLDebugDevice = NSClassFromString(@"MTLDebugDevice");
	if ([GMtlDevice isKindOfClass:MTLDebugDevice])
	{
		Features |= EAGXFeaturesValidation;
	}
#endif

	static const auto CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shaders.Optimize"));
	if (CVar->GetInt() == 0 || FParse::Param(FCommandLine::Get(),TEXT("metalshaderdebug")))
	{
		Features |= EAGXFeaturesGPUTrace;
	}

	PermittedOptions = 0;
	PermittedOptions |= mtlpp::ResourceOptions::CpuCacheModeDefaultCache;
	PermittedOptions |= mtlpp::ResourceOptions::CpuCacheModeWriteCombined;
	{
		PermittedOptions |= mtlpp::ResourceOptions::StorageModeShared;
		PermittedOptions |= mtlpp::ResourceOptions::StorageModePrivate;
#if PLATFORM_MAC
		PermittedOptions |= mtlpp::ResourceOptions::StorageModeManaged;
#else
		PermittedOptions |= mtlpp::ResourceOptions::StorageModeMemoryless;
#endif
		// You can't use HazardUntracked under the validation layer due to bugs in the layer when trying to create linear-textures/texture-buffers
		if ((Features & EAGXFeaturesFences) && !(Features & EAGXFeaturesValidation))
		{
			PermittedOptions |= mtlpp::ResourceOptions::HazardTrackingModeUntracked;
		}
	}
}

FAGXCommandQueue::~FAGXCommandQueue(void)
{
	if (CommandQueue != nil)
	{
		[CommandQueue release];
		CommandQueue = nil;
	}
}
	
#pragma mark - Public Command Buffer Mutators -

mtlpp::CommandBuffer FAGXCommandQueue::CreateCommandBuffer(void)
{
#if PLATFORM_MAC
	static bool bUnretainedRefs = FParse::Param(FCommandLine::Get(),TEXT("metalunretained"))
	|| (!FParse::Param(FCommandLine::Get(),TEXT("metalretainrefs"))
		&& ([[GMtlDevice name] rangeOfString:@"Nvidia" options:NSCaseInsensitiveSearch].location == NSNotFound)
		&& ([[GMtlDevice name] rangeOfString:@"Intel"  options:NSCaseInsensitiveSearch].location == NSNotFound));
#else
	static bool bUnretainedRefs = !FParse::Param(FCommandLine::Get(),TEXT("metalretainrefs"));
#endif
	
	mtlpp::CommandBuffer CmdBuffer;
	@autoreleasepool
	{
		id<MTLCommandBuffer> MtlCmdBuf = bUnretainedRefs ? [CommandQueue commandBufferWithUnretainedReferences] : [CommandQueue commandBuffer];
		check(MtlCmdBuf);
		
		CmdBuffer = mtlpp::CommandBuffer(MtlCmdBuf);
		
		if (RuntimeDebuggingLevel > EAGXDebugLevelOff)
		{			
			METAL_DEBUG_ONLY(FAGXCommandBufferDebugging AddDebugging(CmdBuffer));
			MTLPP_VALIDATION(mtlpp::CommandBufferValidationTable ValidatedCommandBuffer(CmdBuffer));
		}
	}
	CommandBufferFences.Push(new mtlpp::CommandBufferFence(CmdBuffer.GetCompletionFence()));
	INC_DWORD_STAT(STAT_AGXCommandBufferCreatedPerFrame);
	return CmdBuffer;
}

void FAGXCommandQueue::CommitCommandBuffer(mtlpp::CommandBuffer& CommandBuffer)
{
	check(CommandBuffer);
	INC_DWORD_STAT(STAT_AGXCommandBufferCommittedPerFrame);
	
	MTLPP_VALIDATE(mtlpp::CommandBuffer, CommandBuffer, AGXSafeGetRuntimeDebuggingLevel() >= EAGXDebugLevelValidation, Commit());
	
	// Wait for completion when debugging command-buffers.
	if (RuntimeDebuggingLevel >= EAGXDebugLevelWaitForComplete)
	{
		CommandBuffer.WaitUntilCompleted();
	}
}

void FAGXCommandQueue::SubmitCommandBuffers(TArray<mtlpp::CommandBuffer> BufferList, uint32 Index, uint32 Count)
{
	CommandBuffers.SetNumZeroed(Count);
	CommandBuffers[Index] = BufferList;
	ParallelCommandLists |= (1 << Index);
	if (ParallelCommandLists == ((1 << Count) - 1))
	{
		for (uint32 i = 0; i < Count; i++)
		{
			TArray<mtlpp::CommandBuffer>& CmdBuffers = CommandBuffers[i];
			for (mtlpp::CommandBuffer Buffer : CmdBuffers)
			{
				check(Buffer);
				CommitCommandBuffer(Buffer);
			}
			CommandBuffers[i].Empty();
		}
		
		ParallelCommandLists = 0;
	}
}

FAGXFence* FAGXCommandQueue::CreateFence(ns::String const& Label) const
{
	if ((Features & EAGXFeaturesFences) != 0)
	{
		FAGXFence* InternalFence = FAGXFencePool::Get().AllocateFence();
		for (uint32 i = mtlpp::RenderStages::Vertex; InternalFence && i <= mtlpp::RenderStages::Fragment; i++)
		{
			mtlpp::Fence InnerFence = InternalFence->Get((mtlpp::RenderStages)i);
			NSString* String = nil;
			if (GetEmitDrawEvents())
			{
				String = [NSString stringWithFormat:@"%u %p: %@", i, InnerFence.GetPtr(), Label.GetPtr()];
			}
	#if METAL_DEBUG_OPTIONS
			if (RuntimeDebuggingLevel >= EAGXDebugLevelValidation)
			{
				FAGXDebugFence* Fence = (FAGXDebugFence*)InnerFence.GetPtr();
				Fence.label = String;
			}
			else
	#endif
			if(InnerFence && String)
			{
				InnerFence.SetLabel(String);
			}
		}
		return InternalFence;
	}
	else
	{
		return nullptr;
	}
}

void FAGXCommandQueue::GetCommittedCommandBufferFences(TArray<mtlpp::CommandBufferFence>& Fences)
{
	TArray<mtlpp::CommandBufferFence*> Temp;
	CommandBufferFences.PopAll(Temp);
	for (mtlpp::CommandBufferFence* Fence : Temp)
	{
		Fences.Add(*Fence);
		delete Fence;
	}
}

#pragma mark - Public Command Queue Accessors -

mtlpp::ResourceOptions FAGXCommandQueue::GetCompatibleResourceOptions(mtlpp::ResourceOptions Options)
{
	NSUInteger NewOptions = (Options & PermittedOptions);
#if PLATFORM_IOS // Swizzle Managed to Shared for iOS - we can do this as they are equivalent, unlike Shared -> Managed on Mac.
	if ((Options & (1 /*mtlpp::StorageMode::Managed*/ << mtlpp::ResourceStorageModeShift)))
	{
		NewOptions |= mtlpp::ResourceOptions::StorageModeShared;
	}
#endif
	return (mtlpp::ResourceOptions)NewOptions;
}

#pragma mark - Public Debug Support -

void FAGXCommandQueue::InsertDebugCaptureBoundary(void)
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	[CommandQueue insertDebugCaptureBoundary];
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void FAGXCommandQueue::SetRuntimeDebuggingLevel(int32 const Level)
{
	RuntimeDebuggingLevel = Level;
}

int32 FAGXCommandQueue::GetRuntimeDebuggingLevel(void) const
{
	return RuntimeDebuggingLevel;
}
