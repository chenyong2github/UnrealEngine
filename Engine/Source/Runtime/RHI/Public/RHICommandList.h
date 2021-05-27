// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RHICommandList.h: RHI Command List definitions for queueing up & executing later.
=============================================================================*/

#pragma once
#include "CoreTypes.h"
#include "Misc/AssertionMacros.h"
#include "HAL/UnrealMemory.h"
#include "Templates/UnrealTemplate.h"
#include "Math/Color.h"
#include "Math/IntPoint.h"
#include "Math/IntRect.h"
#include "Math/Box2D.h"
#include "Math/PerspectiveMatrix.h"
#include "Math/TranslationMatrix.h"
#include "Math/ScaleMatrix.h"
#include "Math/Float16Color.h"
#include "HAL/ThreadSafeCounter.h"
#include "GenericPlatform/GenericPlatformProcess.h"
#include "Misc/MemStack.h"
#include "Misc/App.h"
#include "Stats/Stats.h"
#include "HAL/IConsoleManager.h"
#include "Async/TaskGraphInterfaces.h"
#include "HAL/LowLevelMemTracker.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Trace/Trace.h"

CSV_DECLARE_CATEGORY_MODULE_EXTERN(RHI_API, RHITStalls);
CSV_DECLARE_CATEGORY_MODULE_EXTERN(RHI_API, RHITFlushes);

// Set to 1 to capture the callstack for every RHI command. Cheap & memory efficient representation: Use the 
// value in FRHICommand::StackFrames to get the pointer to the code (ie paste on a disassembly window)
#define RHICOMMAND_CALLSTACK		0
#if RHICOMMAND_CALLSTACK
#include "HAL/PlatformStackwalk.h"
#endif

#define DISABLE_BREADCRUMBS 1

class FApp;
class FBlendStateInitializerRHI;
class FGraphicsPipelineStateInitializer;
class FLastRenderTimeContainer;
class FRHICommandListBase;
class FRHIComputeShader;
class IRHICommandContext;
class IRHIComputeContext;
struct FDepthStencilStateInitializerRHI;
struct FRasterizerStateInitializerRHI;
struct FRHIResourceCreateInfo;
struct FRHIResourceInfo;
struct FRHIUniformBufferLayout;
struct FSamplerStateInitializerRHI;
struct FTextureMemoryStats;
class FComputePipelineState;
class FGraphicsPipelineState;
class FRayTracingPipelineState;

DECLARE_STATS_GROUP(TEXT("RHICmdList"), STATGROUP_RHICMDLIST, STATCAT_Advanced);

UE_TRACE_CHANNEL_EXTERN(RHICommandsChannel, RHI_API);

// set this one to get a stat for each RHI command 
#define RHI_STATS 0

#if RHI_STATS
DECLARE_STATS_GROUP(TEXT("RHICommands"),STATGROUP_RHI_COMMANDS, STATCAT_Advanced);
#define RHISTAT(Method)	DECLARE_SCOPE_CYCLE_COUNTER(TEXT(#Method), STAT_RHI##Method, STATGROUP_RHI_COMMANDS)
#else
#define RHISTAT(Method)
#endif

extern RHI_API bool GUseRHIThread_InternalUseOnly;
extern RHI_API bool GUseRHITaskThreads_InternalUseOnly;
extern RHI_API bool GIsRunningRHIInSeparateThread_InternalUseOnly;
extern RHI_API bool GIsRunningRHIInDedicatedThread_InternalUseOnly;
extern RHI_API bool GIsRunningRHIInTaskThread_InternalUseOnly;

/** private accumulator for the RHI thread. */
extern RHI_API uint32 GWorkingRHIThreadTime;
extern RHI_API uint32 GWorkingRHIThreadStallTime;
extern RHI_API uint32 GWorkingRHIThreadStartCycles;

/** How many cycles the from sampling input to the frame being flipped. */
extern RHI_API uint64 GInputLatencyTime;

/*Trace::FChannel& FORCEINLINE GetRHICommandsChannel() 
{

}*/

/**
* Whether the RHI commands are being run in a thread other than the render thread
*/
bool FORCEINLINE IsRunningRHIInSeparateThread()
{
	return GIsRunningRHIInSeparateThread_InternalUseOnly;
}

/**
* Whether the RHI commands are being run on a dedicated thread other than the render thread
*/
bool FORCEINLINE IsRunningRHIInDedicatedThread()
{
	return GIsRunningRHIInDedicatedThread_InternalUseOnly;
}

/**
* Whether the RHI commands are being run on a dedicated thread other than the render thread
*/
bool FORCEINLINE IsRunningRHIInTaskThread()
{
	return GIsRunningRHIInTaskThread_InternalUseOnly;
}


extern RHI_API bool GEnableAsyncCompute;
extern RHI_API TAutoConsoleVariable<int32> CVarRHICmdWidth;
extern RHI_API TAutoConsoleVariable<int32> CVarRHICmdFlushRenderThreadTasks;

#if RHI_RAYTRACING
struct FRayTracingShaderBindings
{
	FRHITexture* Textures[64] = {};
	FRHIShaderResourceView* SRVs[64] = {};
	FRHIUniformBuffer* UniformBuffers[16] = {};
	FRHISamplerState* Samplers[16] = {};
	FRHIUnorderedAccessView* UAVs[16] = {};
};

struct FRayTracingLocalShaderBindings
{
	uint32 InstanceIndex = 0;
	uint32 SegmentIndex = 0;
	uint32 ShaderSlot = 0;
	uint32 ShaderIndexInPipeline = 0;
	uint32 UserData = 0;
	uint16 NumUniformBuffers = 0;
	uint16 LooseParameterDataSize = 0;
	FRHIUniformBuffer** UniformBuffers = nullptr;
	uint8* LooseParameterData = nullptr;
};

// C++ counter-part of FBasicRayData declared in RayTracingCommon.ush
struct FBasicRayData
{
	float Origin[3];
	uint32 Mask;
	float Direction[3];
	float TFar;
};

// C++ counter-part of FIntersectionPayload declared in RayTracingCommon.ush
struct FIntersectionPayload
{
	float  HitT;            // Distance from ray origin to the intersection point in the ray direction. Negative on miss.
	uint32 PrimitiveIndex;  // Index of the primitive within the geometry inside the bottom-level acceleration structure instance. Undefined on miss.
	uint32 InstanceIndex;   // Index of the current instance in the top-level structure. Undefined on miss.
	float  Barycentrics[2]; // Primitive barycentric coordinates of the intersection point. Undefined on miss.
};
#endif // RHI_RAYTRACING

struct RHI_API FLockTracker
{
	struct FLockParams
	{
		void* RHIBuffer;
		void* Buffer;
		uint32 BufferSize;
		uint32 Offset;
		EResourceLockMode LockMode;

		FORCEINLINE_DEBUGGABLE FLockParams(void* InRHIBuffer, void* InBuffer, uint32 InOffset, uint32 InBufferSize, EResourceLockMode InLockMode)
			: RHIBuffer(InRHIBuffer)
			, Buffer(InBuffer)
			, BufferSize(InBufferSize)
			, Offset(InOffset)
			, LockMode(InLockMode)
		{
		}
	};
	TArray<FLockParams, TInlineAllocator<16> > OutstandingLocks;
	uint32 TotalMemoryOutstanding;

	FLockTracker()
	{
		TotalMemoryOutstanding = 0;
	}

	FORCEINLINE_DEBUGGABLE void Lock(void* RHIBuffer, void* Buffer, uint32 Offset, uint32 SizeRHI, EResourceLockMode LockMode)
	{
#if DO_CHECK
		for (auto& Parms : OutstandingLocks)
		{
			check(Parms.RHIBuffer != RHIBuffer);
		}
#endif
		OutstandingLocks.Add(FLockParams(RHIBuffer, Buffer, Offset, SizeRHI, LockMode));
		TotalMemoryOutstanding += SizeRHI;
	}
	FORCEINLINE_DEBUGGABLE FLockParams Unlock(void* RHIBuffer)
	{
		for (int32 Index = 0; Index < OutstandingLocks.Num(); Index++)
		{
			if (OutstandingLocks[Index].RHIBuffer == RHIBuffer)
			{
				FLockParams Result = OutstandingLocks[Index];
				OutstandingLocks.RemoveAtSwap(Index, 1, false);
				return Result;
			}
		}
		check(!"Mismatched RHI buffer locks.");
		return FLockParams(nullptr, nullptr, 0, 0, RLM_WriteOnly);
	}
};

#ifdef CONTINUABLE_PSO_VERIFY
#define PSO_VERIFY ensure
#else
#define PSO_VERIFY	check
#endif

class IRHICommandContextContainer
{
public:
	virtual ~IRHICommandContextContainer()
	{
	}

	virtual IRHICommandContext* GetContext()
	{
		return nullptr;
	}

	virtual void SubmitAndFreeContextContainer(int32 Index, int32 Num)
	{
		check(0);
	}

	virtual void FinishContext()
	{
		check(0);
	}
};

struct FRHICommandListDebugContext
{
	FRHICommandListDebugContext()
	{
#if RHI_COMMAND_LIST_DEBUG_TRACES
		DebugStringStore[MaxDebugStoreSize] = 1337;
#endif
	}

	void PushMarker(const TCHAR* Marker)
	{
#if RHI_COMMAND_LIST_DEBUG_TRACES
		//allocate a new slot for the stack of pointers
		//and preserve the top of the stack in case we reach the limit
		if (++DebugMarkerStackIndex >= MaxDebugMarkerStackDepth)
		{
			for (uint32 i = 1; i < MaxDebugMarkerStackDepth; i++)
			{
				DebugMarkerStack[i - 1] = DebugMarkerStack[i];
				DebugMarkerSizes[i - 1] = DebugMarkerSizes[i];
			}
			DebugMarkerStackIndex = MaxDebugMarkerStackDepth - 1;
		}

		//try and copy the sting into the debugstore on the stack
		TCHAR* Offset = &DebugStringStore[DebugStoreOffset];
		uint32 MaxLength = MaxDebugStoreSize - DebugStoreOffset;
		uint32 Length = TryCopyString(Offset, Marker, MaxLength) + 1;

		//if we reached the end reset to the start and try again
		if (Length >= MaxLength)
		{
			DebugStoreOffset = 0;
			Offset = &DebugStringStore[DebugStoreOffset];
			MaxLength = MaxDebugStoreSize;
			Length = TryCopyString(Offset, Marker, MaxLength) + 1;

			//if the sting was bigger than the size of the store just terminate what we have
			if (Length >= MaxDebugStoreSize)
			{
				DebugStringStore[MaxDebugStoreSize - 1] = TEXT('\0');
			}
		}

		//add the string to the stack
		DebugMarkerStack[DebugMarkerStackIndex] = Offset;
		DebugStoreOffset += Length;
		DebugMarkerSizes[DebugMarkerStackIndex] = Length;

		check(DebugStringStore[MaxDebugStoreSize] == 1337);
#endif
	}

	void PopMarker()
	{
#if RHI_COMMAND_LIST_DEBUG_TRACES
		//clean out the debug stack if we have valid data
		if (DebugMarkerStackIndex >= 0 && DebugMarkerStackIndex < MaxDebugMarkerStackDepth)
		{
			DebugMarkerStack[DebugMarkerStackIndex] = nullptr;
			//also free the data in the store to postpone wrapping as much as possibler
			DebugStoreOffset -= DebugMarkerSizes[DebugMarkerStackIndex];

			//in case we already wrapped in the past just assume we start allover again
			if (DebugStoreOffset >= MaxDebugStoreSize)
			{
				DebugStoreOffset = 0;
			}
		}

		//pop the stack pointer
		if (--DebugMarkerStackIndex == (~0u) - 1)
		{
			//in case we wrapped in the past just restart
			DebugMarkerStackIndex = ~0u;
		}
#endif
	}

#if RHI_COMMAND_LIST_DEBUG_TRACES
private:

	//Tries to copy a string and early exits if it hits the limit. 
	//Returns the size of the string or the limit when reached.
	uint32 TryCopyString(TCHAR* Dest, const TCHAR* Source, uint32 MaxLength)
	{
		uint32 Length = 0;
		while(Source[Length] != TEXT('\0') && Length < MaxLength)
		{
			Dest[Length] = Source[Length];
			Length++;
		}

		if (Length < MaxLength)
		{
			Dest[Length] = TEXT('\0');
		}
		return Length;
	}

	uint32 DebugStoreOffset = 0;
	static constexpr int MaxDebugStoreSize = 1023;
	TCHAR DebugStringStore[MaxDebugStoreSize + 1];

	uint32 DebugMarkerStackIndex = ~0u;
	static constexpr int MaxDebugMarkerStackDepth = 32;
	const TCHAR* DebugMarkerStack[MaxDebugMarkerStackDepth] = {};
	uint32 DebugMarkerSizes[MaxDebugMarkerStackDepth] = {};
#endif
};

struct FRHICommandBase
{
	FRHICommandBase* Next = nullptr;
	virtual void ExecuteAndDestruct(FRHICommandListBase& CmdList, FRHICommandListDebugContext& DebugContext) = 0;
};

// Thread-safe allocator for GPU fences used in deferred command list execution
// Fences are stored in a ringbuffer
class RHI_API FRHICommandListFenceAllocator
{
public:
	static const int MAX_FENCE_INDICES		= 4096;
	FRHICommandListFenceAllocator()
	{
		CurrentFenceIndex = 0;
		for ( int i=0; i<MAX_FENCE_INDICES; i++)
		{
			FenceIDs[i] = 0xffffffffffffffffull;
			FenceFrameNumber[i] = 0xffffffff;
		}
	}

	uint32 AllocFenceIndex()
	{
		check(IsInRenderingThread());
		uint32 FenceIndex = ( FPlatformAtomics::InterlockedIncrement(&CurrentFenceIndex)-1 ) % MAX_FENCE_INDICES;
		check(FenceFrameNumber[FenceIndex] != GFrameNumberRenderThread);
		FenceFrameNumber[FenceIndex] = GFrameNumberRenderThread;

		return FenceIndex;
	}

	volatile uint64& GetFenceID( int32 FenceIndex )
	{
		check( FenceIndex < MAX_FENCE_INDICES );
		return FenceIDs[ FenceIndex ];
	}

private:
	volatile int32 CurrentFenceIndex;
	uint64 FenceIDs[MAX_FENCE_INDICES];
	uint32 FenceFrameNumber[MAX_FENCE_INDICES];
};

extern RHI_API FRHICommandListFenceAllocator GRHIFenceAllocator;

class RHI_API FRHICommandListBase : public FNoncopyable
{
public:
	~FRHICommandListBase();

	/** Custom new/delete with recycling */
	void* operator new(size_t Size);
	void operator delete(void *RawMemory);

	inline void Flush();
	inline bool IsImmediate();
	inline bool IsImmediateAsyncCompute();

	const int32 GetUsedMemory() const;
	void QueueAsyncCommandListSubmit(FGraphEventRef& AnyThreadCompletionEvent, class FRHICommandList* CmdList);
	void QueueParallelAsyncCommandListSubmit(FGraphEventRef* AnyThreadCompletionEvents, bool bIsPrepass, class FRHICommandList** CmdLists, int32* NumDrawsIfKnown, int32 Num, int32 MinDrawsPerTranslate, bool bSpewMerge);
	void QueueRenderThreadCommandListSubmit(FGraphEventRef& RenderThreadCompletionEvent, class FRHICommandList* CmdList);
	void QueueCommandListSubmit(class FRHICommandList* CmdList);
	void AddDispatchPrerequisite(const FGraphEventRef& Prereq);
	void WaitForTasks(bool bKnownToBeComplete = false);
	void WaitForDispatch();
	void WaitForRHIThreadTasks();
	void HandleRTThreadTaskCompletion(const FGraphEventRef& MyCompletionGraphEvent);

	FORCEINLINE_DEBUGGABLE void* Alloc(int32 AllocSize, int32 Alignment)
	{
		checkSlow(!Bypass() && "Can't use RHICommandList in bypass mode.");
		return MemManager.Alloc(AllocSize, Alignment);
	}

	template <typename T>
	FORCEINLINE_DEBUGGABLE void* Alloc()
	{
		return Alloc(sizeof(T), alignof(T));
	}

	template <typename T>
	FORCEINLINE_DEBUGGABLE const TArrayView<T> AllocArray(const TArrayView<T> InArray)
	{
		void* NewArray = Alloc(InArray.Num() * sizeof(T), alignof(T));
		FMemory::Memcpy(NewArray, InArray.GetData(), InArray.Num() * sizeof(T));
		return TArrayView<T>((T*) NewArray, InArray.Num());
	}

	FORCEINLINE_DEBUGGABLE TCHAR* AllocString(const TCHAR* Name)
	{
		int32 Len = FCString::Strlen(Name) + 1;
		TCHAR* NameCopy  = (TCHAR*)Alloc(Len * (int32)sizeof(TCHAR), (int32)sizeof(TCHAR));
		FCString::Strcpy(NameCopy, Len, Name);
		return NameCopy;
	}

	FORCEINLINE_DEBUGGABLE void* AllocCommand(int32 AllocSize, int32 Alignment)
	{
		checkSlow(!IsExecuting());
		FRHICommandBase* Result = (FRHICommandBase*) MemManager.Alloc(AllocSize, Alignment);
		++NumCommands;
		*CommandLink = Result;
		CommandLink = &Result->Next;
		return Result;
	}
	template <typename TCmd>
	FORCEINLINE void* AllocCommand()
	{
		return AllocCommand(sizeof(TCmd), alignof(TCmd));
	}

	FORCEINLINE uint32 GetUID()  const
	{
		return UID;
	}
	FORCEINLINE bool HasCommands() const
	{
		return (NumCommands > 0);
	}
	FORCEINLINE bool IsExecuting() const
	{
		return bExecuting;
	}
	FORCEINLINE bool IsBottomOfPipe() const
	{
		return Bypass() || IsExecuting();
	}

	FORCEINLINE bool IsTopOfPipe() const
	{
		return !IsBottomOfPipe();
	}

	FORCEINLINE bool IsGraphics() const
	{
		return Context != nullptr;
	}

	FORCEINLINE bool IsAsyncCompute() const
	{
		return Context == nullptr && ComputeContext != nullptr;
	}

	FORCEINLINE ERHIPipeline GetPipeline() const
	{
		return IsAsyncCompute() ? ERHIPipeline::AsyncCompute : ERHIPipeline::Graphics;
	}

	bool Bypass() const;

	FORCEINLINE void ExchangeCmdList(FRHICommandListBase& Other)
	{
		check(!RTTasks.Num() && !Other.RTTasks.Num());
		FMemory::Memswap(this, &Other, sizeof(FRHICommandListBase));
		if (CommandLink == &Other.Root)
		{
			CommandLink = &Root;
		}
		if (Other.CommandLink == &Root)
		{
			Other.CommandLink = &Other.Root;
		}
		Other.BoundShaderInput = BoundShaderInput;
		Other.BoundComputeShaderRHI = BoundComputeShaderRHI;
	}

	void SetContext(IRHICommandContext* InContext)
	{
		check(InContext);
		Context = InContext;
		ComputeContext = InContext;
	}

	FORCEINLINE IRHICommandContext& GetContext()
	{
		checkSlow(Context);
		return *Context;
	}

	void SetComputeContext(IRHIComputeContext* InComputeContext)
	{
		check(InComputeContext);
		check(Context == nullptr);
		ComputeContext = InComputeContext;
	}

	IRHIComputeContext& GetComputeContext()
	{
		checkSlow(ComputeContext);
		return *ComputeContext;
	}

	void CopyContext(FRHICommandListBase& ParentCommandList)
	{
		Context = ParentCommandList.Context;
		ComputeContext = ParentCommandList.ComputeContext;
	}

	void MaybeDispatchToRHIThread()
	{
		if (IsImmediate() && HasCommands() && GRHIThreadNeedsKicking && IsRunningRHIInSeparateThread())
		{
			MaybeDispatchToRHIThreadInner();
		}
	}
	void MaybeDispatchToRHIThreadInner();

	FORCEINLINE const FRHIGPUMask& GetGPUMask() const { return GPUMask; }

private:
	FRHICommandBase* Root;
	FRHICommandBase** CommandLink;
	bool bExecuting;
	uint32 NumCommands;
	uint32 UID;
	IRHICommandContext* Context;
	IRHIComputeContext* ComputeContext;
	FMemStackBase MemManager; 
	FGraphEventArray RTTasks;

	friend class FRHICommandListExecutor;
	friend class FRHICommandListIterator;
	friend class FRHICommandListScopedFlushAndExecute;

protected:
	FRHICommandListBase(FRHIGPUMask InGPUMask);

	bool bAsyncPSOCompileAllowed;
	FRHIGPUMask GPUMask;
	// GPUMask that was set at the time the command list was last Reset. We set
    // this mask on the command contexts immediately before executing the
    // command list. This way we don't need to worry about having any initial
    // FRHICommandSetGPUMask at the root of the list.
	FRHIGPUMask InitialGPUMask;
	void Reset();

public:
	TStatId	ExecuteStat;
	enum class ERenderThreadContext
	{
		SceneRenderTargets,
		Num
	};
	void *RenderThreadContexts[(int32)ERenderThreadContext::Num];

protected:
	//the values of this struct must be copied when the commandlist is split 
	struct FPSOContext
	{
		uint32 CachedNumSimultanousRenderTargets = 0;
		TStaticArray<FRHIRenderTargetView, MaxSimultaneousRenderTargets> CachedRenderTargets;
		FRHIDepthRenderTargetView CachedDepthStencilTarget;
		
		ESubpassHint SubpassHint = ESubpassHint::None;
		uint8 SubpassIndex = 0;
		uint8 MultiViewCount = 0;
		bool HasFragmentDensityAttachment = false;
	} PSOContext;

	FBoundShaderStateInput BoundShaderInput;
	FRHIComputeShader* BoundComputeShaderRHI;

	FORCEINLINE void ValidateBoundShader(FRHIVertexShader* ShaderRHI) { checkSlow(BoundShaderInput.VertexShaderRHI == ShaderRHI); }
	FORCEINLINE void ValidateBoundShader(FRHIPixelShader* ShaderRHI) { checkSlow(BoundShaderInput.PixelShaderRHI == ShaderRHI); }
	FORCEINLINE void ValidateBoundShader(FRHIGeometryShader* ShaderRHI) { checkSlow(BoundShaderInput.GeometryShaderRHI == ShaderRHI); }
	FORCEINLINE void ValidateBoundShader(FRHIHullShader* ShaderRHI) { checkSlow(BoundShaderInput.HullShaderRHI == ShaderRHI); }
	FORCEINLINE void ValidateBoundShader(FRHIDomainShader* ShaderRHI) { checkSlow(BoundShaderInput.DomainShaderRHI == ShaderRHI); }
	FORCEINLINE void ValidateBoundShader(FRHIComputeShader* ShaderRHI) { checkSlow(BoundComputeShaderRHI == ShaderRHI); }

	FORCEINLINE void ValidateBoundShader(FRHIGraphicsShader* ShaderRHI)
	{
#if DO_GUARD_SLOW
		switch (ShaderRHI->GetFrequency())
		{
		case SF_Vertex: checkSlow(BoundShaderInput.VertexShaderRHI == ShaderRHI); break;
		case SF_Hull: checkSlow(BoundShaderInput.HullShaderRHI == ShaderRHI); break;
		case SF_Domain: checkSlow(BoundShaderInput.DomainShaderRHI == ShaderRHI); break;
		case SF_Pixel: checkSlow(BoundShaderInput.PixelShaderRHI == ShaderRHI); break;
		case SF_Geometry: checkSlow(BoundShaderInput.GeometryShaderRHI == ShaderRHI); break;
		default: checkfSlow(false, TEXT("Unexpected graphics shader type %d"), ShaderRHI->GetFrequency());
		}
#endif // DO_GUARD_SLOW
	}

	void CacheActiveRenderTargets(
		uint32 NewNumSimultaneousRenderTargets,
		const FRHIRenderTargetView* NewRenderTargetsRHI,
		const FRHIDepthRenderTargetView* NewDepthStencilTargetRHI,
		const bool HasFragmentDensityAttachment,
		const uint8 MultiViewCount
		)
	{
		PSOContext.CachedNumSimultanousRenderTargets = NewNumSimultaneousRenderTargets;

		for (uint32 RTIdx = 0; RTIdx < PSOContext.CachedNumSimultanousRenderTargets; ++RTIdx)
		{
			PSOContext.CachedRenderTargets[RTIdx] = NewRenderTargetsRHI[RTIdx];
		}

		PSOContext.CachedDepthStencilTarget = (NewDepthStencilTargetRHI) ? *NewDepthStencilTargetRHI : FRHIDepthRenderTargetView();
		PSOContext.HasFragmentDensityAttachment = HasFragmentDensityAttachment;
		PSOContext.MultiViewCount = MultiViewCount;
	}

	void CacheActiveRenderTargets(const FRHIRenderPassInfo& Info)
	{
		FRHISetRenderTargetsInfo RTInfo;
		Info.ConvertToRenderTargetsInfo(RTInfo);
		CacheActiveRenderTargets(RTInfo.NumColorRenderTargets, RTInfo.ColorRenderTarget, &RTInfo.DepthStencilRenderTarget, RTInfo.ShadingRateTexture != nullptr, RTInfo.MultiViewCount);
	}

	void IncrementSubpass()
	{
		PSOContext.SubpassIndex++;
	}
	
	void ResetSubpass(ESubpassHint SubpassHint)
	{
		PSOContext.SubpassHint = SubpassHint;
		PSOContext.SubpassIndex = 0;
	}
	
public:
	void CopyRenderThreadContexts(const FRHICommandListBase& ParentCommandList)
	{
		for (int32 Index = 0; ERenderThreadContext(Index) < ERenderThreadContext::Num; Index++)
		{
			RenderThreadContexts[Index] = ParentCommandList.RenderThreadContexts[Index];
		}
	}
	void SetRenderThreadContext(void* InContext, ERenderThreadContext Slot)
	{
		RenderThreadContexts[int32(Slot)] = InContext;
	}
	FORCEINLINE void* GetRenderThreadContext(ERenderThreadContext Slot)
	{
		return RenderThreadContexts[int32(Slot)];
	}

	struct FCommonData
	{
		class FRHICommandListBase* Parent = nullptr;

		enum class ECmdListType
		{
			Immediate = 1,
			Regular,
		};
		ECmdListType Type = ECmdListType::Regular;
		bool bInsideRenderPass = false;
		bool bInsideComputePass = false;
	};

	bool DoValidation() const
	{
		static auto* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RenderPass.Validation"));
		return CVar && CVar->GetInt() != 0;
	}

	inline bool IsOutsideRenderPass() const
	{
		return !Data.bInsideRenderPass;
	}

	inline bool IsInsideRenderPass() const
	{
		return Data.bInsideRenderPass;
	}

	inline bool IsInsideComputePass() const
	{
		return Data.bInsideComputePass;
	}

	FCommonData Data;
};

struct FUnnamedRhiCommand
{
	static const TCHAR* TStr() { return TEXT("FUnnamedRhiCommand"); }
};

template<typename TCmd, typename NameType = FUnnamedRhiCommand>
struct FRHICommand : public FRHICommandBase
{
#if RHICOMMAND_CALLSTACK
	uint64 StackFrames[16];

	FRHICommand()
	{
		FPlatformStackWalk::CaptureStackBackTrace(StackFrames, 16);
	}
#endif

	void ExecuteAndDestruct(FRHICommandListBase& CmdList, FRHICommandListDebugContext& Context) override final
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL_STR(NameType::TStr(), RHICommandsChannel);
		
		TCmd *ThisCmd = static_cast<TCmd*>(this);
#if RHI_COMMAND_LIST_DEBUG_TRACES
		ThisCmd->StoreDebugInfo(Context);
#endif
		ThisCmd->Execute(CmdList);
		ThisCmd->~TCmd();
	}

	virtual void StoreDebugInfo(FRHICommandListDebugContext& Context) {};
};

#define FRHICOMMAND_MACRO(CommandName)								\
struct PREPROCESSOR_JOIN(CommandName##String, __LINE__)				\
{																	\
	static const TCHAR* TStr() { return TEXT(#CommandName); }		\
};																	\
struct CommandName final : public FRHICommand<CommandName, PREPROCESSOR_JOIN(CommandName##String, __LINE__)>

FRHICOMMAND_MACRO(FRHICommandBeginUpdateMultiFrameResource)
{
	FRHITexture* Texture;
	FORCEINLINE_DEBUGGABLE FRHICommandBeginUpdateMultiFrameResource(FRHITexture* InTexture)
		: Texture(InTexture)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandEndUpdateMultiFrameResource)
{
	FRHITexture* Texture;
	FORCEINLINE_DEBUGGABLE FRHICommandEndUpdateMultiFrameResource(FRHITexture* InTexture)
		: Texture(InTexture)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandBeginUpdateMultiFrameUAV)
{
	FRHIUnorderedAccessView* UAV;
	FORCEINLINE_DEBUGGABLE FRHICommandBeginUpdateMultiFrameUAV(FRHIUnorderedAccessView* InUAV)
		: UAV(InUAV)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandEndUpdateMultiFrameUAV)
{
	FRHIUnorderedAccessView* UAV;
	FORCEINLINE_DEBUGGABLE FRHICommandEndUpdateMultiFrameUAV(FRHIUnorderedAccessView* InUAV)
		: UAV(InUAV)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

#if WITH_MGPU
FRHICOMMAND_MACRO(FRHICommandSetGPUMask)
{
	FRHIGPUMask GPUMask;
	FORCEINLINE_DEBUGGABLE FRHICommandSetGPUMask(FRHIGPUMask InGPUMask)
		: GPUMask(InGPUMask)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandWaitForTemporalEffect)
{
	FName EffectName;
	FORCEINLINE_DEBUGGABLE FRHICommandWaitForTemporalEffect(const FName& InEffectName)
		: EffectName(InEffectName)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandBroadcastTemporalEffectString
{
	static const TCHAR* TStr() { return TEXT("FRHICommandBroadcastTemporalEffect"); }
};
template <typename TRHIResource>
struct FRHICommandBroadcastTemporalEffect final	: public FRHICommand<FRHICommandBroadcastTemporalEffect<TRHIResource>, FRHICommandBroadcastTemporalEffectString>
{
	FName EffectName;
	const TArrayView<TRHIResource*> Resources;
	FORCEINLINE_DEBUGGABLE FRHICommandBroadcastTemporalEffect(const FName& InEffectName, const TArrayView<TRHIResource*> InResources)
		: EffectName(InEffectName)
		, Resources(InResources)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandTransferTextures)
{
	TArray<FTransferTextureParams, TInlineAllocator<4>> Params;

	FORCEINLINE_DEBUGGABLE FRHICommandTransferTextures(TArrayView<const FTransferTextureParams> InParams)
		: Params(InParams)
	{
	}

	RHI_API void Execute(FRHICommandListBase& CmdList);
};

#endif // WITH_MGPU

FRHICOMMAND_MACRO(FRHICommandSetStencilRef)
{
	uint32 StencilRef;
	FORCEINLINE_DEBUGGABLE FRHICommandSetStencilRef(uint32 InStencilRef)
		: StencilRef(InStencilRef)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandSetShaderParameterString
{
	static const TCHAR* TStr() { return TEXT("FRHICommandSetShaderParameter"); }
};
template <typename TRHIShader>
struct FRHICommandSetShaderParameter final : public FRHICommand<FRHICommandSetShaderParameter<TRHIShader>, FRHICommandSetShaderParameterString>
{
	TRHIShader* Shader;
	const void* NewValue;
	uint32 BufferIndex;
	uint32 BaseIndex;
	uint32 NumBytes;
	FORCEINLINE_DEBUGGABLE FRHICommandSetShaderParameter(TRHIShader* InShader, uint32 InBufferIndex, uint32 InBaseIndex, uint32 InNumBytes, const void* InNewValue)
		: Shader(InShader)
		, NewValue(InNewValue)
		, BufferIndex(InBufferIndex)
		, BaseIndex(InBaseIndex)
		, NumBytes(InNumBytes)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandSetShaderUniformBufferString
{
	static const TCHAR* TStr() { return TEXT("FRHICommandSetShaderUniformBuffer"); }
};
template <typename TRHIShader>
struct FRHICommandSetShaderUniformBuffer final : public FRHICommand<FRHICommandSetShaderUniformBuffer<TRHIShader>, FRHICommandSetShaderUniformBufferString>
{
	TRHIShader* Shader;
	uint32 BaseIndex;
	FRHIUniformBuffer* UniformBuffer;
	FORCEINLINE_DEBUGGABLE FRHICommandSetShaderUniformBuffer(TRHIShader* InShader, uint32 InBaseIndex, FRHIUniformBuffer* InUniformBuffer)
		: Shader(InShader)
		, BaseIndex(InBaseIndex)
		, UniformBuffer(InUniformBuffer)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandSetShaderTextureString
{
	static const TCHAR* TStr() { return TEXT("FRHICommandSetShaderTexture"); }
};
template <typename TRHIShader>
struct FRHICommandSetShaderTexture final : public FRHICommand<FRHICommandSetShaderTexture<TRHIShader>, FRHICommandSetShaderTextureString >
{
	TRHIShader* Shader;
	uint32 TextureIndex;
	FRHITexture* Texture;
	FORCEINLINE_DEBUGGABLE FRHICommandSetShaderTexture(TRHIShader* InShader, uint32 InTextureIndex, FRHITexture* InTexture)
		: Shader(InShader)
		, TextureIndex(InTextureIndex)
		, Texture(InTexture)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandSetShaderResourceViewParameterString
{
	static const TCHAR* TStr() { return TEXT("FRHICommandSetShaderResourceViewParameter"); }
};
template <typename TRHIShader>
struct FRHICommandSetShaderResourceViewParameter final : public FRHICommand<FRHICommandSetShaderResourceViewParameter<TRHIShader>, FRHICommandSetShaderResourceViewParameterString >
{
	TRHIShader* Shader;
	uint32 SamplerIndex;
	FRHIShaderResourceView* SRV;
	FORCEINLINE_DEBUGGABLE FRHICommandSetShaderResourceViewParameter(TRHIShader* InShader, uint32 InSamplerIndex, FRHIShaderResourceView* InSRV)
		: Shader(InShader)
		, SamplerIndex(InSamplerIndex)
		, SRV(InSRV)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandSetUAVParameterString
{
	static const TCHAR* TStr() { return TEXT("FRHICommandSetUAVParameter"); }
};
template <typename TRHIShader>
struct FRHICommandSetUAVParameter final : public FRHICommand<FRHICommandSetUAVParameter<TRHIShader>, FRHICommandSetUAVParameterString >
{
	TRHIShader* Shader;
	uint32 UAVIndex;
	FRHIUnorderedAccessView* UAV;
	FORCEINLINE_DEBUGGABLE FRHICommandSetUAVParameter(TRHIShader* InShader, uint32 InUAVIndex, FRHIUnorderedAccessView* InUAV)
		: Shader(InShader)
		, UAVIndex(InUAVIndex)
		, UAV(InUAV)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandSetUAVParameter_InitialCountString
{
	static const TCHAR* TStr() { return TEXT("FRHICommandSetUAVParameter_InitialCount"); }
};
struct FRHICommandSetUAVParameter_InitialCount final : public FRHICommand<FRHICommandSetUAVParameter_InitialCount, FRHICommandSetUAVParameter_InitialCountString >
{
	FRHIComputeShader* Shader;
	uint32 UAVIndex;
	FRHIUnorderedAccessView* UAV;
	uint32 InitialCount;
	FORCEINLINE_DEBUGGABLE FRHICommandSetUAVParameter_InitialCount(FRHIComputeShader* InShader, uint32 InUAVIndex, FRHIUnorderedAccessView* InUAV, uint32 InInitialCount)
		: Shader(InShader)
		, UAVIndex(InUAVIndex)
		, UAV(InUAV)
		, InitialCount(InInitialCount)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandSetShaderSamplerString
{
	static const TCHAR* TStr() { return TEXT("FRHICommandSetShaderSampler"); }
};
template <typename TRHIShader>
struct FRHICommandSetShaderSampler final : public FRHICommand<FRHICommandSetShaderSampler<TRHIShader>, FRHICommandSetShaderSamplerString >
{
	TRHIShader* Shader;
	uint32 SamplerIndex;
	FRHISamplerState* Sampler;
	FORCEINLINE_DEBUGGABLE FRHICommandSetShaderSampler(TRHIShader* InShader, uint32 InSamplerIndex, FRHISamplerState* InSampler)
		: Shader(InShader)
		, SamplerIndex(InSamplerIndex)
		, Sampler(InSampler)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandDrawPrimitive)
{
	uint32 BaseVertexIndex;
	uint32 NumPrimitives;
	uint32 NumInstances;
	FORCEINLINE_DEBUGGABLE FRHICommandDrawPrimitive(uint32 InBaseVertexIndex, uint32 InNumPrimitives, uint32 InNumInstances)
		: BaseVertexIndex(InBaseVertexIndex)
		, NumPrimitives(InNumPrimitives)
		, NumInstances(InNumInstances)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandDrawIndexedPrimitive)
{
	FRHIIndexBuffer* IndexBuffer;
	int32 BaseVertexIndex;
	uint32 FirstInstance;
	uint32 NumVertices;
	uint32 StartIndex;
	uint32 NumPrimitives;
	uint32 NumInstances;
	FORCEINLINE_DEBUGGABLE FRHICommandDrawIndexedPrimitive(FRHIIndexBuffer* InIndexBuffer, int32 InBaseVertexIndex, uint32 InFirstInstance, uint32 InNumVertices, uint32 InStartIndex, uint32 InNumPrimitives, uint32 InNumInstances)
		: IndexBuffer(InIndexBuffer)
		, BaseVertexIndex(InBaseVertexIndex)
		, FirstInstance(InFirstInstance)
		, NumVertices(InNumVertices)
		, StartIndex(InStartIndex)
		, NumPrimitives(InNumPrimitives)
		, NumInstances(InNumInstances)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandSetBlendFactor)
{
	FLinearColor BlendFactor;
	FORCEINLINE_DEBUGGABLE FRHICommandSetBlendFactor(const FLinearColor& InBlendFactor)
		: BlendFactor(InBlendFactor)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandSetStreamSource)
{
	uint32 StreamIndex;
	FRHIVertexBuffer* VertexBuffer;
	uint32 Offset;
	FORCEINLINE_DEBUGGABLE FRHICommandSetStreamSource(uint32 InStreamIndex, FRHIVertexBuffer* InVertexBuffer, uint32 InOffset)
		: StreamIndex(InStreamIndex)
		, VertexBuffer(InVertexBuffer)
		, Offset(InOffset)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandSetViewport)
{
	float MinX;
	float MinY;
	float MinZ;
	float MaxX;
	float MaxY;
	float MaxZ;
	FORCEINLINE_DEBUGGABLE FRHICommandSetViewport(float InMinX, float InMinY, float InMinZ, float InMaxX, float InMaxY, float InMaxZ)
		: MinX(InMinX)
		, MinY(InMinY)
		, MinZ(InMinZ)
		, MaxX(InMaxX)
		, MaxY(InMaxY)
		, MaxZ(InMaxZ)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandSetStereoViewport)
{
	float LeftMinX;
	float RightMinX;
	float LeftMinY;
	float RightMinY;
	float MinZ;
	float LeftMaxX;
	float RightMaxX;
	float LeftMaxY;
	float RightMaxY;
	float MaxZ;
	FORCEINLINE_DEBUGGABLE FRHICommandSetStereoViewport(float InLeftMinX, float InRightMinX, float InLeftMinY, float InRightMinY, float InMinZ, float InLeftMaxX, float InRightMaxX, float InLeftMaxY, float InRightMaxY, float InMaxZ)
		: LeftMinX(InLeftMinX)
		, RightMinX(InRightMinX)
		, LeftMinY(InLeftMinY)
		, RightMinY(InRightMinY)
		, MinZ(InMinZ)
		, LeftMaxX(InLeftMaxX)
		, RightMaxX(InRightMaxX)
		, LeftMaxY(InLeftMaxY)
		, RightMaxY(InRightMaxY)
		, MaxZ(InMaxZ)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandSetScissorRect)
{
	bool bEnable;
	uint32 MinX;
	uint32 MinY;
	uint32 MaxX;
	uint32 MaxY;
	FORCEINLINE_DEBUGGABLE FRHICommandSetScissorRect(bool InbEnable, uint32 InMinX, uint32 InMinY, uint32 InMaxX, uint32 InMaxY)
		: bEnable(InbEnable)
		, MinX(InMinX)
		, MinY(InMinY)
		, MaxX(InMaxX)
		, MaxY(InMaxY)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandBeginRenderPass)
{
	FRHIRenderPassInfo Info;
	const TCHAR* Name;

	FRHICommandBeginRenderPass(const FRHIRenderPassInfo& InInfo, const TCHAR* InName)
		: Info(InInfo)
		, Name(InName)
	{
	}

	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandEndRenderPass)
{
	FRHICommandEndRenderPass()
	{
	}

	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandBeginLateLatching)
{
	int32 FrameNumber;

	FRHICommandBeginLateLatching(int32 InFrameNumber)
		:FrameNumber(InFrameNumber)
	{
	}

	RHI_API void Execute(FRHICommandListBase & CmdList);
};


FRHICOMMAND_MACRO(FRHICommandEndLateLatching)
{
	FRHICommandEndLateLatching()
	{
	}

	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandNextSubpass)
{
	FRHICommandNextSubpass()
	{
	}

	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FLocalCmdListParallelRenderPass
{
	TRefCountPtr<struct FRHIParallelRenderPass> RenderPass;
};

FRHICOMMAND_MACRO(FRHICommandBeginParallelRenderPass)
{
	FRHIRenderPassInfo Info;
	FLocalCmdListParallelRenderPass* LocalRenderPass;
	const TCHAR* Name;

	FRHICommandBeginParallelRenderPass(const FRHIRenderPassInfo& InInfo, FLocalCmdListParallelRenderPass* InLocalRenderPass, const TCHAR* InName)
		: Info(InInfo)
		, LocalRenderPass(InLocalRenderPass)
		, Name(InName)
	{
	}

	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandEndParallelRenderPass)
{
	FLocalCmdListParallelRenderPass* LocalRenderPass;

	FRHICommandEndParallelRenderPass(FLocalCmdListParallelRenderPass* InLocalRenderPass)
		: LocalRenderPass(InLocalRenderPass)
	{
	}

	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FLocalCmdListRenderSubPass
{
	TRefCountPtr<struct FRHIRenderSubPass> RenderSubPass;
};

FRHICOMMAND_MACRO(FRHICommandBeginRenderSubPass)
{
	FLocalCmdListParallelRenderPass* LocalRenderPass;
	FLocalCmdListRenderSubPass* LocalRenderSubPass;

	FRHICommandBeginRenderSubPass(FLocalCmdListParallelRenderPass* InLocalRenderPass, FLocalCmdListRenderSubPass* InLocalRenderSubPass)
		: LocalRenderPass(InLocalRenderPass)
		, LocalRenderSubPass(InLocalRenderSubPass)
	{
	}

	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandEndRenderSubPass)
{
	FLocalCmdListParallelRenderPass* LocalRenderPass;
	FLocalCmdListRenderSubPass* LocalRenderSubPass;

	FRHICommandEndRenderSubPass(FLocalCmdListParallelRenderPass* InLocalRenderPass, FLocalCmdListRenderSubPass* InLocalRenderSubPass)
		: LocalRenderPass(InLocalRenderPass)
		, LocalRenderSubPass(InLocalRenderSubPass)
	{
	}

	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandSetComputeShaderString
{
	static const TCHAR* TStr() { return TEXT("FRHICommandSetComputeShader"); }
};

struct FRHICommandSetComputeShader final : public FRHICommand<FRHICommandSetComputeShader, FRHICommandSetComputeShaderString>
{
	FRHIComputeShader* ComputeShader;
	FORCEINLINE_DEBUGGABLE FRHICommandSetComputeShader(FRHIComputeShader* InComputeShader)
		: ComputeShader(InComputeShader)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandSetComputePipelineStateString
{
	static const TCHAR* TStr() { return TEXT("FRHICommandSetComputePipelineState"); }
};

struct FRHICommandSetComputePipelineState final : public FRHICommand<FRHICommandSetComputePipelineState, FRHICommandSetComputePipelineStateString>
{
	FComputePipelineState* ComputePipelineState;
	FORCEINLINE_DEBUGGABLE FRHICommandSetComputePipelineState(FComputePipelineState* InComputePipelineState)
		: ComputePipelineState(InComputePipelineState)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandSetGraphicsPipelineState)
{
	FGraphicsPipelineState* GraphicsPipelineState;
	bool bApplyAdditionalState;
	FORCEINLINE_DEBUGGABLE FRHICommandSetGraphicsPipelineState(FGraphicsPipelineState* InGraphicsPipelineState, bool bInApplyAdditionalState)
		: GraphicsPipelineState(InGraphicsPipelineState)
		, bApplyAdditionalState(bInApplyAdditionalState)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandDispatchComputeShaderString
{
	static const TCHAR* TStr() { return TEXT("FRHICommandDispatchComputeShader"); }
};

struct FRHICommandDispatchComputeShader final : public FRHICommand<FRHICommandDispatchComputeShader, FRHICommandDispatchComputeShaderString>
{
	uint32 ThreadGroupCountX;
	uint32 ThreadGroupCountY;
	uint32 ThreadGroupCountZ;
	FORCEINLINE_DEBUGGABLE FRHICommandDispatchComputeShader(uint32 InThreadGroupCountX, uint32 InThreadGroupCountY, uint32 InThreadGroupCountZ)
		: ThreadGroupCountX(InThreadGroupCountX)
		, ThreadGroupCountY(InThreadGroupCountY)
		, ThreadGroupCountZ(InThreadGroupCountZ)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandDispatchIndirectComputeShaderString
{
	static const TCHAR* TStr() { return TEXT("FRHICommandDispatchIndirectComputeShader"); }
};

struct FRHICommandDispatchIndirectComputeShader final : public FRHICommand<FRHICommandDispatchIndirectComputeShader, FRHICommandDispatchIndirectComputeShaderString>
{
	FRHIVertexBuffer* ArgumentBuffer;
	uint32 ArgumentOffset;
	FORCEINLINE_DEBUGGABLE FRHICommandDispatchIndirectComputeShader(FRHIVertexBuffer* InArgumentBuffer, uint32 InArgumentOffset)
		: ArgumentBuffer(InArgumentBuffer)
		, ArgumentOffset(InArgumentOffset)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandBeginUAVOverlap)
{
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandEndUAVOverlap)
{
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandBeginSpecificUAVOverlap)
{
	TArrayView<FRHIUnorderedAccessView* const> UAVs;
	FORCEINLINE_DEBUGGABLE FRHICommandBeginSpecificUAVOverlap(TArrayView<FRHIUnorderedAccessView* const> InUAVs) : UAVs(InUAVs) {}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandEndSpecificUAVOverlap)
{
	TArrayView<FRHIUnorderedAccessView* const> UAVs;
	FORCEINLINE_DEBUGGABLE FRHICommandEndSpecificUAVOverlap(TArrayView<FRHIUnorderedAccessView* const> InUAVs) : UAVs(InUAVs) {}
	RHI_API void Execute(FRHICommandListBase & CmdList);
};

FRHICOMMAND_MACRO(FRHICommandDrawPrimitiveIndirect)
{
	FRHIVertexBuffer* ArgumentBuffer;
	uint32 ArgumentOffset;
	FORCEINLINE_DEBUGGABLE FRHICommandDrawPrimitiveIndirect(FRHIVertexBuffer* InArgumentBuffer, uint32 InArgumentOffset)
		: ArgumentBuffer(InArgumentBuffer)
		, ArgumentOffset(InArgumentOffset)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandDrawIndexedIndirect)
{
	FRHIIndexBuffer* IndexBufferRHI;
	FRHIStructuredBuffer* ArgumentsBufferRHI;
	uint32 DrawArgumentsIndex;
	uint32 NumInstances;

	FORCEINLINE_DEBUGGABLE FRHICommandDrawIndexedIndirect(FRHIIndexBuffer* InIndexBufferRHI, FRHIStructuredBuffer* InArgumentsBufferRHI, uint32 InDrawArgumentsIndex, uint32 InNumInstances)
		: IndexBufferRHI(InIndexBufferRHI)
		, ArgumentsBufferRHI(InArgumentsBufferRHI)
		, DrawArgumentsIndex(InDrawArgumentsIndex)
		, NumInstances(InNumInstances)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandDrawIndexedPrimitiveIndirect)
{
	FRHIIndexBuffer* IndexBuffer;
	FRHIVertexBuffer* ArgumentsBuffer;
	uint32 ArgumentOffset;

	FORCEINLINE_DEBUGGABLE FRHICommandDrawIndexedPrimitiveIndirect(FRHIIndexBuffer* InIndexBuffer, FRHIVertexBuffer* InArgumentsBuffer, uint32 InArgumentOffset)
		: IndexBuffer(InIndexBuffer)
		, ArgumentsBuffer(InArgumentsBuffer)
		, ArgumentOffset(InArgumentOffset)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandSetDepthBounds)
{
	float MinDepth;
	float MaxDepth;

	FORCEINLINE_DEBUGGABLE FRHICommandSetDepthBounds(float InMinDepth, float InMaxDepth)
		: MinDepth(InMinDepth)
		, MaxDepth(InMaxDepth)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandSetShadingRate)
{
	EVRSShadingRate   ShadingRate;
	EVRSRateCombiner  Combiner;

	FORCEINLINE_DEBUGGABLE FRHICommandSetShadingRate(EVRSShadingRate InShadingRate, EVRSRateCombiner InCombiner)
		: ShadingRate(InShadingRate),
		Combiner(InCombiner)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandSetShadingRateImage)
{
	FRHITexture* RateImageTexture;
	EVRSRateCombiner  Combiner;

	FORCEINLINE_DEBUGGABLE FRHICommandSetShadingRateImage(FRHITexture* InRateImageTexture, EVRSRateCombiner InCombiner)
		: RateImageTexture(InRateImageTexture),
		Combiner(InCombiner)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandClearUAVFloat)
{
	FRHIUnorderedAccessView* UnorderedAccessViewRHI;
	FVector4 Values;

	FORCEINLINE_DEBUGGABLE FRHICommandClearUAVFloat(FRHIUnorderedAccessView* InUnorderedAccessViewRHI, const FVector4& InValues)
		: UnorderedAccessViewRHI(InUnorderedAccessViewRHI)
		, Values(InValues)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandClearUAVUint)
{
	FRHIUnorderedAccessView* UnorderedAccessViewRHI;
	FUintVector4 Values;

	FORCEINLINE_DEBUGGABLE FRHICommandClearUAVUint(FRHIUnorderedAccessView* InUnorderedAccessViewRHI, const FUintVector4& InValues)
		: UnorderedAccessViewRHI(InUnorderedAccessViewRHI)
		, Values(InValues)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandCopyToResolveTarget)
{
	FResolveParams ResolveParams;
	FRHITexture* SourceTexture;
	FRHITexture* DestTexture;

	FORCEINLINE_DEBUGGABLE FRHICommandCopyToResolveTarget(FRHITexture* InSourceTexture, FRHITexture* InDestTexture, const FResolveParams& InResolveParams)
		: ResolveParams(InResolveParams)
		, SourceTexture(InSourceTexture)
		, DestTexture(InDestTexture)
	{
		ensure(SourceTexture);
		ensure(DestTexture);
		ensure(SourceTexture->GetTexture2D() || SourceTexture->GetTexture3D() || SourceTexture->GetTextureCube() || SourceTexture->GetTexture2DArray());
		ensure(DestTexture->GetTexture2D() || DestTexture->GetTexture3D() || DestTexture->GetTextureCube() || DestTexture->GetTexture2DArray());
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandCopyTexture)
{
	FRHICopyTextureInfo CopyInfo;
	FRHITexture* SourceTexture;
	FRHITexture* DestTexture;

	FORCEINLINE_DEBUGGABLE FRHICommandCopyTexture(FRHITexture* InSourceTexture, FRHITexture* InDestTexture, const FRHICopyTextureInfo& InCopyInfo)
		: CopyInfo(InCopyInfo)
		, SourceTexture(InSourceTexture)
		, DestTexture(InDestTexture)
	{
		ensure(SourceTexture);
		ensure(DestTexture);
		ensure(SourceTexture->GetTexture2D() || SourceTexture->GetTexture2DArray() || SourceTexture->GetTexture3D() || SourceTexture->GetTextureCube());
		ensure(DestTexture->GetTexture2D() || DestTexture->GetTexture2DArray() || DestTexture->GetTexture3D() || DestTexture->GetTextureCube());
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandResummarizeHTile)
{
	FRHITexture2D* DepthTexture;

	FORCEINLINE_DEBUGGABLE FRHICommandResummarizeHTile(FRHITexture2D* InDepthTexture)
	: DepthTexture(InDepthTexture)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandBeginTransitions)
{
	TArrayView<const FRHITransition*> Transitions;

	FRHICommandBeginTransitions(TArrayView<const FRHITransition*> InTransitions)
		: Transitions(InTransitions)
	{
	}

	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandEndTransitions)
{
	TArrayView<const FRHITransition*> Transitions;

	FRHICommandEndTransitions(TArrayView<const FRHITransition*> InTransitions)
		: Transitions(InTransitions)
	{
	}

	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandResourceTransition)
{
	FRHITransition* Transition;

	FRHICommandResourceTransition(FRHITransition* InTransition)
		: Transition(InTransition)
	{
	}

	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandSetAsyncComputeBudgetString
{
	static const TCHAR* TStr() { return TEXT("FRHICommandSetAsyncComputeBudget"); }
};

struct FRHICommandSetAsyncComputeBudget final : public FRHICommand<FRHICommandSetAsyncComputeBudget, FRHICommandSetAsyncComputeBudgetString>
{
	EAsyncComputeBudget Budget;

	FORCEINLINE_DEBUGGABLE FRHICommandSetAsyncComputeBudget(EAsyncComputeBudget InBudget)
		: Budget(InBudget)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandCopyToStagingBufferString
{
	static const TCHAR* TStr() { return TEXT("FRHICommandCopyToStagingBuffer"); }
};

struct FRHICommandCopyToStagingBuffer final : public FRHICommand<FRHICommandCopyToStagingBuffer, FRHICommandCopyToStagingBufferString>
{
	FRHIVertexBuffer* SourceBuffer;
	FRHIStagingBuffer* DestinationStagingBuffer;
	uint32 Offset;
	uint32 NumBytes;

	FORCEINLINE_DEBUGGABLE FRHICommandCopyToStagingBuffer(FRHIVertexBuffer* InSourceBuffer, FRHIStagingBuffer* InDestinationStagingBuffer, uint32 InOffset, uint32 InNumBytes)
		: SourceBuffer(InSourceBuffer)
		, DestinationStagingBuffer(InDestinationStagingBuffer)
		, Offset(InOffset)
		, NumBytes(InNumBytes)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandWriteGPUFenceString
{
	static const TCHAR* TStr() { return TEXT("FRHICommandWriteGPUFence"); }
};

struct FRHICommandWriteGPUFence final : public FRHICommand<FRHICommandWriteGPUFence, FRHICommandWriteGPUFenceString>
{
	FRHIGPUFence* Fence;

	FORCEINLINE_DEBUGGABLE FRHICommandWriteGPUFence(FRHIGPUFence* InFence)
		: Fence(InFence)
	{
		if (Fence)
		{
			Fence->NumPendingWriteCommands.Increment();
		}
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandClearColorTexture)
{
	FRHITexture* Texture;
	FLinearColor Color;

	FORCEINLINE_DEBUGGABLE FRHICommandClearColorTexture(
		FRHITexture* InTexture,
		const FLinearColor& InColor
		)
		: Texture(InTexture)
		, Color(InColor)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandClearDepthStencilTexture)
{
	FRHITexture* Texture;
	float Depth;
	uint32 Stencil;
	EClearDepthStencil ClearDepthStencil;

	FORCEINLINE_DEBUGGABLE FRHICommandClearDepthStencilTexture(
		FRHITexture* InTexture,
		EClearDepthStencil InClearDepthStencil,
		float InDepth,
		uint32 InStencil
	)
		: Texture(InTexture)
		, Depth(InDepth)
		, Stencil(InStencil)
		, ClearDepthStencil(InClearDepthStencil)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandClearColorTextures)
{
	FLinearColor ColorArray[MaxSimultaneousRenderTargets];
	FRHITexture* Textures[MaxSimultaneousRenderTargets];
	int32 NumClearColors;

	FORCEINLINE_DEBUGGABLE FRHICommandClearColorTextures(
		int32 InNumClearColors,
		FRHITexture** InTextures,
		const FLinearColor* InColorArray
		)
		: NumClearColors(InNumClearColors)
	{
		check(InNumClearColors <= MaxSimultaneousRenderTargets);
		for (int32 Index = 0; Index < InNumClearColors; Index++)
		{
			ColorArray[Index] = InColorArray[Index];
			Textures[Index] = InTextures[Index];
		}
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandSetGlobalUniformBuffers)
{
	FUniformBufferStaticBindings UniformBuffers;

	FORCEINLINE_DEBUGGABLE FRHICommandSetGlobalUniformBuffers(const FUniformBufferStaticBindings& InUniformBuffers)
		: UniformBuffers(InUniformBuffers)
	{}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FComputedGraphicsPipelineState
{
	FGraphicsPipelineStateRHIRef GraphicsPipelineState;
	int32 UseCount;
	FComputedGraphicsPipelineState()
		: UseCount(0)
	{
	}
};

struct FComputedUniformBuffer
{
	FUniformBufferRHIRef UniformBuffer;
	mutable int32 UseCount;
	FComputedUniformBuffer()
		: UseCount(0)
	{
	}
};

struct FLocalUniformBufferWorkArea
{
	void* Contents;
	const FRHIUniformBufferLayout* Layout;
	FComputedUniformBuffer* ComputedUniformBuffer;
#if DO_CHECK // the below variables are used in check(), which can be enabled in Shipping builds (see Build.h)
	FRHICommandListBase* CheckCmdList;
	int32 UID;
#endif

	FLocalUniformBufferWorkArea(FRHICommandListBase* InCheckCmdList, const void* InContents, uint32 ContentsSize, const FRHIUniformBufferLayout* InLayout)
		: Layout(InLayout)
#if DO_CHECK
		, CheckCmdList(InCheckCmdList)
		, UID(InCheckCmdList->GetUID())
#endif
	{
		check(ContentsSize);
		Contents = InCheckCmdList->Alloc(ContentsSize, SHADER_PARAMETER_STRUCT_ALIGNMENT);
		FMemory::Memcpy(Contents, InContents, ContentsSize);
		ComputedUniformBuffer = new (InCheckCmdList->Alloc<FComputedUniformBuffer>()) FComputedUniformBuffer;
	}
};

struct FLocalUniformBuffer
{
	FLocalUniformBufferWorkArea* WorkArea;
	FUniformBufferRHIRef BypassUniform; // this is only used in the case of Bypass, should eventually be deleted
	FLocalUniformBuffer()
		: WorkArea(nullptr)
	{
	}
	FLocalUniformBuffer(const FLocalUniformBuffer& Other)
		: WorkArea(Other.WorkArea)
		, BypassUniform(Other.BypassUniform)
	{
	}
	FORCEINLINE_DEBUGGABLE bool IsValid() const
	{
		return WorkArea || IsValidRef(BypassUniform);
	}
};

FRHICOMMAND_MACRO(FRHICommandBuildLocalUniformBuffer)
{
	FLocalUniformBufferWorkArea WorkArea;
	FORCEINLINE_DEBUGGABLE FRHICommandBuildLocalUniformBuffer(
		FRHICommandListBase* CheckCmdList,
		const void* Contents,
		uint32 ContentsSize,
		const FRHIUniformBufferLayout& Layout
		)
		: WorkArea(CheckCmdList, Contents, ContentsSize, &Layout)

	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandSetLocalUniformBufferString
{
	static const TCHAR* TStr() { return TEXT("FRHICommandSetLocalUniformBuffer"); }
};
template <typename TRHIShader>
struct FRHICommandSetLocalUniformBuffer final : public FRHICommand<FRHICommandSetLocalUniformBuffer<TRHIShader>, FRHICommandSetLocalUniformBufferString >
{
	TRHIShader* Shader;
	uint32 BaseIndex;
	FLocalUniformBuffer LocalUniformBuffer;
	FORCEINLINE_DEBUGGABLE FRHICommandSetLocalUniformBuffer(FRHICommandListBase* CheckCmdList, TRHIShader* InShader, uint32 InBaseIndex, const FLocalUniformBuffer& InLocalUniformBuffer)
		: Shader(InShader)
		, BaseIndex(InBaseIndex)
		, LocalUniformBuffer(InLocalUniformBuffer)

	{
		check(CheckCmdList == LocalUniformBuffer.WorkArea->CheckCmdList && CheckCmdList->GetUID() == LocalUniformBuffer.WorkArea->UID); // this uniform buffer was not built for this particular commandlist
		LocalUniformBuffer.WorkArea->ComputedUniformBuffer->UseCount++;
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandBeginRenderQuery)
{
	FRHIRenderQuery* RenderQuery;

	FORCEINLINE_DEBUGGABLE FRHICommandBeginRenderQuery(FRHIRenderQuery* InRenderQuery)
		: RenderQuery(InRenderQuery)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandEndRenderQuery)
{
	FRHIRenderQuery* RenderQuery;

	FORCEINLINE_DEBUGGABLE FRHICommandEndRenderQuery(FRHIRenderQuery* InRenderQuery)
		: RenderQuery(InRenderQuery)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandCalibrateTimers)
{
	FRHITimestampCalibrationQuery* CalibrationQuery;

	FORCEINLINE_DEBUGGABLE FRHICommandCalibrateTimers(FRHITimestampCalibrationQuery * CalibrationQuery)
		: CalibrationQuery(CalibrationQuery)
	{
	}
	RHI_API void Execute(FRHICommandListBase & CmdList);
};

FRHICOMMAND_MACRO(FRHICommandSubmitCommandsHint)
{
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandPostExternalCommandsReset)
{
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandPollOcclusionQueries)
{
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandBeginScene)
{
	FORCEINLINE_DEBUGGABLE FRHICommandBeginScene()
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandEndScene)
{
	FORCEINLINE_DEBUGGABLE FRHICommandEndScene()
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandBeginFrame)
{
	FORCEINLINE_DEBUGGABLE FRHICommandBeginFrame()
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandEndFrame)
{
	FORCEINLINE_DEBUGGABLE FRHICommandEndFrame()
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandBeginDrawingViewport)
{
	FRHIViewport* Viewport;
	FRHITexture* RenderTargetRHI;

	FORCEINLINE_DEBUGGABLE FRHICommandBeginDrawingViewport(FRHIViewport* InViewport, FRHITexture* InRenderTargetRHI)
		: Viewport(InViewport)
		, RenderTargetRHI(InRenderTargetRHI)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandEndDrawingViewport)
{
	FRHIViewport* Viewport;
	bool bPresent;
	bool bLockToVsync;

	FORCEINLINE_DEBUGGABLE FRHICommandEndDrawingViewport(FRHIViewport* InViewport, bool InbPresent, bool InbLockToVsync)
		: Viewport(InViewport)
		, bPresent(InbPresent)
		, bLockToVsync(InbLockToVsync)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandPushEventString
{
	static const TCHAR* TStr() { return TEXT("FRHICommandPushEventString"); }
};

struct FRHICommandPushEvent final : public FRHICommand<FRHICommandPushEvent, FRHICommandPushEventString>
{
	const TCHAR *Name;
	FColor Color;

	FORCEINLINE_DEBUGGABLE FRHICommandPushEvent(const TCHAR *InName, FColor InColor)
		: Name(InName)
		, Color(InColor)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);

	virtual void StoreDebugInfo(FRHICommandListDebugContext& Context)
	{
		Context.PushMarker(Name);
	};
};

struct FRHICommandPopEventString
{
	static const TCHAR* TStr() { return TEXT("FRHICommandPopEvent"); }
};

struct FRHICommandPopEvent final : public FRHICommand<FRHICommandPopEvent, FRHICommandPopEventString>
{
	RHI_API void Execute(FRHICommandListBase& CmdList);

	virtual void StoreDebugInfo(FRHICommandListDebugContext& Context)
	{
		Context.PopMarker();
	};
};

FRHICOMMAND_MACRO(FRHICommandInvalidateCachedState)
{
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandDiscardRenderTargets)
{
	uint32 ColorBitMask;
	bool Depth;
	bool Stencil;

	FORCEINLINE_DEBUGGABLE FRHICommandDiscardRenderTargets(bool InDepth, bool InStencil, uint32 InColorBitMask)
		: ColorBitMask(InColorBitMask)
		, Depth(InDepth)
		, Stencil(InStencil)
	{
	}
	
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandDebugBreak)
{
	void Execute(FRHICommandListBase& CmdList)
	{
		if (FPlatformMisc::IsDebuggerPresent())
		{
			UE_DEBUG_BREAK();
		}
	}
};

FRHICOMMAND_MACRO(FRHICommandUpdateTextureReference)
{
	FRHITextureReference* TextureRef;
	FRHITexture* NewTexture;
	FORCEINLINE_DEBUGGABLE FRHICommandUpdateTextureReference(FRHITextureReference* InTextureRef, FRHITexture* InNewTexture)
		: TextureRef(InTextureRef)
		, NewTexture(InNewTexture)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHIShaderResourceViewUpdateInfo_VB
{
	FRHIShaderResourceView* SRV;
	FRHIVertexBuffer* VertexBuffer;
	uint32 Stride;
	uint8 Format;
};

struct FRHIShaderResourceViewUpdateInfo_IB
{
	FRHIShaderResourceView* SRV;
	FRHIIndexBuffer* IndexBuffer;
};

struct FRHIVertexBufferUpdateInfo
{
	FRHIVertexBuffer* DestBuffer;
	FRHIVertexBuffer* SrcBuffer;
};

struct FRHIIndexBufferUpdateInfo
{
	FRHIIndexBuffer* DestBuffer;
	FRHIIndexBuffer* SrcBuffer;
};

struct FRHIResourceUpdateInfo
{
	enum EUpdateType
	{
		/** Take over underlying resource from an intermediate vertex buffer */
		UT_VertexBuffer,
		/** Take over underlying resource from an intermediate index buffer */
		UT_IndexBuffer,
		/** Update an SRV to view on a different vertex buffer */
		UT_VertexBufferSRV,
		/** Update an SRV to view on a different index buffer */
		UT_IndexBufferSRV,
		/** Number of update types */
		UT_Num
	};

	EUpdateType Type;
	union
	{
		FRHIVertexBufferUpdateInfo VertexBuffer;
		FRHIIndexBufferUpdateInfo IndexBuffer;
		FRHIShaderResourceViewUpdateInfo_VB VertexBufferSRV;
		FRHIShaderResourceViewUpdateInfo_IB IndexBufferSRV;
	};

	void ReleaseRefs();
};

FRHICOMMAND_MACRO(FRHICommandUpdateRHIResources)
{
	FRHIResourceUpdateInfo* UpdateInfos;
	int32 Num;
	bool bNeedReleaseRefs;

	FRHICommandUpdateRHIResources(FRHIResourceUpdateInfo* InUpdateInfos, int32 InNum, bool bInNeedReleaseRefs)
		: UpdateInfos(InUpdateInfos)
		, Num(InNum)
		, bNeedReleaseRefs(bInNeedReleaseRefs)
	{}

	~FRHICommandUpdateRHIResources();

	RHI_API void Execute(FRHICommandListBase& CmdList);
};

#if PLATFORM_USE_BACKBUFFER_WRITE_TRANSITION_TRACKING
FRHICOMMAND_MACRO(FRHICommandBackBufferWaitTrackingBeginFrame)
{
	uint64	FrameToken;
	bool	bDeferred;

	FORCEINLINE_DEBUGGABLE FRHICommandBackBufferWaitTrackingBeginFrame(uint64 FrameTokenIn, bool bDeferredIn)
		:	FrameToken(FrameTokenIn),
			bDeferred(bDeferredIn)
	{}
	
	RHI_API void Execute(FRHICommandListBase& CmdList);
};
#endif // #if PLATFORM_USE_BACKBUFFER_WRITE_TRANSITION_TRACKING

#if PLATFORM_REQUIRES_UAV_TO_RTV_TEXTURE_CACHE_FLUSH_WORKAROUND
FRHICOMMAND_MACRO(FRHICommandFlushTextureCacheBOP)
{
	FRHITexture* Texture;

	FORCEINLINE_DEBUGGABLE FRHICommandFlushTextureCacheBOP(FRHITexture* TextureIn)
		:	Texture(TextureIn)
	{}
	
	RHI_API void Execute(FRHICommandListBase& CmdList);
};
#endif // #if PLATFORM_REQUIRES_UAV_TO_RTV_TEXTURE_CACHE_FLUSH_WORKAROUND

FRHICOMMAND_MACRO(FRHICommandCopyBufferRegion)
{
	FRHIVertexBuffer* DestBuffer;
	uint64 DstOffset;
	FRHIVertexBuffer* SourceBuffer;
	uint64 SrcOffset;
	uint64 NumBytes;

	explicit FRHICommandCopyBufferRegion(FRHIVertexBuffer* InDestBuffer, uint64 InDstOffset, FRHIVertexBuffer* InSourceBuffer, uint64 InSrcOffset, uint64 InNumBytes)
		: DestBuffer(InDestBuffer)
		, DstOffset(InDstOffset)
		, SourceBuffer(InSourceBuffer)
		, SrcOffset(InSrcOffset)
		, NumBytes(InNumBytes)
	{}

	RHI_API void Execute(FRHICommandListBase& CmdList);
};

#if RHI_RAYTRACING

FRHICOMMAND_MACRO(FRHICommandCopyBufferRegions)
{
	const TArrayView<const FCopyBufferRegionParams> Params;

	explicit FRHICommandCopyBufferRegions(const TArrayView<const FCopyBufferRegionParams> InParams)
		: Params(InParams)
	{}

	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandBuildAccelerationStructure final : public FRHICommand<FRHICommandBuildAccelerationStructure>
{
	FRHIRayTracingScene* Scene;

	explicit FRHICommandBuildAccelerationStructure(FRHIRayTracingScene* InScene)
		: Scene(InScene)
	{}

	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandClearRayTracingBindings)
{
	FRHIRayTracingScene* Scene;

	explicit FRHICommandClearRayTracingBindings(FRHIRayTracingScene* InScene)
		: Scene(InScene)
	{}

	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandBuildAccelerationStructures final : public FRHICommand<FRHICommandBuildAccelerationStructures>
{
	const TArrayView<const FAccelerationStructureBuildParams> Params;

	explicit FRHICommandBuildAccelerationStructures(const TArrayView<const FAccelerationStructureBuildParams> InParams)
		: Params(InParams)
	{}

	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandRayTraceOcclusion)
{
	FRHIRayTracingScene* Scene;
	FRHIShaderResourceView* Rays;
	FRHIUnorderedAccessView* Output;
	uint32 NumRays;

	FRHICommandRayTraceOcclusion(FRHIRayTracingScene* InScene,
		FRHIShaderResourceView* InRays,
		FRHIUnorderedAccessView* InOutput,
		uint32 InNumRays)
		: Scene(InScene)
		, Rays(InRays)
		, Output(InOutput)
		, NumRays(InNumRays)
	{}

	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandRayTraceIntersection)
{
	FRHIRayTracingScene* Scene;
	FRHIShaderResourceView* Rays;
	FRHIUnorderedAccessView* Output;
	uint32 NumRays;

	FRHICommandRayTraceIntersection(FRHIRayTracingScene* InScene,
		FRHIShaderResourceView* InRays,
		FRHIUnorderedAccessView* InOutput,
		uint32 InNumRays)
		: Scene(InScene)
		, Rays(InRays)
		, Output(InOutput)
		, NumRays(InNumRays)
	{}

	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandRayTraceDispatch)
{
	FRayTracingPipelineState* Pipeline;
	FRHIRayTracingScene* Scene;
	FRayTracingShaderBindings GlobalResourceBindings;
	FRHIRayTracingShader* RayGenShader;
	uint32 Width;
	uint32 Height;

	FRHICommandRayTraceDispatch(FRayTracingPipelineState* InPipeline, FRHIRayTracingShader* InRayGenShader, FRHIRayTracingScene* InScene, const FRayTracingShaderBindings& InGlobalResourceBindings, uint32 InWidth, uint32 InHeight)
		: Pipeline(InPipeline)
		, Scene(InScene)
		, GlobalResourceBindings(InGlobalResourceBindings)
		, RayGenShader(InRayGenShader)
		, Width(InWidth)
		, Height(InHeight)
	{}

	RHI_API void Execute(FRHICommandListBase& CmdList);
};

FRHICOMMAND_MACRO(FRHICommandSetRayTracingBindings)
{
	enum EBindingType
	{
		EBindingType_HitGroup,
		EBindingType_CallableShader,
		EBindingType_MissShader,
		EBindingType_HitGroupBatch,
	};

	FRHIRayTracingScene* Scene = nullptr;
	EBindingType BindingType = EBindingType_HitGroup;
	uint32 InstanceIndex = 0;
	uint32 SegmentIndex = 0;
	uint32 ShaderSlot = 0;
	FRayTracingPipelineState* Pipeline = nullptr;
	uint32 ShaderIndex = 0;
	uint32 NumUniformBuffers = 0;
	FRHIUniformBuffer* const* UniformBuffers = nullptr; // Pointer to an array of uniform buffers, allocated inline within the command list
	uint32 LooseParameterDataSize = 0;
	const void* LooseParameterData = nullptr;
	uint32 UserData = 0;

	// Batched bindings
	uint32 NumBindings = 0;
	const FRayTracingLocalShaderBindings* Bindings = nullptr;

	// Hit group bindings
	FRHICommandSetRayTracingBindings(FRHIRayTracingScene* InScene, uint32 InInstanceIndex, uint32 InSegmentIndex, uint32 InShaderSlot,
		FRayTracingPipelineState* InPipeline, uint32 InHitGroupIndex, uint32 InNumUniformBuffers, FRHIUniformBuffer* const* InUniformBuffers,
		uint32 InLooseParameterDataSize, const void* InLooseParameterData,
		uint32 InUserData)
		: Scene(InScene)
		, BindingType(EBindingType_HitGroup)
		, InstanceIndex(InInstanceIndex)
		, SegmentIndex(InSegmentIndex)
		, ShaderSlot(InShaderSlot)
		, Pipeline(InPipeline)
		, ShaderIndex(InHitGroupIndex)
		, NumUniformBuffers(InNumUniformBuffers)
		, UniformBuffers(InUniformBuffers)
		, LooseParameterDataSize(InLooseParameterDataSize)
		, LooseParameterData(InLooseParameterData)
		, UserData(InUserData)
	{
	}

	// Batched hit group bindings
	FRHICommandSetRayTracingBindings(FRHIRayTracingScene* InScene, FRayTracingPipelineState* InPipeline, uint32 InNumBindings, const FRayTracingLocalShaderBindings* InBindings)
		: Scene(InScene)
		, BindingType(EBindingType_HitGroupBatch)
		, Pipeline(InPipeline)
		, NumBindings(InNumBindings)
		, Bindings(InBindings)
	{
	}

	// Callable and Miss shader bindings
	FRHICommandSetRayTracingBindings(FRHIRayTracingScene* InScene, uint32 InShaderSlot,
		FRayTracingPipelineState* InPipeline, uint32 InShaderIndex,
		uint32 InNumUniformBuffers, FRHIUniformBuffer* const* InUniformBuffers,
		uint32 InUserData, EBindingType InBindingType)
		: Scene(InScene)
		, BindingType(InBindingType)
		, InstanceIndex(0)
		, SegmentIndex(0)
		, ShaderSlot(InShaderSlot)
		, Pipeline(InPipeline)
		, ShaderIndex(InShaderIndex)
		, NumUniformBuffers(InNumUniformBuffers)
		, UniformBuffers(InUniformBuffers)
		, UserData(InUserData)
	{
	}

	RHI_API void Execute(FRHICommandListBase& CmdList);
};
#endif // RHI_RAYTRACING

// Using variadic macro because some types are fancy template<A,B> stuff, which gets broken off at the comma and interpreted as multiple arguments. 
#define ALLOC_COMMAND(...) new ( AllocCommand(sizeof(__VA_ARGS__), alignof(__VA_ARGS__)) ) __VA_ARGS__
#define ALLOC_COMMAND_CL(RHICmdList, ...) new ( (RHICmdList).AllocCommand(sizeof(__VA_ARGS__), alignof(__VA_ARGS__)) ) __VA_ARGS__

template<> void FRHICommandSetShaderParameter<FRHIComputeShader>::Execute(FRHICommandListBase& CmdList);
template<> void FRHICommandSetShaderUniformBuffer<FRHIComputeShader>::Execute(FRHICommandListBase& CmdList);
template<> void FRHICommandSetShaderTexture<FRHIComputeShader>::Execute(FRHICommandListBase& CmdList);
template<> void FRHICommandSetShaderResourceViewParameter<FRHIComputeShader>::Execute(FRHICommandListBase& CmdList);
template<> void FRHICommandSetShaderSampler<FRHIComputeShader>::Execute(FRHICommandListBase& CmdList);


class RHI_API FRHIComputeCommandList : public FRHICommandListBase
{
public:
	FRHIComputeCommandList(FRHIGPUMask GPUMask) : FRHICommandListBase(GPUMask) {}

	/** Custom new/delete with recycling */
	void* operator new(size_t Size);
	void operator delete(void *RawMemory);

	inline FRHIComputeShader* GetBoundComputeShader() const { return BoundComputeShaderRHI; }

	FORCEINLINE_DEBUGGABLE void SetGlobalUniformBuffers(const FUniformBufferStaticBindings& UniformBuffers)
	{
		if (Bypass())
		{
			GetComputeContext().RHISetGlobalUniformBuffers(UniformBuffers);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetGlobalUniformBuffers)(UniformBuffers);
	}

	FORCEINLINE_DEBUGGABLE void SetShaderUniformBuffer(FRHIComputeShader* Shader, uint32 BaseIndex, FRHIUniformBuffer* UniformBuffer)
	{
		ValidateBoundShader(Shader);
		if (Bypass())
		{
			GetComputeContext().RHISetShaderUniformBuffer(Shader, BaseIndex, UniformBuffer);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetShaderUniformBuffer<FRHIComputeShader>)(Shader, BaseIndex, UniformBuffer);
	}

	FORCEINLINE void SetShaderUniformBuffer(const FComputeShaderRHIRef& Shader, uint32 BaseIndex, FRHIUniformBuffer* UniformBuffer)
	{
		SetShaderUniformBuffer(Shader.GetReference(), BaseIndex, UniformBuffer);
	}

	FORCEINLINE_DEBUGGABLE void SetShaderParameter(FRHIComputeShader* Shader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue)
	{
		ValidateBoundShader(Shader);
		if (Bypass())
		{
			GetComputeContext().RHISetShaderParameter(Shader, BufferIndex, BaseIndex, NumBytes, NewValue);
			return;
		}
		void* UseValue = Alloc(NumBytes, 16);
		FMemory::Memcpy(UseValue, NewValue, NumBytes);
		ALLOC_COMMAND(FRHICommandSetShaderParameter<FRHIComputeShader>)(Shader, BufferIndex, BaseIndex, NumBytes, UseValue);
	}

	FORCEINLINE void SetShaderParameter(FComputeShaderRHIRef& Shader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue)
	{
		SetShaderParameter(Shader.GetReference(), BufferIndex, BaseIndex, NumBytes, NewValue);
	}

	FORCEINLINE_DEBUGGABLE void SetShaderTexture(FRHIComputeShader* Shader, uint32 TextureIndex, FRHITexture* Texture)
	{
		ValidateBoundShader(Shader);
		if (Bypass())
		{
			GetComputeContext().RHISetShaderTexture(Shader, TextureIndex, Texture);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetShaderTexture<FRHIComputeShader>)(Shader, TextureIndex, Texture);
	}

	FORCEINLINE_DEBUGGABLE void SetShaderResourceViewParameter(FRHIComputeShader* Shader, uint32 SamplerIndex, FRHIShaderResourceView* SRV)
	{
		ValidateBoundShader(Shader);
		if (Bypass())
		{
			GetComputeContext().RHISetShaderResourceViewParameter(Shader, SamplerIndex, SRV);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetShaderResourceViewParameter<FRHIComputeShader>)(Shader, SamplerIndex, SRV);
	}

	FORCEINLINE_DEBUGGABLE void SetShaderSampler(FRHIComputeShader* Shader, uint32 SamplerIndex, FRHISamplerState* State)
	{
		// Immutable samplers can't be set dynamically
		check(!State->IsImmutable());
		if (State->IsImmutable())
		{
			return;
		}

		if (Bypass())
		{
			GetComputeContext().RHISetShaderSampler(Shader, SamplerIndex, State);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetShaderSampler<FRHIComputeShader>)(Shader, SamplerIndex, State);
	}

	FORCEINLINE_DEBUGGABLE void SetUAVParameter(FRHIComputeShader* Shader, uint32 UAVIndex, FRHIUnorderedAccessView* UAV)
	{
		ValidateBoundShader(Shader);
		if (Bypass())
		{
			GetComputeContext().RHISetUAVParameter(Shader, UAVIndex, UAV);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetUAVParameter<FRHIComputeShader>)(Shader, UAVIndex, UAV);
	}

	FORCEINLINE_DEBUGGABLE void SetUAVParameter(FRHIComputeShader* Shader, uint32 UAVIndex, FRHIUnorderedAccessView* UAV, uint32 InitialCount)
	{
		ValidateBoundShader(Shader);
		if (Bypass())
		{
			GetComputeContext().RHISetUAVParameter(Shader, UAVIndex, UAV, InitialCount);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetUAVParameter_InitialCount)(Shader, UAVIndex, UAV, InitialCount);
	}

	FORCEINLINE_DEBUGGABLE void SetComputeShader(FRHIComputeShader* ComputeShader)
	{
		BoundComputeShaderRHI = ComputeShader;
		ComputeShader->UpdateStats();
		if (Bypass())
		{
			GetComputeContext().RHISetComputeShader(ComputeShader);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetComputeShader)(ComputeShader);
	}

	FORCEINLINE_DEBUGGABLE void SetComputePipelineState(FComputePipelineState* ComputePipelineState, FRHIComputeShader* ComputeShader)
	{
		BoundComputeShaderRHI = ComputeShader;
		if (Bypass())
		{
			extern RHI_API FRHIComputePipelineState* ExecuteSetComputePipelineState(FComputePipelineState* ComputePipelineState);
			FRHIComputePipelineState* RHIComputePipelineState = ExecuteSetComputePipelineState(ComputePipelineState);
			GetComputeContext().RHISetComputePipelineState(RHIComputePipelineState);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetComputePipelineState)(ComputePipelineState);
	}

	FORCEINLINE_DEBUGGABLE void SetAsyncComputeBudget(EAsyncComputeBudget Budget)
	{
		if (Bypass())
		{
			GetComputeContext().RHISetAsyncComputeBudget(Budget);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetAsyncComputeBudget)(Budget);
	}

	FORCEINLINE_DEBUGGABLE void DispatchComputeShader(uint32 ThreadGroupCountX, uint32 ThreadGroupCountY, uint32 ThreadGroupCountZ)
	{
		if (Bypass())
		{
			GetComputeContext().RHIDispatchComputeShader(ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
			return;
		}
		ALLOC_COMMAND(FRHICommandDispatchComputeShader)(ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
	}

	FORCEINLINE_DEBUGGABLE void DispatchIndirectComputeShader(FRHIVertexBuffer* ArgumentBuffer, uint32 ArgumentOffset)
	{
		if (Bypass())
		{
			GetComputeContext().RHIDispatchIndirectComputeShader(ArgumentBuffer, ArgumentOffset);
			return;
		}
		ALLOC_COMMAND(FRHICommandDispatchIndirectComputeShader)(ArgumentBuffer, ArgumentOffset);
	}

	FORCEINLINE_DEBUGGABLE void ClearUAVFloat(FRHIUnorderedAccessView* UnorderedAccessViewRHI, const FVector4& Values)
	{
		if (Bypass())
		{
			GetComputeContext().RHIClearUAVFloat(UnorderedAccessViewRHI, Values);
			return;
		}
		ALLOC_COMMAND(FRHICommandClearUAVFloat)(UnorderedAccessViewRHI, Values);
	}

	FORCEINLINE_DEBUGGABLE void ClearUAVUint(FRHIUnorderedAccessView* UnorderedAccessViewRHI, const FUintVector4& Values)
	{
		if (Bypass())
		{
			GetComputeContext().RHIClearUAVUint(UnorderedAccessViewRHI, Values);
			return;
		}
		ALLOC_COMMAND(FRHICommandClearUAVUint)(UnorderedAccessViewRHI, Values);
	}

	FORCEINLINE_DEBUGGABLE void BeginTransitions(TArrayView<const FRHITransition*> Transitions)
	{
		if (Bypass())
		{
			GetComputeContext().RHIBeginTransitions(Transitions);

			for (const FRHITransition* Transition : Transitions)
			{
				Transition->MarkBegin(GetPipeline());
			}
		}
		else
		{
			// Copy the transition array into the command list
			FRHITransition** DstTransitionArray = (FRHITransition**)Alloc(sizeof(FRHITransition*) * Transitions.Num(), alignof(FRHITransition*));
			FMemory::Memcpy(DstTransitionArray, Transitions.GetData(), sizeof(FRHITransition*) * Transitions.Num());

			ALLOC_COMMAND(FRHICommandBeginTransitions)(MakeArrayView((const FRHITransition**)DstTransitionArray, Transitions.Num()));
		}
	}

	FORCEINLINE_DEBUGGABLE void EndTransitions(TArrayView<const FRHITransition*> Transitions)
	{
		if (Bypass())
		{
			GetComputeContext().RHIEndTransitions(Transitions);

			for (const FRHITransition* Transition : Transitions)
			{
				Transition->MarkEnd(GetPipeline());
			}
		}
		else
		{
			// Copy the transition array into the command list
			FRHITransition** DstTransitionArray = (FRHITransition**)Alloc(sizeof(FRHITransition*) * Transitions.Num(), alignof(FRHITransition*));
			FMemory::Memcpy(DstTransitionArray, Transitions.GetData(), sizeof(FRHITransition*) * Transitions.Num());

			ALLOC_COMMAND(FRHICommandEndTransitions)(MakeArrayView((const FRHITransition**)DstTransitionArray, Transitions.Num()));
		}
	}

	inline void Transition(TArrayView<const FRHITransitionInfo> Infos)
	{
		ERHIPipeline Pipeline = GetPipeline();

		if (Bypass())
		{
			// Stack allocate the transition
			FMemStack& MemStack = FMemStack::Get();
			FMemMark Mark(MemStack);
			FRHITransition* Transition = new (MemStack.Alloc(FRHITransition::GetTotalAllocationSize(), FRHITransition::GetAlignment())) FRHITransition(Pipeline, Pipeline);
			GDynamicRHI->RHICreateTransition(Transition, Pipeline, Pipeline, ERHICreateTransitionFlags::NoSplit, Infos);

			GetComputeContext().RHIBeginTransitions(MakeArrayView((const FRHITransition**)&Transition, 1));
			GetComputeContext().RHIEndTransitions(MakeArrayView((const FRHITransition**)&Transition, 1));

			// Manual release
			GDynamicRHI->RHIReleaseTransition(Transition);
			Transition->~FRHITransition();
		}
		else
		{
			// Allocate the transition in the command list
			FRHITransition* Transition = new (Alloc(FRHITransition::GetTotalAllocationSize(), FRHITransition::GetAlignment())) FRHITransition(Pipeline, Pipeline);
			GDynamicRHI->RHICreateTransition(Transition, Pipeline, Pipeline, ERHICreateTransitionFlags::NoSplit, Infos);

			ALLOC_COMMAND(FRHICommandResourceTransition)(Transition);
		}
	}

	FORCEINLINE_DEBUGGABLE void BeginTransition(const FRHITransition* Transition)
	{
		BeginTransitions(MakeArrayView(&Transition, 1));
	}

	FORCEINLINE_DEBUGGABLE void EndTransition(const FRHITransition* Transition)
	{
		EndTransitions(MakeArrayView(&Transition, 1));
	}

	FORCEINLINE_DEBUGGABLE void Transition(const FRHITransitionInfo& Info)
	{
		Transition(MakeArrayView(&Info, 1));
	}

	/* LEGACY API */

	FORCEINLINE_DEBUGGABLE void TransitionResource(ERHIAccess TransitionType, const FTextureRHIRef& InTexture)
	{
		Transition(FRHITransitionInfo(InTexture.GetReference(), ERHIAccess::Unknown, TransitionType));
	}

	FORCEINLINE_DEBUGGABLE void TransitionResource(ERHIAccess TransitionType, FRHITexture* InTexture)
	{
		Transition(FRHITransitionInfo(InTexture, ERHIAccess::Unknown, TransitionType));
	}

	inline void TransitionResources(ERHIAccess TransitionType, FRHITexture* const* InTextures, int32 NumTextures)
	{
		// Stack allocate the transition descriptors. These will get memcpy()ed onto the RHI command list if required.
		FMemMark Mark(FMemStack::Get());
		TArray<FRHITransitionInfo, TMemStackAllocator<>> Infos;
		Infos.Reserve(NumTextures);

		for (int32 Index = 0; Index < NumTextures; ++Index)
		{
			Infos.Add(FRHITransitionInfo(InTextures[Index], ERHIAccess::Unknown, TransitionType));
		}

		Transition(Infos);
	}

	FORCEINLINE_DEBUGGABLE void TransitionResourceArrayNoCopy(ERHIAccess TransitionType, TArray<FRHITexture*>& InTextures)
	{
		TransitionResources(TransitionType, &InTextures[0], InTextures.Num());
	}

	inline void TransitionResources(ERHIAccess TransitionType, EResourceTransitionPipeline /* ignored TransitionPipeline */, FRHIUnorderedAccessView* const* InUAVs, int32 NumUAVs, FRHIComputeFence* WriteFence)
	{
		// Stack allocate the transition descriptors. These will get memcpy()ed onto the RHI command list if required.
		FMemMark Mark(FMemStack::Get());
		TArray<FRHITransitionInfo, TMemStackAllocator<>> Infos;
		Infos.Reserve(NumUAVs);

		for (int32 Index = 0; Index < NumUAVs; ++Index)
		{
			Infos.Add(FRHITransitionInfo(InUAVs[Index], ERHIAccess::Unknown, TransitionType));
		}

		if (WriteFence)
		{
			ERHIPipeline SrcPipeline = IsAsyncCompute() ? ERHIPipeline::AsyncCompute : ERHIPipeline::Graphics;
			ERHIPipeline DstPipeline = IsAsyncCompute() ? ERHIPipeline::Graphics : ERHIPipeline::AsyncCompute;

			// Cross-pipeline transition. Begin on the current context and store the
			// transition in the "compute fence" so we can end it later on the other context.
			WriteFence->Transition = RHICreateTransition(SrcPipeline, DstPipeline, ERHICreateTransitionFlags::None, Infos);
			BeginTransitions(MakeArrayView(&WriteFence->Transition, 1));
		}
		else
		{
			// No compute fence, so this transition is happening entirely on the current pipe.
			Transition(Infos);
		}
	}

	FORCEINLINE_DEBUGGABLE void TransitionResource(ERHIAccess TransitionType, EResourceTransitionPipeline TransitionPipeline, FRHIUnorderedAccessView* InUAV, FRHIComputeFence* WriteFence)
	{
		TransitionResources(TransitionType, TransitionPipeline, &InUAV, 1, WriteFence);
	}

	FORCEINLINE_DEBUGGABLE void TransitionResource(ERHIAccess TransitionType, EResourceTransitionPipeline TransitionPipeline, FRHIUnorderedAccessView* InUAV)
	{
		TransitionResource(TransitionType, TransitionPipeline, InUAV, nullptr);
	}

	FORCEINLINE_DEBUGGABLE void TransitionResources(ERHIAccess TransitionType, EResourceTransitionPipeline TransitionPipeline, FRHIUnorderedAccessView* const* InUAVs, int32 NumUAVs)
	{
		TransitionResources(TransitionType, TransitionPipeline, InUAVs, NumUAVs, nullptr);
	}

	FORCEINLINE_DEBUGGABLE void WaitComputeFence(FRHIComputeFence* WaitFence)
	{
		check(WaitFence->Transition);
		EndTransitions(MakeArrayView(&WaitFence->Transition, 1));
		WaitFence->Transition = nullptr;
	}

	FORCEINLINE_DEBUGGABLE void BeginUAVOverlap()
	{
		if (Bypass())
		{
			GetContext().RHIBeginUAVOverlap();
			return;
		}
		ALLOC_COMMAND(FRHICommandBeginUAVOverlap)();
	}

	FORCEINLINE_DEBUGGABLE void EndUAVOverlap()
	{
		if (Bypass())
		{
			GetContext().RHIEndUAVOverlap();
			return;
		}
		ALLOC_COMMAND(FRHICommandEndUAVOverlap)();
	}

	FORCEINLINE_DEBUGGABLE void BeginUAVOverlap(FRHIUnorderedAccessView* UAV)
	{
		FRHIUnorderedAccessView* UAVs[1] = { UAV };
		BeginUAVOverlap(MakeArrayView(UAVs, 1));
	}

	FORCEINLINE_DEBUGGABLE void EndUAVOverlap(FRHIUnorderedAccessView* UAV)
	{
		FRHIUnorderedAccessView* UAVs[1] = { UAV };
		EndUAVOverlap(MakeArrayView(UAVs, 1));
	}

	FORCEINLINE_DEBUGGABLE void BeginUAVOverlap(TArrayView<FRHIUnorderedAccessView* const> UAVs)
	{
		if (Bypass())
		{
			GetContext().RHIBeginUAVOverlap(UAVs);
			return;
		}

		const uint32 AllocSize = UAVs.Num() * sizeof(FRHIUnorderedAccessView*);
		FRHIUnorderedAccessView** InlineUAVs = (FRHIUnorderedAccessView**)Alloc(AllocSize, alignof(FRHIUnorderedAccessView*));
		FMemory::Memcpy(InlineUAVs, UAVs.GetData(), AllocSize);
		ALLOC_COMMAND(FRHICommandBeginSpecificUAVOverlap)(MakeArrayView(InlineUAVs, UAVs.Num()));
	}

	FORCEINLINE_DEBUGGABLE void EndUAVOverlap(TArrayView<FRHIUnorderedAccessView* const> UAVs)
	{
		if (Bypass())
		{
			GetContext().RHIEndUAVOverlap(UAVs);
			return;
		}

		const uint32 AllocSize = UAVs.Num() * sizeof(FRHIUnorderedAccessView*);
		FRHIUnorderedAccessView** InlineUAVs = (FRHIUnorderedAccessView**)Alloc(AllocSize, alignof(FRHIUnorderedAccessView*));
		FMemory::Memcpy(InlineUAVs, UAVs.GetData(), AllocSize);
		ALLOC_COMMAND(FRHICommandEndSpecificUAVOverlap)(MakeArrayView(InlineUAVs, UAVs.Num()));
	}

	FORCEINLINE_DEBUGGABLE void PushEvent(const TCHAR* Name, FColor Color)
	{
		if (Bypass())
		{
			GetComputeContext().RHIPushEvent(Name, Color);
			return;
		}
		TCHAR* NameCopy = AllocString(Name);
		ALLOC_COMMAND(FRHICommandPushEvent)(NameCopy, Color);
	}

	FORCEINLINE_DEBUGGABLE void PopEvent()
	{
		if (Bypass())
		{
			GetComputeContext().RHIPopEvent();
			return;
		}
		ALLOC_COMMAND(FRHICommandPopEvent)();
	}

	FORCEINLINE_DEBUGGABLE void BreakPoint()
	{
#if !UE_BUILD_SHIPPING
		if (Bypass())
		{
			if (FPlatformMisc::IsDebuggerPresent())
			{
				UE_DEBUG_BREAK();
			}
			return;
		}
		ALLOC_COMMAND(FRHICommandDebugBreak)();
#endif
	}

	FORCEINLINE_DEBUGGABLE void SubmitCommandsHint()
	{
		if (Bypass())
		{
			GetComputeContext().RHISubmitCommandsHint();
			return;
		}
		ALLOC_COMMAND(FRHICommandSubmitCommandsHint)();
	}

	FORCEINLINE_DEBUGGABLE void CopyToStagingBuffer(FRHIVertexBuffer* SourceBuffer, FRHIStagingBuffer* DestinationStagingBuffer, uint32 Offset, uint32 NumBytes)
	{
		if (Bypass())
		{
			GetComputeContext().RHICopyToStagingBuffer(SourceBuffer, DestinationStagingBuffer, Offset, NumBytes);
			return;
		}
		ALLOC_COMMAND(FRHICommandCopyToStagingBuffer)(SourceBuffer, DestinationStagingBuffer, Offset, NumBytes);
	}

	FORCEINLINE_DEBUGGABLE void WriteGPUFence(FRHIGPUFence* Fence)
	{
		if (Bypass())
		{
			GetComputeContext().RHIWriteGPUFence(Fence);
			return;
		}
		ALLOC_COMMAND(FRHICommandWriteGPUFence)(Fence);
	}

	FORCEINLINE_DEBUGGABLE void SetGPUMask(FRHIGPUMask InGPUMask)
	{
		if (GPUMask != InGPUMask)
		{
			GPUMask = InGPUMask;
#if WITH_MGPU
			if (!HasCommands())
			{
				// Update even in Bypass mode to make sure it has the correct value after a toggle.
				InitialGPUMask = GPUMask;
				if (Bypass())
				{
					GetComputeContext().RHISetGPUMask(GPUMask);
					return;
				}
			}
			else
			{
				checkSlow(!Bypass());
				ALLOC_COMMAND(FRHICommandSetGPUMask)(GPUMask);
			}
#endif // WITH_MGPU
		}
	}

	FORCEINLINE_DEBUGGABLE void TransferTextures(const TArrayView<const FTransferTextureParams> Params)
	{
#if WITH_MGPU
		if (Bypass())
		{
			GetComputeContext().RHITransferTextures(Params);
		}
		else
		{
			ALLOC_COMMAND(FRHICommandTransferTextures)(Params);
		}
#endif // WITH_MGPU
	}

#if PLATFORM_REQUIRES_UAV_TO_RTV_TEXTURE_CACHE_FLUSH_WORKAROUND
	FORCEINLINE_DEBUGGABLE void RHIFlushTextureCacheBOP(FRHITexture* Texture)
	{
		if (Bypass())
		{
			GetContext().RHIFlushTextureCacheBOP(Texture);
			return;
		}
		ALLOC_COMMAND(FRHICommandFlushTextureCacheBOP)(Texture);
	}
#endif // #if PLATFORM_REQUIRES_UAV_TO_RTV_TEXTURE_CACHE_FLUSH_WORKAROUND

#if RHI_RAYTRACING
	FORCEINLINE_DEBUGGABLE void BuildAccelerationStructure(FRHIRayTracingGeometry* Geometry)
	{
		FAccelerationStructureBuildParams Params;
		Params.Geometry = Geometry;
		Params.BuildMode = EAccelerationStructureBuildMode::Build;
		BuildAccelerationStructures(MakeArrayView(&Params, 1));
	}

	FORCEINLINE_DEBUGGABLE void BuildAccelerationStructures(const TArrayView<const FAccelerationStructureBuildParams> Params)
	{
		if (Bypass())
		{
			GetComputeContext().RHIBuildAccelerationStructures(Params);
		}
		else
		{
			// Copy the params themselves as well their segment lists, if there are any.
			// AllocArray() can't be used here directly, as we have to modify the params after copy.
			size_t DataSize = sizeof(FAccelerationStructureBuildParams) * Params.Num();
			FAccelerationStructureBuildParams* InlineParams = (FAccelerationStructureBuildParams*) Alloc(DataSize, alignof(FAccelerationStructureBuildParams));
			FMemory::Memcpy(InlineParams, Params.GetData(), DataSize);
			for (int32 i=0; i<Params.Num(); ++i)
			{
				if (Params[i].Segments.Num())
				{
					InlineParams[i].Segments = AllocArray(Params[i].Segments);
				}
			}
			ALLOC_COMMAND(FRHICommandBuildAccelerationStructures)(MakeArrayView(InlineParams, Params.Num()));
		}
	}

	FORCEINLINE_DEBUGGABLE void BuildAccelerationStructure(FRHIRayTracingScene* Scene)
	{
		if (Bypass())
		{
			GetComputeContext().RHIBuildAccelerationStructure(Scene);
		}
		else
		{
			ALLOC_COMMAND(FRHICommandBuildAccelerationStructure)(Scene);
		}
	}
#endif

	FORCEINLINE_DEBUGGABLE void PostExternalCommandsReset()
	{
		if (Bypass())
		{
			GetContext().RHIPostExternalCommandsReset();
			return;
		}
		ALLOC_COMMAND(FRHICommandPostExternalCommandsReset)();
	}
};

class RHI_API FRHICommandList : public FRHIComputeCommandList
{
public:
	FRHICommandList(FRHIGPUMask GPUMask) : FRHIComputeCommandList(GPUMask) {}

	bool AsyncPSOCompileAllowed() const
	{
		return bAsyncPSOCompileAllowed;
	}

	/** Custom new/delete with recycling */
	void* operator new(size_t Size);
	void operator delete(void *RawMemory);
	
	inline FRHIVertexShader* GetBoundVertexShader() const { return BoundShaderInput.VertexShaderRHI; }
	inline FRHIHullShader* GetBoundHullShader() const { return BoundShaderInput.HullShaderRHI; }
	inline FRHIDomainShader* GetBoundDomainShader() const { return BoundShaderInput.DomainShaderRHI; }
	inline FRHIPixelShader* GetBoundPixelShader() const { return BoundShaderInput.PixelShaderRHI; }
	inline FRHIGeometryShader* GetBoundGeometryShader() const { return BoundShaderInput.GeometryShaderRHI; }

	FORCEINLINE_DEBUGGABLE void BeginUpdateMultiFrameResource(FRHITexture* Texture)
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHIBeginUpdateMultiFrameResource( Texture);
			return;
		}
		ALLOC_COMMAND(FRHICommandBeginUpdateMultiFrameResource)(Texture);
	}

	FORCEINLINE_DEBUGGABLE void EndUpdateMultiFrameResource(FRHITexture* Texture)
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHIEndUpdateMultiFrameResource(Texture);
			return;
		}
		ALLOC_COMMAND(FRHICommandEndUpdateMultiFrameResource)(Texture);
	}

	FORCEINLINE_DEBUGGABLE void BeginUpdateMultiFrameResource(FRHIUnorderedAccessView* UAV)
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHIBeginUpdateMultiFrameResource(UAV);
			return;
		}
		ALLOC_COMMAND(FRHICommandBeginUpdateMultiFrameUAV)(UAV);
	}

	FORCEINLINE_DEBUGGABLE void EndUpdateMultiFrameResource(FRHIUnorderedAccessView* UAV)
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHIEndUpdateMultiFrameResource(UAV);
			return;
		}
		ALLOC_COMMAND(FRHICommandEndUpdateMultiFrameUAV)(UAV);
	}

#if WITH_MGPU
	FORCEINLINE_DEBUGGABLE void WaitForTemporalEffect(const FName& EffectName)
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHIWaitForTemporalEffect(EffectName);
			return;
		}
		ALLOC_COMMAND(FRHICommandWaitForTemporalEffect)(EffectName);
	}

	FORCEINLINE_DEBUGGABLE void BroadcastTemporalEffect(const FName& EffectName, const TArrayView<FRHITexture*> Textures)
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHIBroadcastTemporalEffect(EffectName, Textures);
			return;
		}

		ALLOC_COMMAND(FRHICommandBroadcastTemporalEffect<FRHITexture>)(EffectName, AllocArray(Textures));
	}

	FORCEINLINE_DEBUGGABLE void BroadcastTemporalEffect(const FName& EffectName, const TArrayView<FRHIVertexBuffer*> Buffers)
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHIBroadcastTemporalEffect(EffectName, Buffers);
			return;
		}

		ALLOC_COMMAND(FRHICommandBroadcastTemporalEffect<FRHIVertexBuffer>)(EffectName, AllocArray(Buffers));
	}
#endif // WITH_MGPU

	FORCEINLINE_DEBUGGABLE FLocalUniformBuffer BuildLocalUniformBuffer(const void* Contents, uint32 ContentsSize, const FRHIUniformBufferLayout& Layout)
	{
		//check(IsOutsideRenderPass());
		FLocalUniformBuffer Result;
		if (Bypass())
		{
			Result.BypassUniform = RHICreateUniformBuffer(Contents, Layout, UniformBuffer_SingleFrame);
		}
		else
		{
			check(Contents && ContentsSize && (&Layout != nullptr));
			auto* Cmd = ALLOC_COMMAND(FRHICommandBuildLocalUniformBuffer)(this, Contents, ContentsSize, Layout);
			Result.WorkArea = &Cmd->WorkArea;
		}
		return Result;
	}

	template <typename TRHIShader>
	FORCEINLINE_DEBUGGABLE void SetLocalShaderUniformBuffer(TRHIShader* Shader, uint32 BaseIndex, const FLocalUniformBuffer& UniformBuffer)
	{
		//check(IsOutsideRenderPass());
		ValidateBoundShader(Shader);
		if (Bypass())
		{
			GetContext().RHISetShaderUniformBuffer(Shader, BaseIndex, UniformBuffer.BypassUniform);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetLocalUniformBuffer<TRHIShader>)(this, Shader, BaseIndex, UniformBuffer);
	}

	template <typename TShaderRHI>
	FORCEINLINE_DEBUGGABLE void SetLocalShaderUniformBuffer(const TRefCountPtr<TShaderRHI>& Shader, uint32 BaseIndex, const FLocalUniformBuffer& UniformBuffer)
	{
		SetLocalShaderUniformBuffer(Shader.GetReference(), BaseIndex, UniformBuffer);
	}

	using FRHIComputeCommandList::SetShaderUniformBuffer;

	FORCEINLINE_DEBUGGABLE void SetShaderUniformBuffer(FRHIGraphicsShader* Shader, uint32 BaseIndex, FRHIUniformBuffer* UniformBuffer)
	{
		//check(IsOutsideRenderPass());
		ValidateBoundShader(Shader);
		if (Bypass())
		{
			GetContext().RHISetShaderUniformBuffer(Shader, BaseIndex, UniformBuffer);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetShaderUniformBuffer<FRHIGraphicsShader>)(Shader, BaseIndex, UniformBuffer);
	}

	template <typename TShaderRHI>
	FORCEINLINE void SetShaderUniformBuffer(const TRefCountPtr<TShaderRHI>& Shader, uint32 BaseIndex, FRHIUniformBuffer* UniformBuffer)
	{
		SetShaderUniformBuffer(Shader.GetReference(), BaseIndex, UniformBuffer);
	}

	using FRHIComputeCommandList::SetShaderParameter;

	FORCEINLINE_DEBUGGABLE void SetShaderParameter(FRHIGraphicsShader* Shader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue)
	{
		//check(IsOutsideRenderPass());
		ValidateBoundShader(Shader);
		if (Bypass())
		{
			GetContext().RHISetShaderParameter(Shader, BufferIndex, BaseIndex, NumBytes, NewValue);
			return;
		}
		void* UseValue = Alloc(NumBytes, 16);
		FMemory::Memcpy(UseValue, NewValue, NumBytes);
		ALLOC_COMMAND(FRHICommandSetShaderParameter<FRHIGraphicsShader>)(Shader, BufferIndex, BaseIndex, NumBytes, UseValue);
	}

	template <typename TShaderRHI>
	FORCEINLINE void SetShaderParameter(const TRefCountPtr<TShaderRHI>& Shader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue)
	{
		SetShaderParameter(Shader.GetReference(), BufferIndex, BaseIndex, NumBytes, NewValue);
	}

	using FRHIComputeCommandList::SetShaderTexture;

	FORCEINLINE_DEBUGGABLE void SetShaderTexture(FRHIGraphicsShader* Shader, uint32 TextureIndex, FRHITexture* Texture)
	{
		//check(IsOutsideRenderPass());
		ValidateBoundShader(Shader);
		if (Bypass())
		{
			GetContext().RHISetShaderTexture(Shader, TextureIndex, Texture);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetShaderTexture<FRHIGraphicsShader>)(Shader, TextureIndex, Texture);
	}

	template <typename TShaderRHI>
	FORCEINLINE_DEBUGGABLE void SetShaderTexture(const TRefCountPtr<TShaderRHI>& Shader, uint32 TextureIndex, FRHITexture* Texture)
	{
		SetShaderTexture(Shader.GetReference(), TextureIndex, Texture);
	}

	using FRHIComputeCommandList::SetShaderResourceViewParameter;

	FORCEINLINE_DEBUGGABLE void SetShaderResourceViewParameter(FRHIGraphicsShader* Shader, uint32 SamplerIndex, FRHIShaderResourceView* SRV)
	{
		//check(IsOutsideRenderPass());
		ValidateBoundShader(Shader);
		if (Bypass())
		{
			GetContext().RHISetShaderResourceViewParameter(Shader, SamplerIndex, SRV);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetShaderResourceViewParameter<FRHIGraphicsShader>)(Shader, SamplerIndex, SRV);
	}

	template <typename TShaderRHI>
	FORCEINLINE_DEBUGGABLE void SetShaderResourceViewParameter(const TRefCountPtr<TShaderRHI>& Shader, uint32 SamplerIndex, FRHIShaderResourceView* SRV)
	{
		SetShaderResourceViewParameter(Shader.GetReference(), SamplerIndex, SRV);
	}

	using FRHIComputeCommandList::SetShaderSampler;

	FORCEINLINE_DEBUGGABLE void SetShaderSampler(FRHIGraphicsShader* Shader, uint32 SamplerIndex, FRHISamplerState* State)
	{
		//check(IsOutsideRenderPass());
		ValidateBoundShader(Shader);
		
		// Immutable samplers can't be set dynamically
		check(!State->IsImmutable());
		if (State->IsImmutable())
		{
			return;
		}

		if (Bypass())
		{
			GetContext().RHISetShaderSampler(Shader, SamplerIndex, State);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetShaderSampler<FRHIGraphicsShader>)(Shader, SamplerIndex, State);
	}

	template <typename TShaderRHI>
	FORCEINLINE_DEBUGGABLE void SetShaderSampler(const TRefCountPtr<TShaderRHI>& Shader, uint32 SamplerIndex, FRHISamplerState* State)
	{
		SetShaderSampler(Shader.GetReference(), SamplerIndex, State);
	}

	using FRHIComputeCommandList::SetUAVParameter;

	FORCEINLINE_DEBUGGABLE void SetUAVParameter(FRHIPixelShader* Shader, uint32 UAVIndex, FRHIUnorderedAccessView* UAV)
	{
		ValidateBoundShader(Shader);
		if (Bypass())
		{
			GetContext().RHISetUAVParameter(Shader, UAVIndex, UAV);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetUAVParameter<FRHIPixelShader>)(Shader, UAVIndex, UAV);
	}

	FORCEINLINE_DEBUGGABLE void SetUAVParameter(const TRefCountPtr<FRHIPixelShader>& Shader, uint32 UAVIndex, FRHIUnorderedAccessView* UAV)
	{
		SetUAVParameter(Shader.GetReference(), UAVIndex, UAV);
	}

	FORCEINLINE_DEBUGGABLE void SetBlendFactor(const FLinearColor& BlendFactor = FLinearColor::White)
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHISetBlendFactor(BlendFactor);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetBlendFactor)(BlendFactor);
	}

	FORCEINLINE_DEBUGGABLE void DrawPrimitive(uint32 BaseVertexIndex, uint32 NumPrimitives, uint32 NumInstances)
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHIDrawPrimitive(BaseVertexIndex, NumPrimitives, NumInstances);
			return;
		}
		ALLOC_COMMAND(FRHICommandDrawPrimitive)(BaseVertexIndex, NumPrimitives, NumInstances);
	}

	FORCEINLINE_DEBUGGABLE void DrawIndexedPrimitive(FRHIIndexBuffer* IndexBuffer, int32 BaseVertexIndex, uint32 FirstInstance, uint32 NumVertices, uint32 StartIndex, uint32 NumPrimitives, uint32 NumInstances)
	{
		if (!IndexBuffer)
		{
			UE_LOG(LogRHI, Fatal, TEXT("Tried to call DrawIndexedPrimitive with null IndexBuffer!"));
		}

		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHIDrawIndexedPrimitive(IndexBuffer, BaseVertexIndex, FirstInstance, NumVertices, StartIndex, NumPrimitives, NumInstances);
			return;
		}
		ALLOC_COMMAND(FRHICommandDrawIndexedPrimitive)(IndexBuffer, BaseVertexIndex, FirstInstance, NumVertices, StartIndex, NumPrimitives, NumInstances);
	}

	FORCEINLINE_DEBUGGABLE void SetStreamSource(uint32 StreamIndex, FRHIVertexBuffer* VertexBuffer, uint32 Offset)
	{
		if (Bypass())
		{
			GetContext().RHISetStreamSource(StreamIndex, VertexBuffer, Offset);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetStreamSource)(StreamIndex, VertexBuffer, Offset);
	}

	FORCEINLINE_DEBUGGABLE void SetStencilRef(uint32 StencilRef)
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHISetStencilRef(StencilRef);
			return;
		}

		ALLOC_COMMAND(FRHICommandSetStencilRef)(StencilRef);
	}

	FORCEINLINE_DEBUGGABLE void SetViewport(float MinX, float MinY, float MinZ, float MaxX, float MaxY, float MaxZ)
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHISetViewport(MinX, MinY, MinZ, MaxX, MaxY, MaxZ);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetViewport)(MinX, MinY, MinZ, MaxX, MaxY, MaxZ);
	}

	FORCEINLINE_DEBUGGABLE void SetStereoViewport(float LeftMinX, float RightMinX, float LeftMinY, float RightMinY, float MinZ, float LeftMaxX, float RightMaxX, float LeftMaxY, float RightMaxY, float MaxZ)
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHISetStereoViewport(LeftMinX, RightMinX, LeftMinY, RightMinY, MinZ, LeftMaxX, RightMaxX, LeftMaxY, RightMaxY, MaxZ);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetStereoViewport)(LeftMinX, RightMinX, LeftMinY, RightMinY, MinZ, LeftMaxX, RightMaxX, LeftMaxY, RightMaxY, MaxZ);
	}

	FORCEINLINE_DEBUGGABLE void SetScissorRect(bool bEnable, uint32 MinX, uint32 MinY, uint32 MaxX, uint32 MaxY)
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHISetScissorRect(bEnable, MinX, MinY, MaxX, MaxY);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetScissorRect)(bEnable, MinX, MinY, MaxX, MaxY);
	}

	void ApplyCachedRenderTargets(
		FGraphicsPipelineStateInitializer& GraphicsPSOInit
		)
	{
		GraphicsPSOInit.RenderTargetsEnabled = PSOContext.CachedNumSimultanousRenderTargets;

		for (uint32 i = 0; i < GraphicsPSOInit.RenderTargetsEnabled; ++i)
		{
			if (PSOContext.CachedRenderTargets[i].Texture)
			{
				GraphicsPSOInit.RenderTargetFormats[i] = PSOContext.CachedRenderTargets[i].Texture->GetFormat();
				GraphicsPSOInit.RenderTargetFlags[i] = PSOContext.CachedRenderTargets[i].Texture->GetFlags();
				const FRHITexture2DArray* TextureArray = PSOContext.CachedRenderTargets[i].Texture->GetTexture2DArray();
			}
			else
			{
				GraphicsPSOInit.RenderTargetFormats[i] = PF_Unknown;
			}

			if (GraphicsPSOInit.RenderTargetFormats[i] != PF_Unknown)
			{
				GraphicsPSOInit.NumSamples = PSOContext.CachedRenderTargets[i].Texture->GetNumSamples();
			}
		}

		if (PSOContext.CachedDepthStencilTarget.Texture)
		{
			GraphicsPSOInit.DepthStencilTargetFormat = PSOContext.CachedDepthStencilTarget.Texture->GetFormat();
			GraphicsPSOInit.DepthStencilTargetFlag = PSOContext.CachedDepthStencilTarget.Texture->GetFlags();
			const FRHITexture2DArray* TextureArray = PSOContext.CachedDepthStencilTarget.Texture->GetTexture2DArray();
		}
		else
		{
			GraphicsPSOInit.DepthStencilTargetFormat = PF_Unknown;
		}

		GraphicsPSOInit.DepthTargetLoadAction = PSOContext.CachedDepthStencilTarget.DepthLoadAction;
		GraphicsPSOInit.DepthTargetStoreAction = PSOContext.CachedDepthStencilTarget.DepthStoreAction;
		GraphicsPSOInit.StencilTargetLoadAction = PSOContext.CachedDepthStencilTarget.StencilLoadAction;
		GraphicsPSOInit.StencilTargetStoreAction = PSOContext.CachedDepthStencilTarget.GetStencilStoreAction();
		GraphicsPSOInit.DepthStencilAccess = PSOContext.CachedDepthStencilTarget.GetDepthStencilAccess();

		if (GraphicsPSOInit.DepthStencilTargetFormat != PF_Unknown)
		{
			GraphicsPSOInit.NumSamples = PSOContext.CachedDepthStencilTarget.Texture->GetNumSamples();
		}

		GraphicsPSOInit.SubpassHint = PSOContext.SubpassHint;
		GraphicsPSOInit.SubpassIndex = PSOContext.SubpassIndex;
		GraphicsPSOInit.MultiViewCount = PSOContext.MultiViewCount;
		GraphicsPSOInit.bHasFragmentDensityAttachment = PSOContext.HasFragmentDensityAttachment;
	}

	FORCEINLINE_DEBUGGABLE void SetGraphicsPipelineState(class FGraphicsPipelineState* GraphicsPipelineState, const FBoundShaderStateInput& ShaderInput, bool bApplyAdditionalState)
	{
		//check(IsOutsideRenderPass());
		BoundShaderInput = ShaderInput;
		if (Bypass())
		{
			extern RHI_API FRHIGraphicsPipelineState* ExecuteSetGraphicsPipelineState(class FGraphicsPipelineState* GraphicsPipelineState);
			FRHIGraphicsPipelineState* RHIGraphicsPipelineState = ExecuteSetGraphicsPipelineState(GraphicsPipelineState);
			GetContext().RHISetGraphicsPipelineState(RHIGraphicsPipelineState, bApplyAdditionalState);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetGraphicsPipelineState)(GraphicsPipelineState, bApplyAdditionalState);
	}

	FORCEINLINE_DEBUGGABLE void DrawPrimitiveIndirect(FRHIVertexBuffer* ArgumentBuffer, uint32 ArgumentOffset)
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHIDrawPrimitiveIndirect(ArgumentBuffer, ArgumentOffset);
			return;
		}
		ALLOC_COMMAND(FRHICommandDrawPrimitiveIndirect)(ArgumentBuffer, ArgumentOffset);
	}

	FORCEINLINE_DEBUGGABLE void DrawIndexedIndirect(FRHIIndexBuffer* IndexBufferRHI, FRHIStructuredBuffer* ArgumentsBufferRHI, uint32 DrawArgumentsIndex, uint32 NumInstances)
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHIDrawIndexedIndirect(IndexBufferRHI, ArgumentsBufferRHI, DrawArgumentsIndex, NumInstances);
			return;
		}
		ALLOC_COMMAND(FRHICommandDrawIndexedIndirect)(IndexBufferRHI, ArgumentsBufferRHI, DrawArgumentsIndex, NumInstances);
	}

	FORCEINLINE_DEBUGGABLE void DrawIndexedPrimitiveIndirect(FRHIIndexBuffer* IndexBuffer, FRHIVertexBuffer* ArgumentsBuffer, uint32 ArgumentOffset)
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHIDrawIndexedPrimitiveIndirect(IndexBuffer, ArgumentsBuffer, ArgumentOffset);
			return;
		}
		ALLOC_COMMAND(FRHICommandDrawIndexedPrimitiveIndirect)(IndexBuffer, ArgumentsBuffer, ArgumentOffset);
	}

	FORCEINLINE_DEBUGGABLE void SetDepthBounds(float MinDepth, float MaxDepth)
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHISetDepthBounds(MinDepth, MaxDepth);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetDepthBounds)(MinDepth, MaxDepth);
	}
	
	FORCEINLINE_DEBUGGABLE void SetShadingRate(EVRSShadingRate ShadingRate, EVRSRateCombiner Combiner)
	{
#if PLATFORM_SUPPORTS_VARIABLE_RATE_SHADING
		if (Bypass())
		{
			GetContext().RHISetShadingRate(ShadingRate, Combiner);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetShadingRate)(ShadingRate, Combiner);
#endif
	}

	UE_DEPRECATED(4.27, "SetShadingRateImage is deprecated. Bind the shading rate image as part of the FRHIRenderPassInfo struct.")
	FORCEINLINE_DEBUGGABLE void SetShadingRateImage(FRHITexture* RateImageTexture, EVRSRateCombiner Combiner)
	{
		check(false);
	}

	FORCEINLINE_DEBUGGABLE void CopyToResolveTarget(FRHITexture* SourceTextureRHI, FRHITexture* DestTextureRHI, const FResolveParams& ResolveParams)
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHICopyToResolveTarget(SourceTextureRHI, DestTextureRHI, ResolveParams);
			return;
		}
		ALLOC_COMMAND(FRHICommandCopyToResolveTarget)(SourceTextureRHI, DestTextureRHI, ResolveParams);
	}

	FORCEINLINE_DEBUGGABLE void CopyTexture(FRHITexture* SourceTextureRHI, FRHITexture* DestTextureRHI, const FRHICopyTextureInfo& CopyInfo)
	{
		check(IsOutsideRenderPass());
		if (GRHISupportsCopyToTextureMultipleMips)
		{
			if (Bypass())
			{
				GetContext().RHICopyTexture(SourceTextureRHI, DestTextureRHI, CopyInfo);
				return;
			}
			ALLOC_COMMAND(FRHICommandCopyTexture)(SourceTextureRHI, DestTextureRHI, CopyInfo);
		}
		else
		{
			FRHICopyTextureInfo PerMipInfo = CopyInfo;
			PerMipInfo.NumMips = 1;
			for (uint32 MipIndex = 0; MipIndex < CopyInfo.NumMips; MipIndex++)
			{
				if (Bypass())
				{
					GetContext().RHICopyTexture(SourceTextureRHI, DestTextureRHI, PerMipInfo);
				}
				else
				{
					ALLOC_COMMAND(FRHICommandCopyTexture)(SourceTextureRHI, DestTextureRHI, PerMipInfo);
				}

				++PerMipInfo.SourceMipIndex;
				++PerMipInfo.DestMipIndex;
				PerMipInfo.Size.X = FMath::Max(1, PerMipInfo.Size.X / 2);
				PerMipInfo.Size.Y = FMath::Max(1, PerMipInfo.Size.Y / 2);
			}
		}
	}

	FORCEINLINE_DEBUGGABLE void ResummarizeHTile(FRHITexture2D* DepthTexture)
	{
		if (Bypass())
		{
			GetContext().RHIResummarizeHTile(DepthTexture);
			return;
		}
		ALLOC_COMMAND(FRHICommandResummarizeHTile)(DepthTexture);
	}

	UE_DEPRECATED(4.25, "RHIClearTinyUAV is deprecated. Use RHIClearUAVUint or RHIClearUAVFloat instead.")
	FORCEINLINE_DEBUGGABLE void ClearTinyUAV(FRHIUnorderedAccessView* UnorderedAccessViewRHI, const uint32(&Values)[4])
	{
		// Forward to the new uint clear implementation.
		ClearUAVUint(UnorderedAccessViewRHI, FUintVector4(Values[0], Values[1], Values[2], Values[3]));
	}

	FORCEINLINE_DEBUGGABLE void BeginRenderQuery(FRHIRenderQuery* RenderQuery)
	{
		if (Bypass())
		{
			GetContext().RHIBeginRenderQuery(RenderQuery);
			return;
		}
		ALLOC_COMMAND(FRHICommandBeginRenderQuery)(RenderQuery);
	}
	FORCEINLINE_DEBUGGABLE void EndRenderQuery(FRHIRenderQuery* RenderQuery)
	{
		if (Bypass())
		{
			GetContext().RHIEndRenderQuery(RenderQuery);
			return;
		}
		ALLOC_COMMAND(FRHICommandEndRenderQuery)(RenderQuery);
	}
	FORCEINLINE_DEBUGGABLE void CalibrateTimers(FRHITimestampCalibrationQuery* CalibrationQuery)
	{
		if (Bypass())
		{
			GetContext().RHICalibrateTimers(CalibrationQuery);
			return;
		}
		ALLOC_COMMAND(FRHICommandCalibrateTimers)(CalibrationQuery);
	}

	FORCEINLINE_DEBUGGABLE void PollOcclusionQueries()
	{
		if (Bypass())
		{
			GetContext().RHIPollOcclusionQueries();
			return;
		}
		ALLOC_COMMAND(FRHICommandPollOcclusionQueries)();
	}

	/* LEGACY API */

	using FRHIComputeCommandList::TransitionResource;
	using FRHIComputeCommandList::TransitionResources;

	FORCEINLINE_DEBUGGABLE void TransitionResource(FExclusiveDepthStencil DepthStencilMode, FRHITexture* DepthTexture)
	{
		check(DepthStencilMode.IsUsingDepth() || DepthStencilMode.IsUsingStencil());

		TArray<FRHITransitionInfo, TInlineAllocator<2>> Infos;

		DepthStencilMode.EnumerateSubresources([&](ERHIAccess NewAccess, uint32 PlaneSlice)
		{
			FRHITransitionInfo Info;
			Info.Type = FRHITransitionInfo::EType::Texture;
			Info.Texture = DepthTexture;
			Info.AccessAfter = NewAccess;
			Info.PlaneSlice = PlaneSlice;
			Infos.Emplace(Info);
		});

		FRHIComputeCommandList::Transition(MakeArrayView(Infos));
	}

	/* LEGACY API */

	FORCEINLINE_DEBUGGABLE void BeginRenderPass(const FRHIRenderPassInfo& InInfo, const TCHAR* Name)
	{
		check(!IsInsideRenderPass());
		check(!IsInsideComputePass());

		if (InInfo.bTooManyUAVs)
		{
			UE_LOG(LogRHI, Warning, TEXT("RenderPass %s has too many UAVs"));
		}
		InInfo.Validate();

		if (Bypass())
		{
			GetContext().RHIBeginRenderPass(InInfo, Name);
		}
		else
		{
			TCHAR* NameCopy  = AllocString(Name);
			ALLOC_COMMAND(FRHICommandBeginRenderPass)(InInfo, NameCopy);
		}
		Data.bInsideRenderPass = true;

		CacheActiveRenderTargets(InInfo);
		ResetSubpass(InInfo.SubpassHint);
		Data.bInsideRenderPass = true;
	}

	void EndRenderPass()
	{
		check(IsInsideRenderPass());
		check(!IsInsideComputePass());
		if (Bypass())
		{
			GetContext().RHIEndRenderPass();
		}
		else
		{
			ALLOC_COMMAND(FRHICommandEndRenderPass)();
		}
		Data.bInsideRenderPass = false;
		ResetSubpass(ESubpassHint::None);
	}

	FORCEINLINE_DEBUGGABLE void NextSubpass()
	{
		check(IsInsideRenderPass());
		if (Bypass())
		{
			GetContext().RHINextSubpass();
		}
		else
		{
			ALLOC_COMMAND(FRHICommandNextSubpass)();
		}
		IncrementSubpass();
	}

	// These 6 are special in that they must be called on the immediate command list and they force a flush only when we are not doing RHI thread
	void BeginScene();
	void EndScene();
	void BeginDrawingViewport(FRHIViewport* Viewport, FRHITexture* RenderTargetRHI);
	void EndDrawingViewport(FRHIViewport* Viewport, bool bPresent, bool bLockToVsync);
	void BeginFrame();
	void EndFrame();

	FORCEINLINE_DEBUGGABLE void RHIInvalidateCachedState()
	{
		if (Bypass())
		{
			GetContext().RHIInvalidateCachedState();
			return;
		}
		ALLOC_COMMAND(FRHICommandInvalidateCachedState)();
	}

	FORCEINLINE void DiscardRenderTargets(bool Depth, bool Stencil, uint32 ColorBitMask)
	{
		if (Bypass())
		{
			GetContext().RHIDiscardRenderTargets(Depth, Stencil, ColorBitMask);
			return;
		}
		ALLOC_COMMAND(FRHICommandDiscardRenderTargets)(Depth, Stencil, ColorBitMask);
	}

#if PLATFORM_USE_BACKBUFFER_WRITE_TRANSITION_TRACKING
	FORCEINLINE_DEBUGGABLE void RHIBackBufferWaitTrackingBeginFrame(uint64 FrameToken, bool bDeferred)
	{
		if (Bypass())
		{
			GetContext().RHIBackBufferWaitTrackingBeginFrame(FrameToken, bDeferred);
			return;
		}
		ALLOC_COMMAND(FRHICommandBackBufferWaitTrackingBeginFrame)(FrameToken, bDeferred);
	}
#endif // #if PLATFORM_USE_BACKBUFFER_WRITE_TRANSITION_TRACKING
	
	FORCEINLINE_DEBUGGABLE void CopyBufferRegion(FRHIVertexBuffer* DestBuffer, uint64 DstOffset, FRHIVertexBuffer* SourceBuffer, uint64 SrcOffset, uint64 NumBytes)
	{
		// No copy/DMA operation inside render passes
		check(IsOutsideRenderPass());

		if (Bypass())
		{
			GetContext().RHICopyBufferRegion(DestBuffer, DstOffset, SourceBuffer, SrcOffset, NumBytes);
		}
		else
		{
			ALLOC_COMMAND(FRHICommandCopyBufferRegion)(DestBuffer, DstOffset, SourceBuffer, SrcOffset, NumBytes);
		}
	}

#if RHI_RAYTRACING
	// Ray tracing API
	UE_DEPRECATED(4.25, "CopyBufferRegions API is deprecated. Use an explicit compute shader copy dispatch instead.")
	FORCEINLINE_DEBUGGABLE void CopyBufferRegions(const TArrayView<const FCopyBufferRegionParams> Params)
	{
		// No copy/DMA operation inside render passes
		check(IsOutsideRenderPass());

		if (Bypass())
		{
			GetContext().RHICopyBufferRegions(Params);
		}
		else
		{
			ALLOC_COMMAND(FRHICommandCopyBufferRegions)(AllocArray(Params));
		}
	}

	FORCEINLINE_DEBUGGABLE void ClearRayTracingBindings(FRHIRayTracingScene* Scene)
	{
		if (Bypass())
		{
			GetContext().RHIClearRayTracingBindings(Scene);
		}
		else
		{
			ALLOC_COMMAND(FRHICommandClearRayTracingBindings)(Scene);
		}
	}

	/**
	 * Trace rays from an input buffer of FBasicRayData.
	 * Binary intersection results are written to output buffer as R32_UINTs.
	 * 0xFFFFFFFF is written if ray intersects any scene triangle, 0 otherwise.
	 */
	FORCEINLINE_DEBUGGABLE void RayTraceOcclusion(FRHIRayTracingScene* Scene,
		FRHIShaderResourceView* Rays,
		FRHIUnorderedAccessView* Output,
		uint32 NumRays)
	{
		if (Bypass())
		{
			GetContext().RHIRayTraceOcclusion(Scene, Rays, Output, NumRays);
		}
		else
		{
			ALLOC_COMMAND(FRHICommandRayTraceOcclusion)(Scene, Rays, Output, NumRays);
		}
	}

	/**
	 * Trace rays from an input buffer of FBasicRayData.
	 * Primitive intersection results are written to output buffer as FIntersectionPayload.
	 */
	FORCEINLINE_DEBUGGABLE void RayTraceIntersection(FRHIRayTracingScene* Scene,
		FRHIShaderResourceView* Rays,
		FRHIUnorderedAccessView* Output,
		uint32 NumRays)
	{
		if (Bypass())
		{
			GetContext().RHIRayTraceIntersection(Scene, Rays, Output, NumRays);
		}
		else
		{
			ALLOC_COMMAND(FRHICommandRayTraceIntersection)(Scene, Rays, Output, NumRays);
		}
	}

	FORCEINLINE_DEBUGGABLE void RayTraceDispatch(FRayTracingPipelineState* Pipeline, FRHIRayTracingShader* RayGenShader, FRHIRayTracingScene* Scene, const FRayTracingShaderBindings& GlobalResourceBindings, uint32 Width, uint32 Height)
	{
		if (Bypass())
		{
			extern RHI_API FRHIRayTracingPipelineState* GetRHIRayTracingPipelineState(FRayTracingPipelineState*);
			GetContext().RHIRayTraceDispatch(GetRHIRayTracingPipelineState(Pipeline), RayGenShader, Scene, GlobalResourceBindings, Width, Height);
		}
		else
		{
			ALLOC_COMMAND(FRHICommandRayTraceDispatch)(Pipeline, RayGenShader, Scene, GlobalResourceBindings, Width, Height);
		}
	}

	FORCEINLINE_DEBUGGABLE void SetRayTracingHitGroups(
		FRHIRayTracingScene* Scene, FRayTracingPipelineState* Pipeline,
		uint32 NumBindings, const FRayTracingLocalShaderBindings* Bindings,
		bool bCopyDataToInlineStorage = true)
	{
		if (Bypass())
		{
			extern RHI_API FRHIRayTracingPipelineState* GetRHIRayTracingPipelineState(FRayTracingPipelineState*);
			GetContext().RHISetRayTracingHitGroups(Scene, GetRHIRayTracingPipelineState(Pipeline), NumBindings, Bindings);
		}
		else
		{
			FRayTracingLocalShaderBindings* InlineBindings = nullptr;

			// By default all batch binding data is stored in the command list memory.
			// However, user may skip this copy if they take responsibility for keeping data alive until this command is executed.
			if (bCopyDataToInlineStorage)
			{
				if (NumBindings)
				{
					uint32 Size = sizeof(FRayTracingLocalShaderBindings) * NumBindings;
					InlineBindings = (FRayTracingLocalShaderBindings*)Alloc(Size, alignof(FRayTracingLocalShaderBindings));
					FMemory::Memcpy(InlineBindings, Bindings, Size);
				}

				for (uint32 i = 0; i < NumBindings; ++i)
				{
					if (InlineBindings[i].NumUniformBuffers)
					{
						InlineBindings[i].UniformBuffers = (FRHIUniformBuffer**)Alloc(sizeof(FRHIUniformBuffer*) * InlineBindings[i].NumUniformBuffers, alignof(FRHIUniformBuffer*));
						for (uint32 Index = 0; Index < InlineBindings[i].NumUniformBuffers; ++Index)
						{
							InlineBindings[i].UniformBuffers[Index] = Bindings[i].UniformBuffers[Index];
						}
					}

					if (InlineBindings[i].LooseParameterDataSize)
					{
						InlineBindings[i].LooseParameterData = (uint8*)Alloc(InlineBindings[i].LooseParameterDataSize, 16);
						FMemory::Memcpy(InlineBindings[i].LooseParameterData, Bindings[i].LooseParameterData, InlineBindings[i].LooseParameterDataSize);
					}
				}

				ALLOC_COMMAND(FRHICommandSetRayTracingBindings)(Scene, Pipeline, NumBindings, InlineBindings);
			}
			else
			{
				ALLOC_COMMAND(FRHICommandSetRayTracingBindings)(Scene, Pipeline, NumBindings, Bindings);
			}
		}
	}

	FORCEINLINE_DEBUGGABLE void SetRayTracingHitGroup(
		FRHIRayTracingScene* Scene, uint32 InstanceIndex, uint32 SegmentIndex, uint32 ShaderSlot,
		FRayTracingPipelineState* Pipeline, uint32 HitGroupIndex,
		uint32 NumUniformBuffers, FRHIUniformBuffer* const* UniformBuffers,
		uint32 LooseParameterDataSize, const void* LooseParameterData,
		uint32 UserData)
	{
		if (Bypass())
		{
			extern RHI_API FRHIRayTracingPipelineState* GetRHIRayTracingPipelineState(FRayTracingPipelineState*);
			GetContext().RHISetRayTracingHitGroup(Scene, InstanceIndex, SegmentIndex, ShaderSlot, GetRHIRayTracingPipelineState(Pipeline), HitGroupIndex, 
				NumUniformBuffers, UniformBuffers,
				LooseParameterDataSize, LooseParameterData,
				UserData);
		}
		else
		{
			FRHIUniformBuffer** InlineUniformBuffers = nullptr;
			if (NumUniformBuffers)
			{
				InlineUniformBuffers = (FRHIUniformBuffer**)Alloc(sizeof(FRHIUniformBuffer*) * NumUniformBuffers, alignof(FRHIUniformBuffer*));
				for (uint32 Index = 0; Index < NumUniformBuffers; ++Index)
				{
					InlineUniformBuffers[Index] = UniformBuffers[Index];
				}
			}

			void* InlineLooseParameterData = nullptr;
			if (LooseParameterDataSize)
			{
				InlineLooseParameterData = Alloc(LooseParameterDataSize, 16);
				FMemory::Memcpy(InlineLooseParameterData, LooseParameterData, LooseParameterDataSize);
			}

			ALLOC_COMMAND(FRHICommandSetRayTracingBindings)(Scene, InstanceIndex, SegmentIndex, ShaderSlot, Pipeline, HitGroupIndex, 
				NumUniformBuffers, InlineUniformBuffers, 
				LooseParameterDataSize, InlineLooseParameterData,
				UserData);
		}
	}

	FORCEINLINE_DEBUGGABLE void SetRayTracingCallableShader(
		FRHIRayTracingScene* Scene, uint32 ShaderSlotInScene,
		FRayTracingPipelineState* Pipeline, uint32 ShaderIndexInPipeline,
		uint32 NumUniformBuffers, FRHIUniformBuffer* const* UniformBuffers,
		uint32 UserData)
	{
		if (Bypass())
		{
			extern RHI_API FRHIRayTracingPipelineState* GetRHIRayTracingPipelineState(FRayTracingPipelineState*);
			GetContext().RHISetRayTracingCallableShader(Scene, ShaderSlotInScene, GetRHIRayTracingPipelineState(Pipeline), ShaderIndexInPipeline, NumUniformBuffers, UniformBuffers, UserData);
		}
		else
		{
			FRHIUniformBuffer** InlineUniformBuffers = nullptr;
			if (NumUniformBuffers)
			{
				InlineUniformBuffers = (FRHIUniformBuffer**)Alloc(sizeof(FRHIUniformBuffer*) * NumUniformBuffers, alignof(FRHIUniformBuffer*));
				for (uint32 Index = 0; Index < NumUniformBuffers; ++Index)
				{
					InlineUniformBuffers[Index] = UniformBuffers[Index];
				}
			}

			ALLOC_COMMAND(FRHICommandSetRayTracingBindings)(Scene, ShaderSlotInScene, Pipeline, ShaderIndexInPipeline, NumUniformBuffers, InlineUniformBuffers, UserData, FRHICommandSetRayTracingBindings::EBindingType::EBindingType_CallableShader);
		}
	}

	FORCEINLINE_DEBUGGABLE void SetRayTracingMissShader(
		FRHIRayTracingScene* Scene, uint32 ShaderSlotInScene,
		FRayTracingPipelineState* Pipeline, uint32 ShaderIndexInPipeline,
		uint32 NumUniformBuffers, FRHIUniformBuffer* const* UniformBuffers,
		uint32 UserData)
	{
		if (Bypass())
		{
			extern RHI_API FRHIRayTracingPipelineState* GetRHIRayTracingPipelineState(FRayTracingPipelineState*);
			GetContext().RHISetRayTracingMissShader(Scene, ShaderSlotInScene, GetRHIRayTracingPipelineState(Pipeline), ShaderIndexInPipeline, NumUniformBuffers, UniformBuffers, UserData);
		}
		else
		{
			FRHIUniformBuffer** InlineUniformBuffers = nullptr;
			if (NumUniformBuffers)
			{
				InlineUniformBuffers = (FRHIUniformBuffer**)Alloc(sizeof(FRHIUniformBuffer*) * NumUniformBuffers, alignof(FRHIUniformBuffer*));
				for (uint32 Index = 0; Index < NumUniformBuffers; ++Index)
				{
					InlineUniformBuffers[Index] = UniformBuffers[Index];
				}
			}

			ALLOC_COMMAND(FRHICommandSetRayTracingBindings)(Scene, ShaderSlotInScene, Pipeline, ShaderIndexInPipeline, NumUniformBuffers, InlineUniformBuffers, UserData, FRHICommandSetRayTracingBindings::EBindingType::EBindingType_MissShader);
		}
	}

#endif // RHI_RAYTRACING
};

namespace EImmediateFlushType
{
	enum Type
	{ 
		WaitForOutstandingTasksOnly = 0, 
		DispatchToRHIThread, 
		WaitForDispatchToRHIThread,
		FlushRHIThread,
		FlushRHIThreadFlushResources,
		FlushRHIThreadFlushResourcesFlushDeferredDeletes
	};
};

class FScopedRHIThreadStaller
{
	class FRHICommandListImmediate* Immed; // non-null if we need to unstall
public:
	FScopedRHIThreadStaller(class FRHICommandListImmediate& InImmed);
	~FScopedRHIThreadStaller();
};


// Forward declare RHI creation function so they can still be called from the deprecated immediate command list resource creation functions
FIndexBufferRHIRef RHICreateIndexBuffer(uint32 Stride, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo);
FVertexBufferRHIRef RHICreateVertexBuffer(uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo);
FStructuredBufferRHIRef RHICreateStructuredBuffer(uint32 Stride, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo);
FTexture2DRHIRef RHICreateTexture2D(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 NumSamples, ETextureCreateFlags Flags, FRHIResourceCreateInfo& CreateInfo);
FTexture2DRHIRef RHICreateTextureExternal2D(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 NumSamples, ETextureCreateFlags Flags, FRHIResourceCreateInfo& CreateInfo);
FTexture2DRHIRef RHIAsyncCreateTexture2D(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, void** InitialMipData, uint32 NumInitialMips);
FTexture2DArrayRHIRef RHICreateTexture2DArray(uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint8 Format, uint32 NumMips, uint32 NumSamples, ETextureCreateFlags Flags, FRHIResourceCreateInfo& CreateInfo);
FTexture3DRHIRef RHICreateTexture3D(uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, FRHIResourceCreateInfo& CreateInfo);
FTextureCubeRHIRef RHICreateTextureCube(uint32 Size, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, FRHIResourceCreateInfo& CreateInfo);
FTextureCubeRHIRef RHICreateTextureCubeArray(uint32 Size, uint32 ArraySize, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, FRHIResourceCreateInfo& CreateInfo);

extern RHI_API ERHIAccess RHIGetDefaultResourceState(ETextureCreateFlags InUsage, bool bInHasInitialData);
extern RHI_API ERHIAccess RHIGetDefaultResourceState(EBufferUsageFlags InUsage, bool bInHasInitialData);

class RHI_API FRHICommandListImmediate : public FRHICommandList
{
	template <typename LAMBDA>
	struct TRHILambdaCommand final : public FRHICommandBase
	{
		LAMBDA Lambda;

		TRHILambdaCommand(LAMBDA&& InLambda)
			: Lambda(Forward<LAMBDA>(InLambda))
		{}

		void ExecuteAndDestruct(FRHICommandListBase& CmdList, FRHICommandListDebugContext&) override final
		{
			TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(TRHILambdaCommand, RHICommandsChannel);
			Lambda(*static_cast<FRHICommandListImmediate*>(&CmdList));
			Lambda.~LAMBDA();
		}
	};

	friend class FRHICommandListExecutor;
	FRHICommandListImmediate()
		: FRHICommandList(FRHIGPUMask::All())
	{
		Data.Type = FRHICommandListBase::FCommonData::ECmdListType::Immediate;
	}
	~FRHICommandListImmediate()
	{
		check(!HasCommands());
	}
public:

	void ImmediateFlush(EImmediateFlushType::Type FlushType);
	bool StallRHIThread();
	void UnStallRHIThread();
	static bool IsStalled();

	void SetCurrentStat(TStatId Stat);

	static FGraphEventRef RenderThreadTaskFence();
	static FGraphEventArray& GetRenderThreadTaskArray();
	static void WaitOnRenderThreadTaskFence(FGraphEventRef& Fence);
	static bool AnyRenderThreadTasksOutstanding();
	FGraphEventRef RHIThreadFence(bool bSetLockFence = false);

	//Queue the given async compute commandlists in order with the current immediate commandlist
	void QueueAsyncCompute(FRHIComputeCommandList& RHIComputeCmdList);

	FORCEINLINE bool IsBottomOfPipe()
	{
		return Bypass() || IsExecuting();
	}

	FORCEINLINE bool IsTopOfPipe()
	{
		return !IsBottomOfPipe();
	}

	template <typename LAMBDA>
	FORCEINLINE_DEBUGGABLE void EnqueueLambda(LAMBDA&& Lambda)
	{
		if (IsBottomOfPipe())
		{
			Lambda(*this);
		}
		else
		{
			ALLOC_COMMAND(TRHILambdaCommand<LAMBDA>)(Forward<LAMBDA>(Lambda));
		}
	}

	FORCEINLINE FSamplerStateRHIRef CreateSamplerState(const FSamplerStateInitializerRHI& Initializer)
	{
		LLM_SCOPE(ELLMTag::RHIMisc);
		return RHICreateSamplerState(Initializer);
	}
	
	FORCEINLINE FRasterizerStateRHIRef CreateRasterizerState(const FRasterizerStateInitializerRHI& Initializer)
	{
		LLM_SCOPE(ELLMTag::RHIMisc);
		return RHICreateRasterizerState(Initializer);
	}
	
	FORCEINLINE FDepthStencilStateRHIRef CreateDepthStencilState(const FDepthStencilStateInitializerRHI& Initializer)
	{
		LLM_SCOPE(ELLMTag::RHIMisc);
		return RHICreateDepthStencilState(Initializer);
	}
	
	FORCEINLINE FBlendStateRHIRef CreateBlendState(const FBlendStateInitializerRHI& Initializer)
	{
		LLM_SCOPE(ELLMTag::RHIMisc);
		return RHICreateBlendState(Initializer);
	}
	
	FORCEINLINE FPixelShaderRHIRef CreatePixelShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
	{
		LLM_SCOPE(ELLMTag::Shaders);
		return GDynamicRHI->CreatePixelShader_RenderThread(*this, Code, Hash);
	}
	
	
	FORCEINLINE FVertexShaderRHIRef CreateVertexShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
	{
		LLM_SCOPE(ELLMTag::Shaders);
		return GDynamicRHI->CreateVertexShader_RenderThread(*this, Code, Hash);
	}
	
	FORCEINLINE FHullShaderRHIRef CreateHullShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
	{
		LLM_SCOPE(ELLMTag::Shaders);
		return GDynamicRHI->CreateHullShader_RenderThread(*this, Code, Hash);
	}
	
	FORCEINLINE FDomainShaderRHIRef CreateDomainShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
	{
		LLM_SCOPE(ELLMTag::Shaders);
		return GDynamicRHI->CreateDomainShader_RenderThread(*this, Code, Hash);
	}
	
	FORCEINLINE FGeometryShaderRHIRef CreateGeometryShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
	{
		LLM_SCOPE(ELLMTag::Shaders);
		return GDynamicRHI->CreateGeometryShader_RenderThread(*this, Code, Hash);
	}
	
	FORCEINLINE FComputeShaderRHIRef CreateComputeShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
	{
		LLM_SCOPE(ELLMTag::Shaders);
		return GDynamicRHI->CreateComputeShader_RenderThread(*this, Code, Hash);
	}
	
	FORCEINLINE FComputeFenceRHIRef CreateComputeFence(const FName& Name)
	{		
		return GDynamicRHI->RHICreateComputeFence(Name);
	}	

	FORCEINLINE FGPUFenceRHIRef CreateGPUFence(const FName& Name)
	{
		return GDynamicRHI->RHICreateGPUFence(Name);
	}

	FORCEINLINE FStagingBufferRHIRef CreateStagingBuffer()
	{
		return GDynamicRHI->RHICreateStagingBuffer();
	}

	FORCEINLINE FBoundShaderStateRHIRef CreateBoundShaderState(FRHIVertexDeclaration* VertexDeclaration, FRHIVertexShader* VertexShader, FRHIHullShader* HullShader, FRHIDomainShader* DomainShader, FRHIPixelShader* PixelShader, FRHIGeometryShader* GeometryShader)
	{
		LLM_SCOPE(ELLMTag::Shaders);
		return RHICreateBoundShaderState(VertexDeclaration, VertexShader, HullShader, DomainShader, PixelShader, GeometryShader);
	}

	FORCEINLINE FGraphicsPipelineStateRHIRef CreateGraphicsPipelineState(const FGraphicsPipelineStateInitializer& Initializer)
	{
		LLM_SCOPE(ELLMTag::Shaders);
		return RHICreateGraphicsPipelineState(Initializer);
	}

	FORCEINLINE TRefCountPtr<FRHIComputePipelineState> CreateComputePipelineState(FRHIComputeShader* ComputeShader)
	{
		LLM_SCOPE(ELLMTag::Shaders);
		return RHICreateComputePipelineState(ComputeShader);
	}

	FORCEINLINE FUniformBufferRHIRef CreateUniformBuffer(const void* Contents, const FRHIUniformBufferLayout& Layout, EUniformBufferUsage Usage)
	{
		return RHICreateUniformBuffer(Contents, Layout, Usage);
	}
	
	FORCEINLINE FIndexBufferRHIRef CreateAndLockIndexBuffer(uint32 Stride, uint32 Size, EBufferUsageFlags InUsage, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo, void*& OutDataBuffer)
	{
		return GDynamicRHI->CreateAndLockIndexBuffer_RenderThread(*this, Stride, Size, InUsage, InResourceState, CreateInfo, OutDataBuffer);
	}

	FORCEINLINE FIndexBufferRHIRef CreateAndLockIndexBuffer(uint32 Stride, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo, void*& OutDataBuffer)
	{
		ERHIAccess ResourceState = RHIGetDefaultResourceState((EBufferUsageFlags) InUsage | BUF_IndexBuffer, true);
		return CreateAndLockIndexBuffer(Stride, Size, (EBufferUsageFlags) InUsage, ResourceState, CreateInfo, OutDataBuffer);
	}
	
	UE_DEPRECATED(4.26, "The RHI resource creation API has been refactored. Use global RHICreate functions with default initial ResourceState")
	FORCEINLINE FIndexBufferRHIRef CreateIndexBuffer(uint32 Stride, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo)
	{
		return RHICreateIndexBuffer(Stride, Size, InUsage, CreateInfo);
	}
	
	FORCEINLINE void* LockIndexBuffer(FRHIIndexBuffer* IndexBuffer, uint32 Offset, uint32 SizeRHI, EResourceLockMode LockMode)
	{
		return GDynamicRHI->RHILockIndexBuffer(*this, IndexBuffer, Offset, SizeRHI, LockMode);
	}
	
	FORCEINLINE void UnlockIndexBuffer(FRHIIndexBuffer* IndexBuffer)
	{
		GDynamicRHI->RHIUnlockIndexBuffer(*this, IndexBuffer);
	}
	
	FORCEINLINE void* LockStagingBuffer(FRHIStagingBuffer* StagingBuffer, FRHIGPUFence* Fence, uint32 Offset, uint32 SizeRHI)
	{
		return GDynamicRHI->LockStagingBuffer_RenderThread(*this, StagingBuffer, Fence, Offset, SizeRHI);
	}
	
	FORCEINLINE void UnlockStagingBuffer(FRHIStagingBuffer* StagingBuffer)
	{
		GDynamicRHI->UnlockStagingBuffer_RenderThread(*this, StagingBuffer);
	}
	
	FORCEINLINE FVertexBufferRHIRef CreateAndLockVertexBuffer(uint32 Size, EBufferUsageFlags InUsage, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo, void*& OutDataBuffer)
	{
		return GDynamicRHI->CreateAndLockVertexBuffer_RenderThread(*this, Size, InUsage, InResourceState, CreateInfo, OutDataBuffer);
	}

	FORCEINLINE FVertexBufferRHIRef CreateAndLockVertexBuffer(uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo, void*& OutDataBuffer)
	{
		ERHIAccess ResourceState = RHIGetDefaultResourceState((EBufferUsageFlags) InUsage | BUF_VertexBuffer, true);
		return CreateAndLockVertexBuffer(Size, (EBufferUsageFlags) InUsage, ResourceState, CreateInfo, OutDataBuffer);
	}

	UE_DEPRECATED(4.26, "The RHI resource creation API has been refactored. Use global RHICreate functions with default initial ResourceState")
	FORCEINLINE FVertexBufferRHIRef CreateVertexBuffer(uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo)
	{
		return RHICreateVertexBuffer(Size, InUsage, CreateInfo);
	}
	
	FORCEINLINE void* LockVertexBuffer(FRHIVertexBuffer* VertexBuffer, uint32 Offset, uint32 SizeRHI, EResourceLockMode LockMode)
	{
		return GDynamicRHI->RHILockVertexBuffer(*this, VertexBuffer, Offset, SizeRHI, LockMode);
	}
	
	FORCEINLINE void UnlockVertexBuffer(FRHIVertexBuffer* VertexBuffer)
	{
		GDynamicRHI->RHIUnlockVertexBuffer(*this, VertexBuffer);
	}
	
	FORCEINLINE void CopyVertexBuffer(FRHIVertexBuffer* SourceBuffer, FRHIVertexBuffer* DestBuffer)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_CopyVertexBuffer_Flush);
		ImmediateFlush(EImmediateFlushType::FlushRHIThread);  
		GDynamicRHI->RHICopyVertexBuffer(SourceBuffer,DestBuffer);
	}

	UE_DEPRECATED(4.26, "The RHI resource creation API has been refactored. Use global RHICreate functions with default initial ResourceState")
	FORCEINLINE FStructuredBufferRHIRef CreateStructuredBuffer(uint32 Stride, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo)
	{
		return RHICreateStructuredBuffer(Stride, Size, InUsage, CreateInfo);
	}
	
	FORCEINLINE void* LockStructuredBuffer(FRHIStructuredBuffer* StructuredBuffer, uint32 Offset, uint32 SizeRHI, EResourceLockMode LockMode)
	{
		return GDynamicRHI->RHILockStructuredBuffer(*this, StructuredBuffer, Offset, SizeRHI, LockMode);
	}
	
	FORCEINLINE void UnlockStructuredBuffer(FRHIStructuredBuffer* StructuredBuffer)
	{
		GDynamicRHI->RHIUnlockStructuredBuffer(*this, StructuredBuffer);
	}
	
	FORCEINLINE FUnorderedAccessViewRHIRef CreateUnorderedAccessView(FRHIStructuredBuffer* StructuredBuffer, bool bUseUAVCounter, bool bAppendBuffer)
	{
		LLM_SCOPE(ELLMTag::RHIMisc);
		checkf(StructuredBuffer, TEXT("Can't create a view off a null resource!"));
		return GDynamicRHI->RHICreateUnorderedAccessView_RenderThread(*this, StructuredBuffer, bUseUAVCounter, bAppendBuffer);
	}
	
	FORCEINLINE FUnorderedAccessViewRHIRef CreateUnorderedAccessView(FRHITexture* Texture, uint32 MipLevel)
	{
		LLM_SCOPE(ELLMTag::RHIMisc);
		checkf(Texture, TEXT("Can't create a view off a null resource!"));
		return GDynamicRHI->RHICreateUnorderedAccessView_RenderThread(*this, Texture, MipLevel);
	}
	
	FORCEINLINE FUnorderedAccessViewRHIRef CreateUnorderedAccessView(FRHITexture* Texture, uint32 MipLevel, uint8 Format)
	{
		LLM_SCOPE(ELLMTag::RHIMisc);
		checkf(Texture, TEXT("Can't create a view off a null resource!"));
		return GDynamicRHI->RHICreateUnorderedAccessView_RenderThread(*this, Texture, MipLevel, Format);
	}

	FORCEINLINE FUnorderedAccessViewRHIRef CreateUnorderedAccessView(FRHIVertexBuffer* VertexBuffer, uint8 Format)
	{
		LLM_SCOPE(ELLMTag::RHIMisc);
		checkf(VertexBuffer, TEXT("Can't create a view off a null resource!"));
		return GDynamicRHI->RHICreateUnorderedAccessView_RenderThread(*this, VertexBuffer, Format);
	}

	FORCEINLINE FUnorderedAccessViewRHIRef CreateUnorderedAccessView(FRHIIndexBuffer* IndexBuffer, uint8 Format)
	{
		LLM_SCOPE(ELLMTag::RHIMisc);
		checkf(IndexBuffer, TEXT("Can't create a view off a null resource!"));
		return GDynamicRHI->RHICreateUnorderedAccessView_RenderThread(*this, IndexBuffer, Format);
	}

	FORCEINLINE FShaderResourceViewRHIRef CreateShaderResourceView(FRHIStructuredBuffer* StructuredBuffer)
	{
		LLM_SCOPE(ELLMTag::RHIMisc);
		checkf(StructuredBuffer, TEXT("Can't create a view off a null resource!"));
		return GDynamicRHI->RHICreateShaderResourceView_RenderThread(*this, StructuredBuffer);
	}
	
	FORCEINLINE FShaderResourceViewRHIRef CreateShaderResourceView(FRHIVertexBuffer* VertexBuffer, uint32 Stride, uint8 Format)
	{
		LLM_SCOPE(ELLMTag::RHIMisc);
		checkf(VertexBuffer, TEXT("Can't create a view off a null resource!"));
		return GDynamicRHI->CreateShaderResourceView_RenderThread(*this, VertexBuffer, Stride, Format);
	}
	
	FORCEINLINE FShaderResourceViewRHIRef CreateShaderResourceView(const FShaderResourceViewInitializer& Initializer)
	{
		LLM_SCOPE(ELLMTag::RHIMisc);
		return GDynamicRHI->CreateShaderResourceView_RenderThread(*this, Initializer);
	}

	FORCEINLINE FShaderResourceViewRHIRef CreateShaderResourceView(FRHIIndexBuffer* Buffer)
	{
		LLM_SCOPE(ELLMTag::RHIMisc);
		checkf(Buffer, TEXT("Can't create a view off a null resource!"));
		return GDynamicRHI->CreateShaderResourceView_RenderThread(*this, Buffer);
	}
	
	FORCEINLINE uint64 CalcTexture2DPlatformSize(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 NumSamples, ETextureCreateFlags Flags, const FRHIResourceCreateInfo& CreateInfo, uint32& OutAlign)
	{
		return RHICalcTexture2DPlatformSize(SizeX, SizeY, Format, NumMips, NumSamples, Flags, CreateInfo, OutAlign);
	}
	
	FORCEINLINE uint64 CalcTexture3DPlatformSize(uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, const FRHIResourceCreateInfo& CreateInfo, uint32& OutAlign)
	{
		return RHICalcTexture3DPlatformSize(SizeX, SizeY, SizeZ, Format, NumMips, Flags, CreateInfo, OutAlign);
	}
	
	FORCEINLINE uint64 CalcTextureCubePlatformSize(uint32 Size, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, const FRHIResourceCreateInfo& CreateInfo, uint32& OutAlign)
	{
		return RHICalcTextureCubePlatformSize(Size, Format, NumMips, Flags, CreateInfo, OutAlign);
	}
	
	FORCEINLINE void GetTextureMemoryStats(FTextureMemoryStats& OutStats)
	{
		RHIGetTextureMemoryStats(OutStats);
	}
	
	FORCEINLINE bool GetTextureMemoryVisualizeData(FColor* TextureData,int32 SizeX,int32 SizeY,int32 Pitch,int32 PixelSize)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_GetTextureMemoryVisualizeData_Flush);
		ImmediateFlush(EImmediateFlushType::FlushRHIThread); 
		return GDynamicRHI->RHIGetTextureMemoryVisualizeData(TextureData,SizeX,SizeY,Pitch,PixelSize);
	}
	
	FORCEINLINE FTextureReferenceRHIRef CreateTextureReference(FLastRenderTimeContainer* LastRenderTime)
	{
		LLM_SCOPE(ELLMTag::Textures);
		return GDynamicRHI->RHICreateTextureReference_RenderThread(*this, LastRenderTime);
	}
	
	UE_DEPRECATED(4.26, "The RHI resource creation API has been refactored. Use global RHICreate functions with default initial ResourceState")
	FORCEINLINE FTexture2DRHIRef CreateTexture2D(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 NumSamples, ETextureCreateFlags Flags, FRHIResourceCreateInfo& CreateInfo)
	{
		return RHICreateTexture2D(SizeX, SizeY, Format, NumMips, NumSamples, Flags, CreateInfo);
	}

	UE_DEPRECATED(4.26, "The RHI resource creation API has been refactored. Use global RHICreate functions with default initial ResourceState")
	FORCEINLINE FTexture2DRHIRef CreateTextureExternal2D(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 NumSamples, ETextureCreateFlags Flags, FRHIResourceCreateInfo& CreateInfo)
	{
		return RHICreateTextureExternal2D(SizeX, SizeY, Format, NumMips, NumSamples, Flags, CreateInfo);
	}

	UE_DEPRECATED(4.26, "The RHI resource creation API has been refactored. Use global RHICreate functions with default initial ResourceState")
	FORCEINLINE FTexture2DRHIRef AsyncCreateTexture2D(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, void** InitialMipData, uint32 NumInitialMips)
	{
		return RHIAsyncCreateTexture2D(SizeX, SizeY, Format, NumMips, Flags, InitialMipData, NumInitialMips);
	}
	
	FORCEINLINE void CopySharedMips(FRHITexture2D* DestTexture2D, FRHITexture2D* SrcTexture2D)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_CopySharedMips_Flush);
		DestTexture2D->AddRef();
		SrcTexture2D->AddRef();
		EnqueueLambda([DestTexture2D, SrcTexture2D](FRHICommandList&)
		{
			LLM_SCOPE(ELLMTag::Textures);
			GDynamicRHI->RHICopySharedMips(DestTexture2D, SrcTexture2D);
			DestTexture2D->Release();
			SrcTexture2D->Release();
		});
	}

	UE_DEPRECATED(4.26, "The RHI resource creation API has been refactored. Use global RHICreate functions with default initial ResourceState")
	FORCEINLINE FTexture2DArrayRHIRef CreateTexture2DArray(uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint8 Format, uint32 NumMips, uint32 NumSamples, ETextureCreateFlags Flags, FRHIResourceCreateInfo& CreateInfo)
	{
		return RHICreateTexture2DArray(SizeX, SizeY, SizeZ, Format, NumMips, NumSamples, Flags, CreateInfo);
	}

	UE_DEPRECATED(4.26, "The RHI resource creation API has been refactored. Use global RHICreate functions with default initial ResourceState")
	FORCEINLINE FTexture3DRHIRef CreateTexture3D(uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, FRHIResourceCreateInfo& CreateInfo)
	{
		return RHICreateTexture3D(SizeX, SizeY, SizeZ, Format, NumMips, Flags, CreateInfo);
	}
	
	FORCEINLINE void GetResourceInfo(FRHITexture* Ref, FRHIResourceInfo& OutInfo)
	{
		return RHIGetResourceInfo(Ref, OutInfo);
	}
	
	FORCEINLINE FShaderResourceViewRHIRef CreateShaderResourceView(FRHITexture* Texture, const FRHITextureSRVCreateInfo& CreateInfo)
	{
		LLM_SCOPE(ELLMTag::RHIMisc);
		checkf(Texture, TEXT("Can't create a view off a null resource!"));
		return GDynamicRHI->RHICreateShaderResourceView_RenderThread(*this, Texture, CreateInfo);
	}

	FORCEINLINE FShaderResourceViewRHIRef CreateShaderResourceView(FRHITexture* Texture, uint8 MipLevel)
	{
		LLM_SCOPE(ELLMTag::RHIMisc);
		checkf(Texture, TEXT("Can't create a view off a null resource!"));
		const FRHITextureSRVCreateInfo CreateInfo(MipLevel, 1, Texture->GetFormat());
		return GDynamicRHI->RHICreateShaderResourceView_RenderThread(*this, Texture, CreateInfo);
	}
	
	FORCEINLINE FShaderResourceViewRHIRef CreateShaderResourceView(FRHITexture* Texture, uint8 MipLevel, uint8 NumMipLevels, uint8 Format)
	{
		LLM_SCOPE(ELLMTag::RHIMisc);
		checkf(Texture, TEXT("Can't create a view off a null resource!"));
		const FRHITextureSRVCreateInfo CreateInfo(MipLevel, NumMipLevels, Format);
		return GDynamicRHI->RHICreateShaderResourceView_RenderThread(*this, Texture, CreateInfo);
	}

	FORCEINLINE FShaderResourceViewRHIRef CreateShaderResourceViewWriteMask(FRHITexture2D* Texture2DRHI)
	{
		LLM_SCOPE(ELLMTag::RHIMisc);
		checkf(Texture2DRHI, TEXT("Can't create a view off a null resource!"));
		return GDynamicRHI->RHICreateShaderResourceViewWriteMask_RenderThread(*this, Texture2DRHI);
	}

	FORCEINLINE FShaderResourceViewRHIRef CreateShaderResourceViewFMask(FRHITexture2D* Texture2DRHI)
	{
		LLM_SCOPE(ELLMTag::RHIMisc);
		checkf(Texture2DRHI, TEXT("Can't create a view off a null resource!"));
		return GDynamicRHI->RHICreateShaderResourceViewFMask_RenderThread(*this, Texture2DRHI);
	}

	//UE_DEPRECATED(4.23, "This function is deprecated and will be removed in future releases. Renderer version implemented.")
	FORCEINLINE void GenerateMips(FRHITexture* Texture)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_GenerateMips_Flush);
		ImmediateFlush(EImmediateFlushType::FlushRHIThread); return GDynamicRHI->RHIGenerateMips(Texture);
	}
	
	FORCEINLINE uint32 ComputeMemorySize(FRHITexture* TextureRHI)
	{
		return RHIComputeMemorySize(TextureRHI);
	}
	
	FORCEINLINE FTexture2DRHIRef AsyncReallocateTexture2D(FRHITexture2D* Texture2D, int32 NewMipCount, int32 NewSizeX, int32 NewSizeY, FThreadSafeCounter* RequestStatus)
	{
		LLM_SCOPE(ELLMTag::Textures);
		return GDynamicRHI->AsyncReallocateTexture2D_RenderThread(*this, Texture2D, NewMipCount, NewSizeX, NewSizeY, RequestStatus);
	}
	
	FORCEINLINE ETextureReallocationStatus FinalizeAsyncReallocateTexture2D(FRHITexture2D* Texture2D, bool bBlockUntilCompleted)
	{
		LLM_SCOPE(ELLMTag::Textures);
		return GDynamicRHI->FinalizeAsyncReallocateTexture2D_RenderThread(*this, Texture2D, bBlockUntilCompleted);
	}
	
	FORCEINLINE ETextureReallocationStatus CancelAsyncReallocateTexture2D(FRHITexture2D* Texture2D, bool bBlockUntilCompleted)
	{
		return GDynamicRHI->CancelAsyncReallocateTexture2D_RenderThread(*this, Texture2D, bBlockUntilCompleted);
	}
	
	FORCEINLINE void* LockTexture2D(FRHITexture2D* Texture, uint32 MipIndex, EResourceLockMode LockMode, uint32& DestStride, bool bLockWithinMiptail, bool bFlushRHIThread = true)
	{
		LLM_SCOPE(ELLMTag::Textures);
		return GDynamicRHI->LockTexture2D_RenderThread(*this, Texture, MipIndex, LockMode, DestStride, bLockWithinMiptail, bFlushRHIThread);
	}
	
	FORCEINLINE void UnlockTexture2D(FRHITexture2D* Texture, uint32 MipIndex, bool bLockWithinMiptail, bool bFlushRHIThread = true)
	{		
		GDynamicRHI->UnlockTexture2D_RenderThread(*this, Texture, MipIndex, bLockWithinMiptail, bFlushRHIThread);
	}
	
	FORCEINLINE void* LockTexture2DArray(FRHITexture2DArray* Texture, uint32 TextureIndex, uint32 MipIndex, EResourceLockMode LockMode, uint32& DestStride, bool bLockWithinMiptail)
	{
		LLM_SCOPE(ELLMTag::Textures);
		return GDynamicRHI->LockTexture2DArray_RenderThread(*this, Texture, TextureIndex, MipIndex, LockMode, DestStride, bLockWithinMiptail);
	}
	
	FORCEINLINE void UnlockTexture2DArray(FRHITexture2DArray* Texture, uint32 TextureIndex, uint32 MipIndex, bool bLockWithinMiptail)
	{
		LLM_SCOPE(ELLMTag::Textures);
		GDynamicRHI->UnlockTexture2DArray_RenderThread(*this, Texture, TextureIndex, MipIndex, bLockWithinMiptail);
	}
	
	FORCEINLINE void UpdateTexture2D(FRHITexture2D* Texture, uint32 MipIndex, const struct FUpdateTextureRegion2D& UpdateRegion, uint32 SourcePitch, const uint8* SourceData)
	{		
		checkf(UpdateRegion.DestX + UpdateRegion.Width <= Texture->GetSizeX(), TEXT("UpdateTexture2D out of bounds on X. Texture: %s, %i, %i, %i"), *Texture->GetName().ToString(), UpdateRegion.DestX, UpdateRegion.Width, Texture->GetSizeX());
		checkf(UpdateRegion.DestY + UpdateRegion.Height <= Texture->GetSizeY(), TEXT("UpdateTexture2D out of bounds on Y. Texture: %s, %i, %i, %i"), *Texture->GetName().ToString(), UpdateRegion.DestY, UpdateRegion.Height, Texture->GetSizeY());
		LLM_SCOPE(ELLMTag::Textures);
		GDynamicRHI->UpdateTexture2D_RenderThread(*this, Texture, MipIndex, UpdateRegion, SourcePitch, SourceData);
	}

	FORCEINLINE void UpdateFromBufferTexture2D(FRHITexture2D* Texture, uint32 MipIndex, const struct FUpdateTextureRegion2D& UpdateRegion, uint32 SourcePitch, FRHIStructuredBuffer* Buffer, uint32 BufferOffset)
	{
		checkf(UpdateRegion.DestX + UpdateRegion.Width <= Texture->GetSizeX(), TEXT("UpdateFromBufferTexture2D out of bounds on X. Texture: %s, %i, %i, %i"), *Texture->GetName().ToString(), UpdateRegion.DestX, UpdateRegion.Width, Texture->GetSizeX());
		checkf(UpdateRegion.DestY + UpdateRegion.Height <= Texture->GetSizeY(), TEXT("UpdateFromBufferTexture2D out of bounds on Y. Texture: %s, %i, %i, %i"), *Texture->GetName().ToString(), UpdateRegion.DestY, UpdateRegion.Height, Texture->GetSizeY());
		LLM_SCOPE(ELLMTag::Textures);
		GDynamicRHI->UpdateFromBufferTexture2D_RenderThread(*this, Texture, MipIndex, UpdateRegion, SourcePitch, Buffer, BufferOffset);
	}

	FORCEINLINE FUpdateTexture3DData BeginUpdateTexture3D(FRHITexture3D* Texture, uint32 MipIndex, const struct FUpdateTextureRegion3D& UpdateRegion)
	{
		checkf(UpdateRegion.DestX + UpdateRegion.Width <= Texture->GetSizeX(), TEXT("UpdateTexture3D out of bounds on X. Texture: %s, %i, %i, %i"), *Texture->GetName().ToString(), UpdateRegion.DestX, UpdateRegion.Width, Texture->GetSizeX());
		checkf(UpdateRegion.DestY + UpdateRegion.Height <= Texture->GetSizeY(), TEXT("UpdateTexture3D out of bounds on Y. Texture: %s, %i, %i, %i"), *Texture->GetName().ToString(), UpdateRegion.DestY, UpdateRegion.Height, Texture->GetSizeY());
		checkf(UpdateRegion.DestZ + UpdateRegion.Depth <= Texture->GetSizeZ(), TEXT("UpdateTexture3D out of bounds on Z. Texture: %s, %i, %i, %i"), *Texture->GetName().ToString(), UpdateRegion.DestZ, UpdateRegion.Depth, Texture->GetSizeZ());
		LLM_SCOPE(ELLMTag::Textures);
		return GDynamicRHI->BeginUpdateTexture3D_RenderThread(*this, Texture, MipIndex, UpdateRegion);
	}

	FORCEINLINE void EndUpdateTexture3D(FUpdateTexture3DData& UpdateData)
	{		
		LLM_SCOPE(ELLMTag::Textures);
		GDynamicRHI->EndUpdateTexture3D_RenderThread(*this, UpdateData);
	}

	FORCEINLINE void EndMultiUpdateTexture3D(TArray<FUpdateTexture3DData>& UpdateDataArray)
	{
		LLM_SCOPE(ELLMTag::Textures);
		GDynamicRHI->EndMultiUpdateTexture3D_RenderThread(*this, UpdateDataArray);
	}
	
	FORCEINLINE void UpdateTexture3D(FRHITexture3D* Texture, uint32 MipIndex, const struct FUpdateTextureRegion3D& UpdateRegion, uint32 SourceRowPitch, uint32 SourceDepthPitch, const uint8* SourceData)
	{
		checkf(UpdateRegion.DestX + UpdateRegion.Width <= Texture->GetSizeX(), TEXT("UpdateTexture3D out of bounds on X. Texture: %s, %i, %i, %i"), *Texture->GetName().ToString(), UpdateRegion.DestX, UpdateRegion.Width, Texture->GetSizeX());
		checkf(UpdateRegion.DestY + UpdateRegion.Height <= Texture->GetSizeY(), TEXT("UpdateTexture3D out of bounds on Y. Texture: %s, %i, %i, %i"), *Texture->GetName().ToString(), UpdateRegion.DestY, UpdateRegion.Height, Texture->GetSizeY());
		checkf(UpdateRegion.DestZ + UpdateRegion.Depth <= Texture->GetSizeZ(), TEXT("UpdateTexture3D out of bounds on Z. Texture: %s, %i, %i, %i"), *Texture->GetName().ToString(), UpdateRegion.DestZ, UpdateRegion.Depth, Texture->GetSizeZ());
		LLM_SCOPE(ELLMTag::Textures);
		GDynamicRHI->UpdateTexture3D_RenderThread(*this, Texture, MipIndex, UpdateRegion, SourceRowPitch, SourceDepthPitch, SourceData);
	}
	
	UE_DEPRECATED(4.26, "The RHI resource creation API has been refactored. Use global RHICreate functions with default initial ResourceState")
	FORCEINLINE FTextureCubeRHIRef CreateTextureCube(uint32 Size, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, FRHIResourceCreateInfo& CreateInfo)
	{
		return RHICreateTextureCube(Size, Format, NumMips, Flags, CreateInfo);
	}
	
	UE_DEPRECATED(4.26, "The RHI resource creation API has been refactored. Use global RHICreate functions with default initial ResourceState")
	FORCEINLINE FTextureCubeRHIRef CreateTextureCubeArray(uint32 Size, uint32 ArraySize, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, FRHIResourceCreateInfo& CreateInfo)
	{
		return RHICreateTextureCubeArray(Size, ArraySize, Format, NumMips, Flags, CreateInfo);
	}
	
	FORCEINLINE void* LockTextureCubeFace(FRHITextureCube* Texture, uint32 FaceIndex, uint32 ArrayIndex, uint32 MipIndex, EResourceLockMode LockMode, uint32& DestStride, bool bLockWithinMiptail)
	{
		LLM_SCOPE(ELLMTag::Textures);
		return GDynamicRHI->RHILockTextureCubeFace_RenderThread(*this, Texture, FaceIndex, ArrayIndex, MipIndex, LockMode, DestStride, bLockWithinMiptail);
	}
	
	FORCEINLINE void UnlockTextureCubeFace(FRHITextureCube* Texture, uint32 FaceIndex, uint32 ArrayIndex, uint32 MipIndex, bool bLockWithinMiptail)
	{
		LLM_SCOPE(ELLMTag::Textures);
		GDynamicRHI->RHIUnlockTextureCubeFace_RenderThread(*this, Texture, FaceIndex, ArrayIndex, MipIndex, bLockWithinMiptail);
	}
	
	FORCEINLINE void BindDebugLabelName(FRHITexture* Texture, const TCHAR* Name)
	{
		RHIBindDebugLabelName(Texture, Name);
	}

	FORCEINLINE void BindDebugLabelName(FRHIUnorderedAccessView* UnorderedAccessViewRHI, const TCHAR* Name)
	{
		RHIBindDebugLabelName(UnorderedAccessViewRHI, Name);
	}

	FORCEINLINE void ReadSurfaceData(FRHITexture* Texture,FIntRect Rect,TArray<FColor>& OutData,FReadSurfaceDataFlags InFlags)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_ReadSurfaceData_Flush);
		LLM_SCOPE(ELLMTag::Textures);
		ImmediateFlush(EImmediateFlushType::FlushRHIThread);  
		GDynamicRHI->RHIReadSurfaceData(Texture,Rect,OutData,InFlags);
	}

	FORCEINLINE void ReadSurfaceData(FRHITexture* Texture, FIntRect Rect, TArray<FLinearColor>& OutData, FReadSurfaceDataFlags InFlags)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_ReadSurfaceData_Flush);
		LLM_SCOPE(ELLMTag::Textures);
		ImmediateFlush(EImmediateFlushType::FlushRHIThread);
		GDynamicRHI->RHIReadSurfaceData(Texture, Rect, OutData, InFlags);
	}
	
	FORCEINLINE void MapStagingSurface(FRHITexture* Texture, void*& OutData, int32& OutWidth, int32& OutHeight)
	{
		LLM_SCOPE(ELLMTag::Textures);
		GDynamicRHI->RHIMapStagingSurface_RenderThread(*this, Texture, nullptr, OutData, OutWidth, OutHeight);
	}

	FORCEINLINE void MapStagingSurface(FRHITexture* Texture, FRHIGPUFence* Fence, void*& OutData, int32& OutWidth, int32& OutHeight)
	{
		LLM_SCOPE(ELLMTag::Textures);
		GDynamicRHI->RHIMapStagingSurface_RenderThread(*this, Texture, Fence, OutData, OutWidth, OutHeight);
	}
	
	FORCEINLINE void UnmapStagingSurface(FRHITexture* Texture)
	{
		LLM_SCOPE(ELLMTag::Textures);
		GDynamicRHI->RHIUnmapStagingSurface_RenderThread(*this, Texture);
	}
	
	FORCEINLINE void ReadSurfaceFloatData(FRHITexture* Texture,FIntRect Rect,TArray<FFloat16Color>& OutData,ECubeFace CubeFace,int32 ArrayIndex,int32 MipIndex)
	{
		LLM_SCOPE(ELLMTag::Textures);
		GDynamicRHI->RHIReadSurfaceFloatData_RenderThread(*this, Texture,Rect,OutData,CubeFace,ArrayIndex,MipIndex);
	}

	FORCEINLINE void ReadSurfaceFloatData(FRHITexture* Texture,FIntRect Rect,TArray<FFloat16Color>& OutData,FReadSurfaceDataFlags Flags)
	{
		LLM_SCOPE(ELLMTag::Textures);
		GDynamicRHI->RHIReadSurfaceFloatData_RenderThread(*this, Texture,Rect,OutData,Flags);
	}

	FORCEINLINE void Read3DSurfaceFloatData(FRHITexture* Texture,FIntRect Rect,FIntPoint ZMinMax,TArray<FFloat16Color>& OutData, FReadSurfaceDataFlags Flags = FReadSurfaceDataFlags())
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_Read3DSurfaceFloatData_Flush);
		LLM_SCOPE(ELLMTag::Textures);
		ImmediateFlush(EImmediateFlushType::FlushRHIThread);  
		GDynamicRHI->RHIRead3DSurfaceFloatData(Texture,Rect,ZMinMax,OutData,Flags);
	}
	
	UE_DEPRECATED(4.23, "CreateRenderQuery API is deprecated; use RHICreateRenderQueryPool and suballocate queries there")
	FORCEINLINE FRenderQueryRHIRef CreateRenderQuery(ERenderQueryType QueryType)
	{
		FScopedRHIThreadStaller StallRHIThread(*this);
		return GDynamicRHI->RHICreateRenderQuery(QueryType);
	}

	UE_DEPRECATED(4.23, "CreateRenderQuery API is deprecated; use RHICreateRenderQueryPool and suballocate queries there")
	FORCEINLINE FRenderQueryRHIRef CreateRenderQuery_RenderThread(ERenderQueryType QueryType)
	{
		return GDynamicRHI->RHICreateRenderQuery_RenderThread(*this, QueryType);
	}


	FORCEINLINE void AcquireTransientResource_RenderThread(FRHITexture* Texture)
	{
		if (!Texture->IsCommitted() )
		{
			if (GSupportsTransientResourceAliasing)
			{
				GDynamicRHI->RHIAcquireTransientResource_RenderThread(Texture);
			}
			Texture->SetCommitted(true);
		}
	}

	FORCEINLINE void DiscardTransientResource_RenderThread(FRHITexture* Texture)
	{
		if (Texture->IsCommitted())
		{
			if (GSupportsTransientResourceAliasing)
			{
				GDynamicRHI->RHIDiscardTransientResource_RenderThread(Texture);
			}
			Texture->SetCommitted(false);
		}
	}

	FORCEINLINE void AcquireTransientResource_RenderThread(FRHIVertexBuffer* Buffer)
	{
		if (!Buffer->IsCommitted())
		{
			if (GSupportsTransientResourceAliasing)
			{
				GDynamicRHI->RHIAcquireTransientResource_RenderThread(Buffer);
			}
			Buffer->SetCommitted(true);
		}
	}

	FORCEINLINE void DiscardTransientResource_RenderThread(FRHIVertexBuffer* Buffer)
	{
		if (Buffer->IsCommitted())
		{
			if (GSupportsTransientResourceAliasing)
			{
				GDynamicRHI->RHIDiscardTransientResource_RenderThread(Buffer);
			}
			Buffer->SetCommitted(false);
		}
	}

	FORCEINLINE void AcquireTransientResource_RenderThread(FRHIStructuredBuffer* Buffer)
	{
		if (!Buffer->IsCommitted())
		{
			if (GSupportsTransientResourceAliasing)
			{
				GDynamicRHI->RHIAcquireTransientResource_RenderThread(Buffer);
			}
			Buffer->SetCommitted(true);
		}
	}

	FORCEINLINE void DiscardTransientResource_RenderThread(FRHIStructuredBuffer* Buffer)
	{
		if (Buffer->IsCommitted())
		{
			if (GSupportsTransientResourceAliasing)
			{
				GDynamicRHI->RHIDiscardTransientResource_RenderThread(Buffer);
			}
			Buffer->SetCommitted(false);
		}
	}

	FORCEINLINE bool GetRenderQueryResult(FRHIRenderQuery* RenderQuery, uint64& OutResult, bool bWait, uint32 GPUIndex = INDEX_NONE)
	{
		return RHIGetRenderQueryResult(RenderQuery, OutResult, bWait, GPUIndex);
	}

	FORCEINLINE uint32 GetViewportNextPresentGPUIndex(FRHIViewport* Viewport)
	{
		return GDynamicRHI->RHIGetViewportNextPresentGPUIndex(Viewport);
	}

	FORCEINLINE FTexture2DRHIRef GetViewportBackBuffer(FRHIViewport* Viewport)
	{
		return RHIGetViewportBackBuffer(Viewport);
	}
	
	FORCEINLINE void AdvanceFrameForGetViewportBackBuffer(FRHIViewport* Viewport)
	{
		return RHIAdvanceFrameForGetViewportBackBuffer(Viewport);
	}
	
	FORCEINLINE void AcquireThreadOwnership()
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_AcquireThreadOwnership_Flush);
		ImmediateFlush(EImmediateFlushType::FlushRHIThread); 
		 
		return GDynamicRHI->RHIAcquireThreadOwnership();
	}
	
	FORCEINLINE void ReleaseThreadOwnership()
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_ReleaseThreadOwnership_Flush);
		ImmediateFlush(EImmediateFlushType::FlushRHIThread); 
		 
		return GDynamicRHI->RHIReleaseThreadOwnership();
	}
	
	FORCEINLINE void FlushResources()
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_FlushResources_Flush);
		ImmediateFlush(EImmediateFlushType::FlushRHIThread); 
		 
		return GDynamicRHI->RHIFlushResources();
	}
	
	FORCEINLINE uint32 GetGPUFrameCycles()
	{
		return RHIGetGPUFrameCycles(GetGPUMask().ToIndex());
	}
	
	FORCEINLINE FViewportRHIRef CreateViewport(void* WindowHandle, uint32 SizeX, uint32 SizeY, bool bIsFullscreen, EPixelFormat PreferredPixelFormat)
	{
		LLM_SCOPE(ELLMTag::RenderTargets);
		return RHICreateViewport(WindowHandle, SizeX, SizeY, bIsFullscreen, PreferredPixelFormat);
	}
	
	FORCEINLINE void ResizeViewport(FRHIViewport* Viewport, uint32 SizeX, uint32 SizeY, bool bIsFullscreen, EPixelFormat PreferredPixelFormat)
	{
		LLM_SCOPE(ELLMTag::RenderTargets);
		RHIResizeViewport(Viewport, SizeX, SizeY, bIsFullscreen, PreferredPixelFormat);
	}
	
	FORCEINLINE void Tick(float DeltaTime)
	{
		LLM_SCOPE(ELLMTag::RHIMisc);
		RHITick(DeltaTime);
	}
	
	FORCEINLINE void BlockUntilGPUIdle()
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_BlockUntilGPUIdle_Flush);
		ImmediateFlush(EImmediateFlushType::FlushRHIThread);  
		GDynamicRHI->RHIBlockUntilGPUIdle();
	}

	FORCEINLINE_DEBUGGABLE void SubmitCommandsAndFlushGPU()
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_SubmitCommandsAndFlushGPU_Flush);
		ImmediateFlush(EImmediateFlushType::FlushRHIThread);
		GDynamicRHI->RHISubmitCommandsAndFlushGPU();
	}
	
	FORCEINLINE void SuspendRendering()
	{
		RHISuspendRendering();
	}
	
	FORCEINLINE void ResumeRendering()
	{
		RHIResumeRendering();
	}
	
	FORCEINLINE bool IsRenderingSuspended()
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_IsRenderingSuspended_Flush);
		ImmediateFlush(EImmediateFlushType::FlushRHIThread); 
		return GDynamicRHI->RHIIsRenderingSuspended();
	}

	FORCEINLINE bool EnqueueDecompress(uint8_t* SrcBuffer, uint8_t* DestBuffer, int CompressedSize, void* ErrorCodeBuffer)
	{
		return GDynamicRHI->RHIEnqueueDecompress(SrcBuffer, DestBuffer, CompressedSize, ErrorCodeBuffer);
	}
		
	FORCEINLINE bool GetAvailableResolutions(FScreenResolutionArray& Resolutions, bool bIgnoreRefreshRate)
	{
		return RHIGetAvailableResolutions(Resolutions, bIgnoreRefreshRate);
	}
	
	FORCEINLINE void GetSupportedResolution(uint32& Width, uint32& Height)
	{
		RHIGetSupportedResolution(Width, Height);
	}
	
	FORCEINLINE void VirtualTextureSetFirstMipInMemory(FRHITexture2D* Texture, uint32 FirstMip)
	{
		GDynamicRHI->VirtualTextureSetFirstMipInMemory_RenderThread(*this, Texture, FirstMip);
	}
	
	FORCEINLINE void VirtualTextureSetFirstMipVisible(FRHITexture2D* Texture, uint32 FirstMip)
	{
		GDynamicRHI->VirtualTextureSetFirstMipVisible_RenderThread(*this, Texture, FirstMip);
	}

	UE_DEPRECATED(4.23, "CopySubTextureRegion API is deprecated; please use CopyTexture instead.")
	FORCEINLINE void CopySubTextureRegion(FRHITexture2D* SourceTexture, FRHITexture2D* DestinationTexture, FBox2D SourceBox, FBox2D DestinationBox)
	{
		GDynamicRHI->RHICopySubTextureRegion_RenderThread(*this, SourceTexture, DestinationTexture, SourceBox, DestinationBox);
	}
	
	FORCEINLINE void ExecuteCommandList(FRHICommandList* CmdList)
	{
		FScopedRHIThreadStaller StallRHIThread(*this);
		GDynamicRHI->RHIExecuteCommandList(CmdList);
	}
	
	FORCEINLINE void* GetNativeDevice()
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_GetNativeDevice_Flush);
		ImmediateFlush(EImmediateFlushType::FlushRHIThread); 
		 
		return GDynamicRHI->RHIGetNativeDevice();
	}
	
	FORCEINLINE void* GetNativePhysicalDevice()
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_GetNativePhysicalDevice_Flush);
		ImmediateFlush(EImmediateFlushType::FlushRHIThread); 
		 
		return GDynamicRHI->RHIGetNativePhysicalDevice();
	}
	
	FORCEINLINE void* GetNativeGraphicsQueue()
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_GetNativeGraphicsQueue_Flush);
		ImmediateFlush(EImmediateFlushType::FlushRHIThread); 
		 
		return GDynamicRHI->RHIGetNativeGraphicsQueue();
	}
	
	FORCEINLINE void* GetNativeComputeQueue()
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_GetNativeComputeQueue_Flush);
		ImmediateFlush(EImmediateFlushType::FlushRHIThread); 
		 
		return GDynamicRHI->RHIGetNativeComputeQueue();
	}
	
	FORCEINLINE void* GetNativeInstance()
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_GetNativeInstance_Flush);
		ImmediateFlush(EImmediateFlushType::FlushRHIThread);

		return GDynamicRHI->RHIGetNativeInstance();
	}
	
	FORCEINLINE void* GetNativeCommandBuffer()
	{
		return GDynamicRHI->RHIGetNativeCommandBuffer();
	}

	FORCEINLINE class IRHICommandContext* GetDefaultContext()
	{
		return RHIGetDefaultContext();
	}
	
	FORCEINLINE class IRHICommandContextContainer* GetCommandContextContainer(int32 Index, int32 Num)
	{
		return RHIGetCommandContextContainer(Index, Num, GetGPUMask());
	}
	void UpdateTextureReference(FRHITextureReference* TextureRef, FRHITexture* NewTexture);

	FORCEINLINE void PollRenderQueryResults()
	{
		GDynamicRHI->RHIPollRenderQueryResults();
	}

	/**
	 * @param UpdateInfos - an array of update infos
	 * @param Num - number of update infos
	 * @param bNeedReleaseRefs - whether Release need to be called on RHI resources referenced by update infos
	 */
	void UpdateRHIResources(FRHIResourceUpdateInfo* UpdateInfos, int32 Num, bool bNeedReleaseRefs);

	FORCEINLINE void BeginLateLatching(int32 FrameNumber)
	{
		if (Bypass())
		{
			GetContext().RHIBeginLateLatching(FrameNumber);
		}
		else
		{
			ALLOC_COMMAND(FRHICommandBeginLateLatching)(FrameNumber);
		}
	}

	FORCEINLINE void EndLateLatching()
	{
		if (Bypass())
		{
			GetContext().RHIEndLateLatching();
		}
		else
		{
			ALLOC_COMMAND(FRHICommandEndLateLatching)();
		}
	}
};

class FRHICommandListScopedFlushAndExecute
{
	FRHICommandListImmediate& RHICmdList;

public:
	FRHICommandListScopedFlushAndExecute(FRHICommandListImmediate& InRHICmdList)
		: RHICmdList(InRHICmdList)
	{
		check(RHICmdList.IsTopOfPipe());
		RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
		RHICmdList.bExecuting = true;
	}
	~FRHICommandListScopedFlushAndExecute()
	{
		RHICmdList.bExecuting = false;
	}
};

struct FScopedGPUMask
{
	FRHIComputeCommandList* RHICmdList;
	FRHIGPUMask PrevGPUMask;
	FORCEINLINE FScopedGPUMask(FRHIComputeCommandList& InRHICmdList, FRHIGPUMask InGPUMask)
		: RHICmdList(&InRHICmdList)
		, PrevGPUMask(InRHICmdList.GetGPUMask())
	{
		InRHICmdList.SetGPUMask(InGPUMask);
	}
	FORCEINLINE FScopedGPUMask(FScopedGPUMask&& Other)
		: RHICmdList(Other.RHICmdList)
		, PrevGPUMask(Other.PrevGPUMask)
	{
		Other.RHICmdList = nullptr;
	}
	FORCEINLINE ~FScopedGPUMask()
	{
		if (RHICmdList != nullptr)
		{
			RHICmdList->SetGPUMask(PrevGPUMask);
		}
	}
};

#if WITH_MGPU
	#define SCOPED_GPU_MASK(RHICmdList, GPUMask) FScopedGPUMask PREPROCESSOR_JOIN(ScopedGPUMask, __LINE__){ RHICmdList, GPUMask }
#else
	#define SCOPED_GPU_MASK(RHICmdList, GPUMask)
#endif // WITH_MGPU

struct RHI_API FScopedUniformBufferGlobalBindings
{
	FScopedUniformBufferGlobalBindings(FRHIComputeCommandList& InRHICmdList, FUniformBufferStaticBindings UniformBuffers)
		: RHICmdList(InRHICmdList)
	{
#if VALIDATE_UNIFORM_BUFFER_GLOBAL_BINDINGS
		checkf(!bRecursionGuard, TEXT("Uniform buffer global binding scope has been called recursively!"));
		bRecursionGuard = true;
#endif

		RHICmdList.SetGlobalUniformBuffers(UniformBuffers);
	}

	template <typename... TArgs>
	FScopedUniformBufferGlobalBindings(FRHIComputeCommandList& InRHICmdList, TArgs... Args)
		: FScopedUniformBufferGlobalBindings(InRHICmdList, FUniformBufferStaticBindings{ Args... })
	{}

	~FScopedUniformBufferGlobalBindings()
	{
		RHICmdList.SetGlobalUniformBuffers(FUniformBufferStaticBindings());

#if VALIDATE_UNIFORM_BUFFER_GLOBAL_BINDINGS
		bRecursionGuard = false;
#endif
	}

	FRHIComputeCommandList& RHICmdList;

#if VALIDATE_UNIFORM_BUFFER_GLOBAL_BINDINGS
	static bool bRecursionGuard;
#endif
};

#define SCOPED_UNIFORM_BUFFER_GLOBAL_BINDINGS(RHICmdList, UniformBuffers) FScopedUniformBufferGlobalBindings PREPROCESSOR_JOIN(UniformBuffers, __LINE__){ RHICmdList, UniformBuffers }

// Single commandlist for async compute generation.  In the future we may expand this to allow async compute command generation
// on multiple threads at once.
class RHI_API FRHIAsyncComputeCommandListImmediate : public FRHIComputeCommandList
{
public:
	FRHIAsyncComputeCommandListImmediate()
		: FRHIComputeCommandList(FRHIGPUMask::All())
	{}

	//If RHIThread is enabled this will dispatch all current commands to the RHI Thread.  If RHI thread is disabled
	//this will immediately execute the current commands.
	//This also queues a GPU Submission command as the final command in the dispatch.
	static void ImmediateDispatch(FRHIAsyncComputeCommandListImmediate& RHIComputeCmdList);
};

// typedef to mark the recursive use of commandlists in the RHI implementations

class RHI_API FRHICommandList_RecursiveHazardous : public FRHICommandList
{
public:
	FRHICommandList_RecursiveHazardous(IRHICommandContext *Context, FRHIGPUMask InGPUMask = FRHIGPUMask::All())
		: FRHICommandList(InGPUMask)
	{
		// Always grab the validation RHI context if active, so that the
		// validation RHI can see any RHI commands enqueued within the RHI itself.
		SetContext(static_cast<IRHICommandContext*>(&Context->GetHighestLevelContext()));

		bAsyncPSOCompileAllowed = false;
	}
};

// Helper class used internally by RHIs to make use of FRHICommandList_RecursiveHazardous safer.
// Access to the underlying context is exposed via RunOnContext() to ensure correct ordering of commands.
template <typename ContextType>
class TRHICommandList_RecursiveHazardous : public FRHICommandList_RecursiveHazardous
{
	template <typename LAMBDA>
	struct TRHILambdaCommand final : public FRHICommandBase
	{
		LAMBDA Lambda;

		TRHILambdaCommand(LAMBDA&& InLambda)
			: Lambda(Forward<LAMBDA>(InLambda))
		{}

		void ExecuteAndDestruct(FRHICommandListBase& CmdList, FRHICommandListDebugContext&) override final
		{
			// RunOnContext always requires the lowest level (platform) context, not the validation RHI context.
			Lambda(static_cast<ContextType&>(CmdList.GetContext().GetLowestLevelContext()));
			Lambda.~LAMBDA();
		}
	};

public:
	TRHICommandList_RecursiveHazardous(ContextType *Context, FRHIGPUMask GPUMask = FRHIGPUMask::All())
		: FRHICommandList_RecursiveHazardous(Context, GPUMask)
	{}

	template <typename LAMBDA>
	FORCEINLINE_DEBUGGABLE void RunOnContext(LAMBDA&& Lambda)
	{
		if (Bypass())
		{
			// RunOnContext always requires the lowest level (platform) context, not the validation RHI context.
			Lambda(static_cast<ContextType&>(GetContext().GetLowestLevelContext()));
		}
		else
		{
			ALLOC_COMMAND(TRHILambdaCommand<LAMBDA>)(Forward<LAMBDA>(Lambda));
		}
	}
};

class RHI_API FRHIComputeCommandList_RecursiveHazardous : public FRHIComputeCommandList
{
public:
	FRHIComputeCommandList_RecursiveHazardous(IRHIComputeContext *Context, FRHIGPUMask InGPUMask = FRHIGPUMask::All())
		: FRHIComputeCommandList(InGPUMask)
	{
		// Always grab the validation RHI context if active, so that the
		// validation RHI can see any RHI commands enqueued within the RHI itself.
		SetComputeContext(&Context->GetHighestLevelContext());

		bAsyncPSOCompileAllowed = false;
	}
};

template <typename ContextType>
class TRHIComputeCommandList_RecursiveHazardous : public FRHIComputeCommandList_RecursiveHazardous
{
	template <typename LAMBDA>
	struct TRHILambdaCommand final : public FRHICommandBase
	{
		LAMBDA Lambda;

		TRHILambdaCommand(LAMBDA&& InLambda)
			: Lambda(Forward<LAMBDA>(InLambda))
		{}

		void ExecuteAndDestruct(FRHICommandListBase& CmdList, FRHICommandListDebugContext&) override final
		{
			// RunOnContext always requires the lowest level (platform) context, not the validation RHI context.
			Lambda(static_cast<ContextType&>(CmdList.GetComputeContext().GetLowestLevelContext()));
			Lambda.~LAMBDA();
		}
	};

public:
	TRHIComputeCommandList_RecursiveHazardous(ContextType *Context, FRHIGPUMask GPUMask = FRHIGPUMask::All())
		: FRHIComputeCommandList_RecursiveHazardous(Context, GPUMask)
	{}

	template <typename LAMBDA>
	FORCEINLINE_DEBUGGABLE void RunOnContext(LAMBDA&& Lambda)
	{
		if (Bypass())
		{
			// RunOnContext always requires the lowest level (platform) context, not the validation RHI context.
			Lambda(static_cast<ContextType&>(GetComputeContext().GetLowestLevelContext()));
		}
		else
		{
			ALLOC_COMMAND(TRHILambdaCommand<LAMBDA>)(Forward<LAMBDA>(Lambda));
		}
	}
};

// This controls if the cmd list bypass can be toggled at runtime. It is quite expensive to have these branches in there.
#define CAN_TOGGLE_COMMAND_LIST_BYPASS (!UE_BUILD_SHIPPING && !UE_BUILD_TEST)

class RHI_API FRHICommandListExecutor
{
public:
	enum
	{
		DefaultBypass = PLATFORM_RHITHREAD_DEFAULT_BYPASS
	};
	FRHICommandListExecutor()
		: bLatchedBypass(!!DefaultBypass)
		, bLatchedUseParallelAlgorithms(false)
	{
	}
	static inline FRHICommandListImmediate& GetImmediateCommandList();
	static inline FRHIAsyncComputeCommandListImmediate& GetImmediateAsyncComputeCommandList();

	void ExecuteList(FRHICommandListBase& CmdList);
	void ExecuteList(FRHICommandListImmediate& CmdList);
	void LatchBypass();

	static void WaitOnRHIThreadFence(FGraphEventRef& Fence);

	FORCEINLINE_DEBUGGABLE bool Bypass()
	{
#if CAN_TOGGLE_COMMAND_LIST_BYPASS
		return bLatchedBypass;
#else
		return !!DefaultBypass;
#endif
	}
	FORCEINLINE_DEBUGGABLE bool UseParallelAlgorithms()
	{
#if CAN_TOGGLE_COMMAND_LIST_BYPASS
		return bLatchedUseParallelAlgorithms;
#else
		return  FApp::ShouldUseThreadingForPerformance() && !Bypass() && (GSupportsParallelRenderingTasksWithSeparateRHIThread || !IsRunningRHIInSeparateThread());
#endif
	}
	static void CheckNoOutstandingCmdLists();
	static bool IsRHIThreadActive();
	static bool IsRHIThreadCompletelyFlushed();

private:

	void ExecuteInner(FRHICommandListBase& CmdList);
	friend class FExecuteRHIThreadTask;
	static void ExecuteInner_DoExecute(FRHICommandListBase& CmdList);

	bool bLatchedBypass;
	bool bLatchedUseParallelAlgorithms;
	friend class FRHICommandListBase;
	FThreadSafeCounter UIDCounter;
	FThreadSafeCounter OutstandingCmdListCount;
	FRHICommandListImmediate CommandListImmediate;
	FRHIAsyncComputeCommandListImmediate AsyncComputeCmdListImmediate;
};

extern RHI_API FRHICommandListExecutor GRHICommandList;

extern RHI_API FAutoConsoleTaskPriority CPrio_SceneRenderingTask;

class FRenderTask
{
public:
	FORCEINLINE static ENamedThreads::Type GetDesiredThread()
	{
		return CPrio_SceneRenderingTask.Get();
	}
};


FORCEINLINE_DEBUGGABLE FRHICommandListImmediate& FRHICommandListExecutor::GetImmediateCommandList()
{
	return GRHICommandList.CommandListImmediate;
}

FORCEINLINE_DEBUGGABLE FRHIAsyncComputeCommandListImmediate& FRHICommandListExecutor::GetImmediateAsyncComputeCommandList()
{
	return GRHICommandList.AsyncComputeCmdListImmediate;
}

struct FScopedCommandListWaitForTasks
{
	FRHICommandListImmediate& RHICmdList;
	bool bWaitForTasks;

	FScopedCommandListWaitForTasks(bool InbWaitForTasks, FRHICommandListImmediate& InRHICmdList = FRHICommandListExecutor::GetImmediateCommandList())
		: RHICmdList(InRHICmdList)
		, bWaitForTasks(InbWaitForTasks)
	{
	}
	RHI_API ~FScopedCommandListWaitForTasks();
};


FORCEINLINE FPixelShaderRHIRef RHICreatePixelShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreatePixelShader(Code, Hash);
}

FORCEINLINE FVertexShaderRHIRef RHICreateVertexShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateVertexShader(Code, Hash);
}

FORCEINLINE FHullShaderRHIRef RHICreateHullShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateHullShader(Code, Hash);
}

FORCEINLINE FDomainShaderRHIRef RHICreateDomainShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateDomainShader(Code, Hash);
}

FORCEINLINE FGeometryShaderRHIRef RHICreateGeometryShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateGeometryShader(Code, Hash);
}

FORCEINLINE FComputeShaderRHIRef RHICreateComputeShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateComputeShader(Code, Hash);
}

FORCEINLINE FComputeFenceRHIRef RHICreateComputeFence(const FName& Name)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateComputeFence(Name);
}

FORCEINLINE FGPUFenceRHIRef RHICreateGPUFence(const FName& Name)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateGPUFence(Name);
}

FORCEINLINE FStagingBufferRHIRef RHICreateStagingBuffer()
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateStagingBuffer();
}

FORCEINLINE FIndexBufferRHIRef RHICreateAndLockIndexBuffer(uint32 Stride, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo, void*& OutDataBuffer)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateAndLockIndexBuffer(Stride, Size, InUsage, CreateInfo, OutDataBuffer);
}

FORCEINLINE FIndexBufferRHIRef RHICreateIndexBuffer(uint32 Stride, uint32 Size, uint32 InUsage, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo)
{
	return GDynamicRHI->CreateIndexBuffer_RenderThread(FRHICommandListExecutor::GetImmediateCommandList(), Stride, Size, InUsage, InResourceState, CreateInfo);
}

FORCEINLINE FIndexBufferRHIRef RHIAsyncCreateIndexBuffer(uint32 Stride, uint32 Size, uint32 InUsage, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo)
{
	return GDynamicRHI->RHICreateIndexBuffer(Stride, Size, InUsage, InResourceState, CreateInfo);
}

FORCEINLINE FIndexBufferRHIRef RHICreateIndexBuffer(uint32 Stride, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo)
{
	bool bHasInitialData = CreateInfo.BulkData != nullptr;
	ERHIAccess ResourceState = RHIGetDefaultResourceState((EBufferUsageFlags)InUsage | BUF_IndexBuffer, bHasInitialData);
	return RHICreateIndexBuffer(Stride, Size, InUsage, ResourceState, CreateInfo);
}

FORCEINLINE FIndexBufferRHIRef RHIAsyncCreateIndexBuffer(uint32 Stride, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo)
{
	bool bHasInitialData = CreateInfo.BulkData != nullptr;
	ERHIAccess ResourceState = RHIGetDefaultResourceState((EBufferUsageFlags)InUsage | BUF_IndexBuffer, bHasInitialData);
	return RHIAsyncCreateIndexBuffer(Stride, Size, InUsage, ResourceState, CreateInfo);
}

FORCEINLINE void* RHILockIndexBuffer(FRHIIndexBuffer* IndexBuffer, uint32 Offset, uint32 Size, EResourceLockMode LockMode)
{
	return FRHICommandListExecutor::GetImmediateCommandList().LockIndexBuffer(IndexBuffer, Offset, Size, LockMode);
}

FORCEINLINE void RHIUnlockIndexBuffer(FRHIIndexBuffer* IndexBuffer)
{
	 FRHICommandListExecutor::GetImmediateCommandList().UnlockIndexBuffer(IndexBuffer);
}

FORCEINLINE FVertexBufferRHIRef RHICreateAndLockVertexBuffer(uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo, void*& OutDataBuffer)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateAndLockVertexBuffer(Size, InUsage, CreateInfo, OutDataBuffer);
}

FORCEINLINE FVertexBufferRHIRef RHICreateVertexBuffer(uint32 Size, uint32 InUsage, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo)
{
	return GDynamicRHI->CreateVertexBuffer_RenderThread(FRHICommandListExecutor::GetImmediateCommandList(), Size, InUsage, InResourceState, CreateInfo);
}

FORCEINLINE FVertexBufferRHIRef RHIAsyncCreateVertexBuffer(uint32 Size, uint32 InUsage, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo)
{
	return GDynamicRHI->RHICreateVertexBuffer(Size, InUsage, InResourceState, CreateInfo);
}

FORCEINLINE FVertexBufferRHIRef RHICreateVertexBuffer(uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo)
{
	bool bHasInitialData = CreateInfo.BulkData != nullptr;
	ERHIAccess ResourceState = RHIGetDefaultResourceState((EBufferUsageFlags)InUsage | BUF_VertexBuffer, bHasInitialData);
	return RHICreateVertexBuffer(Size, InUsage, ResourceState, CreateInfo);
}

FORCEINLINE FVertexBufferRHIRef RHIAsyncCreateVertexBuffer(uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo)
{
	bool bHasInitialData = CreateInfo.BulkData != nullptr;
	ERHIAccess ResourceState = RHIGetDefaultResourceState((EBufferUsageFlags)InUsage | BUF_VertexBuffer, bHasInitialData);
	return RHIAsyncCreateVertexBuffer(Size, InUsage, ResourceState, CreateInfo);
}

FORCEINLINE void* RHILockVertexBuffer(FRHIVertexBuffer* VertexBuffer, uint32 Offset, uint32 SizeRHI, EResourceLockMode LockMode)
{
	return FRHICommandListExecutor::GetImmediateCommandList().LockVertexBuffer(VertexBuffer, Offset, SizeRHI, LockMode);
}

FORCEINLINE void RHIUnlockVertexBuffer(FRHIVertexBuffer* VertexBuffer)
{
	 FRHICommandListExecutor::GetImmediateCommandList().UnlockVertexBuffer(VertexBuffer);
}

FORCEINLINE FStructuredBufferRHIRef RHICreateStructuredBuffer(uint32 Stride, uint32 Size, uint32 InUsage, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo)
{
	return GDynamicRHI->CreateStructuredBuffer_RenderThread(FRHICommandListExecutor::GetImmediateCommandList(), Stride, Size, InUsage, InResourceState, CreateInfo);
}

FORCEINLINE FStructuredBufferRHIRef RHICreateStructuredBuffer(uint32 Stride, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo)
{
	bool bHasInitialData = CreateInfo.BulkData != nullptr;
	ERHIAccess ResourceState = RHIGetDefaultResourceState((EBufferUsageFlags)InUsage | BUF_StructuredBuffer, bHasInitialData);
	return RHICreateStructuredBuffer(Stride, Size, InUsage, ResourceState, CreateInfo);
}

FORCEINLINE void* RHILockStructuredBuffer(FRHIStructuredBuffer* StructuredBuffer, uint32 Offset, uint32 SizeRHI, EResourceLockMode LockMode)
{
	return FRHICommandListExecutor::GetImmediateCommandList().LockStructuredBuffer(StructuredBuffer, Offset, SizeRHI, LockMode);
}

FORCEINLINE void RHIUnlockStructuredBuffer(FRHIStructuredBuffer* StructuredBuffer)
{
	 FRHICommandListExecutor::GetImmediateCommandList().UnlockStructuredBuffer(StructuredBuffer);
}

FORCEINLINE FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView(FRHIStructuredBuffer* StructuredBuffer, bool bUseUAVCounter, bool bAppendBuffer)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateUnorderedAccessView(StructuredBuffer, bUseUAVCounter, bAppendBuffer);
}

FORCEINLINE FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView(FRHITexture* Texture, uint32 MipLevel = 0)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateUnorderedAccessView(Texture, MipLevel);
}

FORCEINLINE FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView(FRHITexture* Texture, uint32 MipLevel, uint8 Format)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateUnorderedAccessView(Texture, MipLevel, Format);
}

FORCEINLINE FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView(FRHIVertexBuffer* VertexBuffer, uint8 Format)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateUnorderedAccessView(VertexBuffer, Format);
}

FORCEINLINE FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView(FRHIIndexBuffer* IndexBuffer, uint8 Format)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateUnorderedAccessView(IndexBuffer, Format);
}

FORCEINLINE FShaderResourceViewRHIRef RHICreateShaderResourceView(FRHIStructuredBuffer* StructuredBuffer)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateShaderResourceView(StructuredBuffer);
}

FORCEINLINE FShaderResourceViewRHIRef RHICreateShaderResourceView(FRHIVertexBuffer* VertexBuffer, uint32 Stride, uint8 Format)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateShaderResourceView(VertexBuffer, Stride, Format);
}

FORCEINLINE FShaderResourceViewRHIRef RHICreateShaderResourceView(const FShaderResourceViewInitializer& Initializer)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateShaderResourceView(Initializer);
}

FORCEINLINE FShaderResourceViewRHIRef RHICreateShaderResourceView(FRHIIndexBuffer* Buffer)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateShaderResourceView(Buffer);
}

FORCEINLINE void RHIUpdateRHIResources(FRHIResourceUpdateInfo* UpdateInfos, int32 Num, bool bNeedReleaseRefs)
{
	return FRHICommandListExecutor::GetImmediateCommandList().UpdateRHIResources(UpdateInfos, Num, bNeedReleaseRefs);
}

FORCEINLINE FTextureReferenceRHIRef RHICreateTextureReference(FLastRenderTimeContainer* LastRenderTime)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateTextureReference(LastRenderTime);
}

FORCEINLINE void RHIUpdateTextureReference(FRHITextureReference* TextureRef, FRHITexture* NewTexture)
{
	 FRHICommandListExecutor::GetImmediateCommandList().UpdateTextureReference(TextureRef, NewTexture);
}

FORCEINLINE FTexture2DRHIRef RHICreateTexture2D(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 NumSamples, ETextureCreateFlags Flags, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo)
{
	LLM_SCOPE((Flags & (TexCreate_RenderTargetable | TexCreate_DepthStencilTargetable)) != 0 ? ELLMTag::RenderTargets : ELLMTag::Textures);
	return GDynamicRHI->RHICreateTexture2D_RenderThread(FRHICommandListExecutor::GetImmediateCommandList(), SizeX, SizeY, Format, NumMips, NumSamples, Flags, InResourceState, CreateInfo);
}

FORCEINLINE FTexture2DRHIRef RHICreateTextureExternal2D(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 NumSamples, ETextureCreateFlags Flags, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo)
{
	LLM_SCOPE((Flags & (TexCreate_RenderTargetable | TexCreate_DepthStencilTargetable)) != 0 ? ELLMTag::RenderTargets : ELLMTag::Textures);
	return GDynamicRHI->RHICreateTextureExternal2D_RenderThread(FRHICommandListExecutor::GetImmediateCommandList(), SizeX, SizeY, Format, NumMips, NumSamples, Flags, InResourceState, CreateInfo);
}

FORCEINLINE FTexture2DRHIRef RHIAsyncCreateTexture2D(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, ERHIAccess InResourceState, void** InitialMipData, uint32 NumInitialMips)
{
	LLM_SCOPE((Flags & (TexCreate_RenderTargetable | TexCreate_DepthStencilTargetable)) != 0 ? ELLMTag::RenderTargets : ELLMTag::Textures);
	return GDynamicRHI->RHIAsyncCreateTexture2D(SizeX, SizeY, Format, NumMips, Flags, InResourceState, InitialMipData, NumInitialMips);
}

FORCEINLINE FTexture2DRHIRef RHICreateTexture2D(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 NumSamples, ETextureCreateFlags Flags, FRHIResourceCreateInfo& CreateInfo)
{
	bool bHasInitialData = CreateInfo.BulkData != nullptr;
	ERHIAccess ResourceState = RHIGetDefaultResourceState((ETextureCreateFlags)Flags, bHasInitialData);
	return RHICreateTexture2D(SizeX, SizeY, Format, NumMips, NumSamples, Flags, ResourceState, CreateInfo);
}

FORCEINLINE FTexture2DRHIRef RHICreateTextureExternal2D(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 NumSamples, ETextureCreateFlags Flags, FRHIResourceCreateInfo& CreateInfo)
{
	bool bHasInitialData = CreateInfo.BulkData != nullptr;
	ERHIAccess ResourceState = RHIGetDefaultResourceState((ETextureCreateFlags)Flags, bHasInitialData);
	return RHICreateTextureExternal2D(SizeX, SizeY, Format, NumMips, NumSamples, Flags, ResourceState, CreateInfo);
}

FORCEINLINE FTexture2DRHIRef RHIAsyncCreateTexture2D(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, void** InitialMipData, uint32 NumInitialMips)
{
	bool bHasInitialData = InitialMipData != nullptr;
	ERHIAccess ResourceState = RHIGetDefaultResourceState((ETextureCreateFlags)Flags, bHasInitialData);
	return RHIAsyncCreateTexture2D(SizeX, SizeY, Format, NumMips, Flags, ResourceState, InitialMipData, NumInitialMips);
}

FORCEINLINE void RHICopySharedMips(FRHITexture2D* DestTexture2D, FRHITexture2D* SrcTexture2D)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CopySharedMips(DestTexture2D, SrcTexture2D);
}

FORCEINLINE FTexture2DArrayRHIRef RHICreateTexture2DArray(uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint8 Format, uint32 NumMips, uint32 NumSamples, ETextureCreateFlags Flags, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo)
{
	LLM_SCOPE((Flags & (TexCreate_RenderTargetable | TexCreate_DepthStencilTargetable)) != 0 ? ELLMTag::RenderTargets : ELLMTag::Textures);
	return GDynamicRHI->RHICreateTexture2DArray_RenderThread(FRHICommandListExecutor::GetImmediateCommandList(), SizeX, SizeY, SizeZ, Format, NumMips, NumSamples, Flags, InResourceState, CreateInfo);
}

FORCEINLINE FTexture2DArrayRHIRef RHICreateTexture2DArray(uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint8 Format, uint32 NumMips, uint32 NumSamples, ETextureCreateFlags Flags, FRHIResourceCreateInfo& CreateInfo)
{
	bool bHasInitialData = CreateInfo.BulkData != nullptr;
	ERHIAccess ResourceState = RHIGetDefaultResourceState((ETextureCreateFlags)Flags, bHasInitialData);
	return RHICreateTexture2DArray(SizeX, SizeY, SizeZ, Format, NumMips, NumSamples, Flags, ResourceState, CreateInfo);
}

FORCEINLINE FTexture3DRHIRef RHICreateTexture3D(uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, ERHIAccess ResourceState, FRHIResourceCreateInfo& CreateInfo)
{
	LLM_SCOPE((Flags & (TexCreate_RenderTargetable | TexCreate_DepthStencilTargetable)) != 0 ? ELLMTag::RenderTargets : ELLMTag::Textures);
	return GDynamicRHI->RHICreateTexture3D_RenderThread(FRHICommandListExecutor::GetImmediateCommandList(), SizeX, SizeY, SizeZ, Format, NumMips, Flags, ResourceState, CreateInfo);
}

FORCEINLINE FTexture3DRHIRef RHICreateTexture3D(uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, FRHIResourceCreateInfo& CreateInfo)
{
	bool bHasInitialData = CreateInfo.BulkData != nullptr;
	ERHIAccess ResourceState = RHIGetDefaultResourceState((ETextureCreateFlags)Flags, bHasInitialData);
	return RHICreateTexture3D(SizeX, SizeY, SizeZ, Format, NumMips, Flags, ResourceState, CreateInfo);
}

FORCEINLINE FShaderResourceViewRHIRef RHICreateShaderResourceView(FRHITexture* Texture, uint8 MipLevel)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateShaderResourceView(Texture, MipLevel);
}

FORCEINLINE FShaderResourceViewRHIRef RHICreateShaderResourceView(FRHITexture* Texture, uint8 MipLevel, uint8 NumMipLevels, uint8 Format)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateShaderResourceView(Texture, MipLevel, NumMipLevels, Format);
}

FORCEINLINE FShaderResourceViewRHIRef RHICreateShaderResourceView(FRHITexture* Texture, const FRHITextureSRVCreateInfo& CreateInfo)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateShaderResourceView(Texture, CreateInfo);
}

FORCEINLINE FShaderResourceViewRHIRef RHICreateShaderResourceViewWriteMask(FRHITexture2D* Texture2D)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateShaderResourceViewWriteMask(Texture2D);
}

FORCEINLINE FShaderResourceViewRHIRef RHICreateShaderResourceViewFMask(FRHITexture2D* Texture2D)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateShaderResourceViewFMask(Texture2D);
}

FORCEINLINE FTexture2DRHIRef RHIAsyncReallocateTexture2D(FRHITexture2D* Texture2D, int32 NewMipCount, int32 NewSizeX, int32 NewSizeY, FThreadSafeCounter* RequestStatus)
{
	return FRHICommandListExecutor::GetImmediateCommandList().AsyncReallocateTexture2D(Texture2D, NewMipCount, NewSizeX, NewSizeY, RequestStatus);
}

FORCEINLINE ETextureReallocationStatus RHIFinalizeAsyncReallocateTexture2D(FRHITexture2D* Texture2D, bool bBlockUntilCompleted)
{
	return FRHICommandListExecutor::GetImmediateCommandList().FinalizeAsyncReallocateTexture2D(Texture2D, bBlockUntilCompleted);
}

FORCEINLINE ETextureReallocationStatus RHICancelAsyncReallocateTexture2D(FRHITexture2D* Texture2D, bool bBlockUntilCompleted)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CancelAsyncReallocateTexture2D(Texture2D, bBlockUntilCompleted);
}

FORCEINLINE void* RHILockTexture2D(FRHITexture2D* Texture, uint32 MipIndex, EResourceLockMode LockMode, uint32& DestStride, bool bLockWithinMiptail, bool bFlushRHIThread = true)
{
	return FRHICommandListExecutor::GetImmediateCommandList().LockTexture2D(Texture, MipIndex, LockMode, DestStride, bLockWithinMiptail, bFlushRHIThread);
}

FORCEINLINE void RHIUnlockTexture2D(FRHITexture2D* Texture, uint32 MipIndex, bool bLockWithinMiptail, bool bFlushRHIThread = true)
{
	 FRHICommandListExecutor::GetImmediateCommandList().UnlockTexture2D(Texture, MipIndex, bLockWithinMiptail, bFlushRHIThread);
}

FORCEINLINE void* RHILockTexture2DArray(FRHITexture2DArray* Texture, uint32 TextureIndex, uint32 MipIndex, EResourceLockMode LockMode, uint32& DestStride, bool bLockWithinMiptail)
{
	return FRHICommandListExecutor::GetImmediateCommandList().LockTexture2DArray(Texture, TextureIndex, MipIndex, LockMode, DestStride, bLockWithinMiptail);
}

FORCEINLINE void RHIUnlockTexture2DArray(FRHITexture2DArray* Texture, uint32 TextureIndex, uint32 MipIndex, bool bLockWithinMiptail)
{
	 FRHICommandListExecutor::GetImmediateCommandList().UnlockTexture2DArray(Texture, TextureIndex, MipIndex, bLockWithinMiptail);
}

FORCEINLINE void RHIUpdateTexture2D(FRHITexture2D* Texture, uint32 MipIndex, const struct FUpdateTextureRegion2D& UpdateRegion, uint32 SourcePitch, const uint8* SourceData)
{
	 FRHICommandListExecutor::GetImmediateCommandList().UpdateTexture2D(Texture, MipIndex, UpdateRegion, SourcePitch, SourceData);
}

FORCEINLINE FUpdateTexture3DData RHIBeginUpdateTexture3D(FRHITexture3D* Texture, uint32 MipIndex, const struct FUpdateTextureRegion3D& UpdateRegion)
{
	return FRHICommandListExecutor::GetImmediateCommandList().BeginUpdateTexture3D(Texture, MipIndex, UpdateRegion);
}

FORCEINLINE void RHIEndUpdateTexture3D(FUpdateTexture3DData& UpdateData)
{
	FRHICommandListExecutor::GetImmediateCommandList().EndUpdateTexture3D(UpdateData);
}

FORCEINLINE void RHIEndMultiUpdateTexture3D(TArray<FUpdateTexture3DData>& UpdateDataArray)
{
	FRHICommandListExecutor::GetImmediateCommandList().EndMultiUpdateTexture3D(UpdateDataArray);
}

FORCEINLINE void RHIUpdateTexture3D(FRHITexture3D* Texture, uint32 MipIndex, const struct FUpdateTextureRegion3D& UpdateRegion, uint32 SourceRowPitch, uint32 SourceDepthPitch, const uint8* SourceData)
{
	 FRHICommandListExecutor::GetImmediateCommandList().UpdateTexture3D(Texture, MipIndex, UpdateRegion, SourceRowPitch, SourceDepthPitch, SourceData);
}

FORCEINLINE FTextureCubeRHIRef RHICreateTextureCube(uint32 Size, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo)
{
	LLM_SCOPE((Flags & (TexCreate_RenderTargetable | TexCreate_DepthStencilTargetable)) != 0 ? ELLMTag::RenderTargets : ELLMTag::Textures);
	return GDynamicRHI->RHICreateTextureCube_RenderThread(FRHICommandListExecutor::GetImmediateCommandList(), Size, Format, NumMips, Flags, InResourceState, CreateInfo);
}

FORCEINLINE FTextureCubeRHIRef RHICreateTextureCubeArray(uint32 Size, uint32 ArraySize, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo)
{
	LLM_SCOPE((Flags & (TexCreate_RenderTargetable | TexCreate_DepthStencilTargetable)) != 0 ? ELLMTag::RenderTargets : ELLMTag::Textures);
	return GDynamicRHI->RHICreateTextureCubeArray_RenderThread(FRHICommandListExecutor::GetImmediateCommandList(), Size, ArraySize, Format, NumMips, Flags, InResourceState, CreateInfo);
}

FORCEINLINE FTextureCubeRHIRef RHICreateTextureCube(uint32 Size, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, FRHIResourceCreateInfo& CreateInfo)
{
	bool bHasInitialData = CreateInfo.BulkData != nullptr;
	ERHIAccess ResourceState = RHIGetDefaultResourceState((ETextureCreateFlags)Flags, bHasInitialData);
	return RHICreateTextureCube(Size, Format, NumMips, Flags, ResourceState, CreateInfo);
}

FORCEINLINE FTextureCubeRHIRef RHICreateTextureCubeArray(uint32 Size, uint32 ArraySize, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, FRHIResourceCreateInfo& CreateInfo)
{
	bool bHasInitialData = CreateInfo.BulkData != nullptr;
	ERHIAccess ResourceState = RHIGetDefaultResourceState((ETextureCreateFlags)Flags, bHasInitialData);
	return RHICreateTextureCubeArray(Size, ArraySize, Format, NumMips, Flags, ResourceState, CreateInfo);
}

FORCEINLINE void* RHILockTextureCubeFace(FRHITextureCube* Texture, uint32 FaceIndex, uint32 ArrayIndex, uint32 MipIndex, EResourceLockMode LockMode, uint32& DestStride, bool bLockWithinMiptail)
{
	return FRHICommandListExecutor::GetImmediateCommandList().LockTextureCubeFace(Texture, FaceIndex, ArrayIndex, MipIndex, LockMode, DestStride, bLockWithinMiptail);
}

FORCEINLINE void RHIUnlockTextureCubeFace(FRHITextureCube* Texture, uint32 FaceIndex, uint32 ArrayIndex, uint32 MipIndex, bool bLockWithinMiptail)
{
	 FRHICommandListExecutor::GetImmediateCommandList().UnlockTextureCubeFace(Texture, FaceIndex, ArrayIndex, MipIndex, bLockWithinMiptail);
}

UE_DEPRECATED(4.23, "CreateRenderQuery API is deprecated; use RHICreateRenderQueryPool and suballocate queries there")
FORCEINLINE FRenderQueryRHIRef RHICreateRenderQuery(ERenderQueryType QueryType)
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return FRHICommandListExecutor::GetImmediateCommandList().CreateRenderQuery_RenderThread(QueryType);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

FORCEINLINE void RHIAcquireTransientResource(FRHITexture* Resource)
{
	FRHICommandListExecutor::GetImmediateCommandList().AcquireTransientResource_RenderThread(Resource);
}

FORCEINLINE void RHIDiscardTransientResource(FRHITexture* Resource)
{
	FRHICommandListExecutor::GetImmediateCommandList().DiscardTransientResource_RenderThread(Resource);
}

FORCEINLINE void RHIAcquireTransientResource(FRHIVertexBuffer* Resource)
{
	FRHICommandListExecutor::GetImmediateCommandList().AcquireTransientResource_RenderThread(Resource);
}

FORCEINLINE void RHIDiscardTransientResource(FRHIVertexBuffer* Resource)
{
	FRHICommandListExecutor::GetImmediateCommandList().DiscardTransientResource_RenderThread(Resource);
}

FORCEINLINE void RHIAcquireTransientResource(FRHIStructuredBuffer* Resource)
{
	FRHICommandListExecutor::GetImmediateCommandList().AcquireTransientResource_RenderThread(Resource);
}

FORCEINLINE void RHIDiscardTransientResource(FRHIStructuredBuffer* Resource)
{
	FRHICommandListExecutor::GetImmediateCommandList().DiscardTransientResource_RenderThread(Resource);
}

FORCEINLINE void RHIAcquireThreadOwnership()
{
	return FRHICommandListExecutor::GetImmediateCommandList().AcquireThreadOwnership();
}

FORCEINLINE void RHIReleaseThreadOwnership()
{
	return FRHICommandListExecutor::GetImmediateCommandList().ReleaseThreadOwnership();
}

FORCEINLINE void RHIFlushResources()
{
	return FRHICommandListExecutor::GetImmediateCommandList().FlushResources();
}

FORCEINLINE void RHIVirtualTextureSetFirstMipInMemory(FRHITexture2D* Texture, uint32 FirstMip)
{
	 FRHICommandListExecutor::GetImmediateCommandList().VirtualTextureSetFirstMipInMemory(Texture, FirstMip);
}

FORCEINLINE void RHIVirtualTextureSetFirstMipVisible(FRHITexture2D* Texture, uint32 FirstMip)
{
	 FRHICommandListExecutor::GetImmediateCommandList().VirtualTextureSetFirstMipVisible(Texture, FirstMip);
}

FORCEINLINE void RHIExecuteCommandList(FRHICommandList* CmdList)
{
	 FRHICommandListExecutor::GetImmediateCommandList().ExecuteCommandList(CmdList);
}

FORCEINLINE void* RHIGetNativeDevice()
{
	return FRHICommandListExecutor::GetImmediateCommandList().GetNativeDevice();
}

FORCEINLINE void* RHIGetNativePhysicalDevice()
{
	return FRHICommandListExecutor::GetImmediateCommandList().GetNativePhysicalDevice();
}

FORCEINLINE void* RHIGetNativeGraphicsQueue()
{
	return FRHICommandListExecutor::GetImmediateCommandList().GetNativeGraphicsQueue();
}

FORCEINLINE void* RHIGetNativeComputeQueue()
{
	return FRHICommandListExecutor::GetImmediateCommandList().GetNativeComputeQueue();
}

FORCEINLINE void* RHIGetNativeInstance()
{
	return FRHICommandListExecutor::GetImmediateCommandList().GetNativeInstance();
}

FORCEINLINE void* RHIGetNativeCommandBuffer()
{
	return FRHICommandListExecutor::GetImmediateCommandList().GetNativeCommandBuffer();
}

FORCEINLINE FRHIShaderLibraryRef RHICreateShaderLibrary(EShaderPlatform Platform, FString const& FilePath, FString const& Name)
{
    return GDynamicRHI->RHICreateShaderLibrary(Platform, FilePath, Name);
}

FORCEINLINE void* RHILockStagingBuffer(FRHIStagingBuffer* StagingBuffer, uint32 Offset, uint32 Size)
{
	return FRHICommandListExecutor::GetImmediateCommandList().LockStagingBuffer(StagingBuffer, nullptr, Offset, Size);
}

FORCEINLINE void* RHILockStagingBuffer(FRHIStagingBuffer* StagingBuffer, FRHIGPUFence* Fence, uint32 Offset, uint32 Size)
{
	return FRHICommandListExecutor::GetImmediateCommandList().LockStagingBuffer(StagingBuffer, Fence, Offset, Size);
}

FORCEINLINE void RHIUnlockStagingBuffer(FRHIStagingBuffer* StagingBuffer)
{
	 FRHICommandListExecutor::GetImmediateCommandList().UnlockStagingBuffer(StagingBuffer);
}

template <uint32 MaxNumUpdates>
struct TRHIResourceUpdateBatcher
{
	FRHIResourceUpdateInfo UpdateInfos[MaxNumUpdates];
	uint32 NumBatched;

	TRHIResourceUpdateBatcher()
		: NumBatched(0)
	{}

	~TRHIResourceUpdateBatcher()
	{
		Flush();
	}

	void Flush()
	{
		if (NumBatched > 0)
		{
			RHIUpdateRHIResources(UpdateInfos, NumBatched, true);
			NumBatched = 0;
		}
	}

	void QueueUpdateRequest(FRHIVertexBuffer* DestVertexBuffer, FRHIVertexBuffer* SrcVertexBuffer)
	{
		FRHIResourceUpdateInfo& UpdateInfo = GetNextUpdateInfo();
		UpdateInfo.Type = FRHIResourceUpdateInfo::UT_VertexBuffer;
		UpdateInfo.VertexBuffer = { DestVertexBuffer, SrcVertexBuffer };
		DestVertexBuffer->AddRef();
		if (SrcVertexBuffer)
		{
			SrcVertexBuffer->AddRef();
		}
	}

	void QueueUpdateRequest(FRHIIndexBuffer* DestIndexBuffer, FRHIIndexBuffer* SrcIndexBuffer)
	{
		FRHIResourceUpdateInfo& UpdateInfo = GetNextUpdateInfo();
		UpdateInfo.Type = FRHIResourceUpdateInfo::UT_IndexBuffer;
		UpdateInfo.IndexBuffer = { DestIndexBuffer, SrcIndexBuffer };
		DestIndexBuffer->AddRef();
		if (SrcIndexBuffer)
		{
			SrcIndexBuffer->AddRef();
		}
	}

	void QueueUpdateRequest(FRHIShaderResourceView* SRV, FRHIVertexBuffer* VertexBuffer, uint32 Stride, uint8 Format)
	{
		FRHIResourceUpdateInfo& UpdateInfo = GetNextUpdateInfo();
		UpdateInfo.Type = FRHIResourceUpdateInfo::UT_VertexBufferSRV;
		UpdateInfo.VertexBufferSRV = { SRV, VertexBuffer, Stride, Format };
		SRV->AddRef();
		if (VertexBuffer)
		{
			VertexBuffer->AddRef();
		}
	}

	void QueueUpdateRequest(FRHIShaderResourceView* SRV, FRHIIndexBuffer* IndexBuffer)
	{
		FRHIResourceUpdateInfo& UpdateInfo = GetNextUpdateInfo();
		UpdateInfo.Type = FRHIResourceUpdateInfo::UT_IndexBufferSRV;
		UpdateInfo.IndexBufferSRV = { SRV, IndexBuffer };
		SRV->AddRef();
		if (IndexBuffer)
		{
			IndexBuffer->AddRef();
		}
	}

private:
	FRHIResourceUpdateInfo & GetNextUpdateInfo()
	{
		check(NumBatched <= MaxNumUpdates);
		if (NumBatched >= MaxNumUpdates)
		{
			Flush();
		}
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 6385) // Access is always in-bound due to the Flush above
#endif
		return UpdateInfos[NumBatched++];
#ifdef _MSC_VER
#pragma warning(pop)
#endif
	}
};

#undef RHICOMMAND_CALLSTACK

#include "RHICommandList.inl"
