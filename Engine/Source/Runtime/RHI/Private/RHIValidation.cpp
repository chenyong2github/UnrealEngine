// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RHIValidation.cpp: Public RHI Validation layer definitions.
=============================================================================*/

#include "RHIValidation.h"
#include "RHIValidationContext.h"
#include "HAL/PlatformStackWalk.h"
#include "Misc/OutputDeviceRedirector.h"

#if ENABLE_RHI_VALIDATION

namespace RHIValidation
{
	int32 GBreakOnTransitionError = 1;
	FAutoConsoleVariableRef CVarBreakOnTransitionError(
		TEXT("r.RHIValidation.DebugBreak.Transitions"),
		GBreakOnTransitionError,
		TEXT("Controls whether the debugger should break when a validation error is encountered.\n")
		TEXT(" 0: disabled;\n")
		TEXT(" 1: break in the debugger if a validation error is encountered."),
		ECVF_RenderThreadSafe);
}

bool GRHIValidationEnabled = false;

// When set to 1, callstack for each uniform buffer allocation will be tracked 
// (slow and leaks memory, but can be handy to find the location where an invalid
// allocation has been made)
#define CAPTURE_UNIFORMBUFFER_ALLOCATION_BACKTRACES 0

// When set to 1, logs resource transitions on all unnamed resources, useful for
// tracking down missing barriers when GAutoLogResourceNames cannot be used.
// Don't leave this enabled. Log backtraces are leaked.
#define LOG_UNNAMED_RESOURCES 0

static const TCHAR* GAutoLogResourceNames[] =
{
	//
	// Add resource names here to automatically enable barrier logging for those resources.
	// e.g.
	//  TEXT("SceneDepthZ"),
	//

	// ----------------------
	// Add names above this line
	nullptr
};

TSet<uint32> FValidationRHI::SeenFailureHashes;
FCriticalSection FValidationRHI::SeenFailureHashesMutex;

FValidationRHI::FValidationRHI(FDynamicRHI* InRHI)
	: RHI(InRHI)
{
	check(RHI);
	UE_LOG(LogRHI, Warning, TEXT("FValidationRHI on, intercepting %s RHI!"), InRHI && InRHI->GetName() ? InRHI->GetName() : TEXT("<NULL>"));
	GRHIValidationEnabled = true;
	SeenFailureHashes.Reserve(256);
}

FValidationRHI::~FValidationRHI()
{
	GRHIValidationEnabled = false;
}

IRHICommandContext* FValidationRHI::RHIGetDefaultContext()
{
	IRHICommandContext* LowLevelContext = RHI->RHIGetDefaultContext();
	IRHICommandContext* HighLevelContext = static_cast<IRHICommandContext*>(&LowLevelContext->GetHighestLevelContext());

	if (LowLevelContext == HighLevelContext)
	{
		FValidationContext* ValidationContext = new FValidationContext();
		OwnedContexts.Add(ValidationContext);

		ValidationContext->LinkToContext(LowLevelContext);
		HighLevelContext = ValidationContext;
	}

	return HighLevelContext;
}

IRHIComputeContext* FValidationRHI::RHIGetDefaultAsyncComputeContext()
{
	IRHIComputeContext* LowLevelContext = RHI->RHIGetDefaultAsyncComputeContext();
	IRHIComputeContext* HighLevelContext = &LowLevelContext->GetHighestLevelContext();

	if (LowLevelContext == HighLevelContext)
	{
		FValidationComputeContext* ValidationContext = new FValidationComputeContext();
		OwnedContexts.Add(ValidationContext);

		ValidationContext->LinkToContext(LowLevelContext);
		HighLevelContext = ValidationContext;
	}

	return HighLevelContext;
}

void FValidationRHI::ValidatePipeline(const FGraphicsPipelineStateInitializer& PSOInitializer)
{
	{
		// Verify depth/stencil access/usage
		bool bHasDepth = IsDepthOrStencilFormat(PSOInitializer.DepthStencilTargetFormat);
		bool bHasStencil = IsStencilFormat(PSOInitializer.DepthStencilTargetFormat);
		const FDepthStencilStateInitializerRHI& Initializer = DepthStencilStates.FindChecked(PSOInitializer.DepthStencilState);
		if (bHasDepth)
		{
			if (!bHasStencil)
			{
				RHI_VALIDATION_CHECK(!Initializer.bEnableFrontFaceStencil
					&& Initializer.FrontFaceStencilTest == CF_Always
					&& Initializer.FrontFaceStencilFailStencilOp == SO_Keep
					&& Initializer.FrontFaceDepthFailStencilOp == SO_Keep
					&& Initializer.FrontFacePassStencilOp == SO_Keep
					&& !Initializer.bEnableBackFaceStencil
					&& Initializer.BackFaceStencilTest == CF_Always
					&& Initializer.BackFaceStencilFailStencilOp == SO_Keep
					&& Initializer.BackFaceDepthFailStencilOp == SO_Keep
					&& Initializer.BackFacePassStencilOp == SO_Keep, TEXT("No stencil render target set, yet PSO wants to use stencil operations!"));
/*
				RHI_VALIDATION_CHECK(PSOInitializer.StencilTargetLoadAction == ERenderTargetLoadAction::ENoAction,
					TEXT("No stencil target set, yet PSO wants to load from it!"));
				RHI_VALIDATION_CHECK(PSOInitializer.StencilTargetStoreAction == ERenderTargetStoreAction::ENoAction,
					TEXT("No stencil target set, yet PSO wants to store into it!"));
*/
			}
		}
		else
		{
			RHI_VALIDATION_CHECK(!Initializer.bEnableDepthWrite && Initializer.DepthTest == CF_Always, TEXT("No depth render target set, yet PSO wants to use depth operations!"));
			RHI_VALIDATION_CHECK(PSOInitializer.DepthTargetLoadAction == ERenderTargetLoadAction::ENoAction
				&& PSOInitializer.StencilTargetLoadAction == ERenderTargetLoadAction::ENoAction,
				TEXT("No depth/stencil target set, yet PSO wants to load from it!"));
			RHI_VALIDATION_CHECK(PSOInitializer.DepthTargetStoreAction == ERenderTargetStoreAction::ENoAction
				&& PSOInitializer.StencilTargetStoreAction == ERenderTargetStoreAction::ENoAction,
				TEXT("No depth/stencil target set, yet PSO wants to store into it!"));
		}
	}
}

void FValidationRHI::RHICreateTransition(FRHITransition* Transition, ERHIPipeline SrcPipelines, ERHIPipeline DstPipelines, ERHICreateTransitionFlags CreateFlags, TArrayView<const FRHITransitionInfo> Infos)
{
	using namespace RHIValidation;

	struct FFenceEdge
	{
		FFence* Fence = nullptr;
		ERHIPipeline SrcPipe = ERHIPipeline::None;
		ERHIPipeline DstPipe = ERHIPipeline::None;
	};

	TArray<FFenceEdge> Fences;

	if (SrcPipelines != DstPipelines)
	{
		for (ERHIPipeline SrcPipe : GetRHIPipelines())
		{
			if (!EnumHasAnyFlags(SrcPipelines, SrcPipe))
			{
				continue;
			}

			for (ERHIPipeline DstPipe : GetRHIPipelines())
			{
				if (!EnumHasAnyFlags(DstPipelines, DstPipe) || SrcPipe == DstPipe)
				{
					continue;
				}

				FFenceEdge FenceEdge;
				FenceEdge.Fence = new FFence;
				FenceEdge.SrcPipe = SrcPipe;
				FenceEdge.DstPipe = DstPipe;
				Fences.Add(FenceEdge);
			}
		}
	}

	TArray<FOperation> BeginOps, EndOps;
	BeginOps.Reserve(Infos.Num() + Fences.Num());
	EndOps  .Reserve(Infos.Num() + Fences.Num());

	for (const FFenceEdge& FenceEdge : Fences)
	{
		EndOps.Emplace(FOperation::Wait(FenceEdge.Fence, FenceEdge.DstPipe));
	}

	bool bDoTrace = false;

	for (int32 Index = 0; Index < Infos.Num(); ++Index)
	{
		const FRHITransitionInfo& Info = Infos[Index];
		if (!Info.Resource)
			continue;

		checkf(Info.Type != FRHITransitionInfo::EType::Unknown, TEXT("FRHITransitionInfo::Type cannot be Unknown when creating a resource transition."));

		FResourceIdentity Identity;

		switch (Info.Type)
		{
		default: checkNoEntry(); // fall through
		case FRHITransitionInfo::EType::Texture:
			Identity = Info.Texture->GetTransitionIdentity(Info);
			break;

		case FRHITransitionInfo::EType::Buffer:
			Identity = Info.Buffer->GetWholeResourceIdentity();
			break;

		case FRHITransitionInfo::EType::UAV:
			Identity = Info.UAV->ViewIdentity;
			break;
		}

		// Take a backtrace of this transition creation if any of the resources it contains have logging enabled.
		bDoTrace |= (Identity.Resource->LoggingMode != RHIValidation::ELoggingMode::None);

		FState PreviousState = FState(Info.AccessBefore, SrcPipelines);
		FState NextState = FState(Info.AccessAfter, DstPipelines);

		BeginOps.Emplace(FOperation::BeginTransitionResource(Identity, PreviousState, NextState, Info.Flags, nullptr));
		EndOps  .Emplace(FOperation::EndTransitionResource(Identity, PreviousState, NextState, nullptr));
	}

	if (bDoTrace)
	{
		void* Backtrace = CaptureBacktrace();
		for (FOperation& Op : BeginOps) { Op.Data_BeginTransition.CreateBacktrace = Backtrace; }
		for (FOperation& Op : EndOps) { Op.Data_EndTransition.CreateBacktrace = Backtrace; }
	}

	for (const FFenceEdge& FenceEdge : Fences)
	{
		BeginOps.Emplace(FOperation::Signal(FenceEdge.Fence, FenceEdge.SrcPipe));
	}

	Transition->PendingOperationsBegin.Operations = MoveTemp(BeginOps);
	Transition->PendingOperationsEnd.Operations = MoveTemp(EndOps);

	return RHI->RHICreateTransition(Transition, SrcPipelines, DstPipelines, CreateFlags, Infos);
}

void FValidationRHI::ReportValidationFailure(const TCHAR* InMessage)
{
	// Report failures only once per session, since many of them will happen repeatedly. This is similar to what ensure() does, but
	// ensure() looks at the source location to determine if it's seen the error before. We want to look at the actual message, since
	// all failures of a given kind will come from the same place, but (hopefully) the error message contains the name of the resource
	// and a description of the state, so it should be unique for each failure.
	uint32 Hash = FCrc::StrCrc32<TCHAR>(InMessage);
	
	SeenFailureHashesMutex.Lock();
	bool bIsAlreadyInSet;
	SeenFailureHashes.Add(Hash, &bIsAlreadyInSet);
	SeenFailureHashesMutex.Unlock();

	if (bIsAlreadyInSet)
	{
		return;
	}

	UE_LOG(LogRHI, Error, TEXT("%s"), InMessage);

	if (FPlatformMisc::IsDebuggerPresent() && RHIValidation::GBreakOnTransitionError)
	{
		// Print the message again using the debug output function, because UE_LOG doesn't always reach
		// the VS output window before the breakpoint is triggered, despite the log flush call below.
		FPlatformMisc::LowLevelOutputDebugStringf(TEXT("%s\n"), InMessage);
		GLog->PanicFlushThreadedLogs();
		PLATFORM_BREAK();
	}
}

FValidationComputeContext::FValidationComputeContext()
	: RHIContext(nullptr)
{
	State.Reset();
	Tracker = &State.TrackerInstance;
}

void FValidationComputeContext::FState::Reset()
{
	ComputePassName.Reset();
	bComputeShaderSet = false;
	TrackerInstance.ResetAllUAVState();
	GlobalUniformBuffers.Reset();
}

FValidationContext::FValidationContext()
	: RHIContext(nullptr)
{
	State.Reset();
	Tracker = &State.TrackerInstance;
}

void FValidationContext::RHIEndFrame()
{
	RHIContext->RHIEndFrame();

	// The RHI thread should always be updated at its own frequency (called from RHI thread if available)
	// The RenderThread FrameID is update in RHIAdvanceFrameFence which is called on the RenderThread
	FValidationRHI* ValidateRHI = (FValidationRHI*)GDynamicRHI;
	ValidateRHI->RHIThreadFrameID++;
}

void FValidationContext::FState::Reset()
{
	bInsideBeginRenderPass = false;
	bGfxPSOSet = false;
	RenderPassName.Reset();
	PreviousRenderPassName.Reset();
	ComputePassName.Reset();
	bComputeShaderSet = false;
	TrackerInstance.ResetAllUAVState();
	GlobalUniformBuffers.Reset();
}

namespace RHIValidation
{
	void FGlobalUniformBuffers::Reset()
	{
		Bindings.Reset();
		check(!bInSetPipelineStateCall);
	}

	void FGlobalUniformBuffers::ValidateSetShaderUniformBuffer(FRHIUniformBuffer* UniformBuffer)
	{
		check(UniformBuffer);
		UniformBuffer->ValidateLifeTime();

		// Skip validating global uniform buffers that are set internally by the RHI as part of the pipeline state.
		if (bInSetPipelineStateCall)
		{
			return;
		}

		const FRHIUniformBufferLayout& Layout = UniformBuffer->GetLayout();

		checkf(EnumHasAnyFlags(Layout.BindingFlags, EUniformBufferBindingFlags::Shader), TEXT("Uniform buffer '%s' does not have the 'Shader' binding flag."), *Layout.GetDebugName());

		if (Layout.StaticSlot < Bindings.Num())
		{
			check(Layout.BindingFlags == EUniformBufferBindingFlags::StaticAndShader);

			ensureMsgf(Bindings[Layout.StaticSlot] == nullptr,
				TEXT("Uniform buffer '%s' was bound statically and is now being bound on a specific RHI shader. Only one binding model should be used at a time."),
				*Layout.GetDebugName());
		}
	}

	ERHIAccess DecayResourceAccess(ERHIAccess AccessMask, ERHIAccess RequiredAccess, bool bAllowUAVOverlap)
	{
		using T = __underlying_type(ERHIAccess);
		checkf((T(RequiredAccess) & (T(RequiredAccess) - 1)) == 0, TEXT("Only one required access bit may be set at once."));
		
		if (EnumHasAnyFlags(RequiredAccess, ERHIAccess::UAVMask))
		{
			// UAV writes decay to no allowed resource access when overlaps are disabled. A barrier is always required after the dispatch/draw.
			// Otherwise keep the same accessmask and don't touch or decay the state
			return !bAllowUAVOverlap ? ERHIAccess::None : AccessMask;
		}

		// Handle DSV modes
		if (EnumHasAnyFlags(RequiredAccess, ERHIAccess::DSVWrite))
		{
			constexpr ERHIAccess CompatibleStates =
				ERHIAccess::DSVRead |
				ERHIAccess::DSVWrite;

			return AccessMask & CompatibleStates;
		}
		if (EnumHasAnyFlags(RequiredAccess, ERHIAccess::DSVRead))
		{
			constexpr ERHIAccess CompatibleStates =
				ERHIAccess::DSVRead |
				ERHIAccess::DSVWrite |
				ERHIAccess::SRVGraphics |
				ERHIAccess::SRVCompute;

			return AccessMask & CompatibleStates;
		}

		if (EnumHasAnyFlags(RequiredAccess, ERHIAccess::WritableMask))
		{
			// Decay to only 1 allowed state for all other writable states.
			return RequiredAccess;
		}

		// Else, the state is readable. All readable states are compatible.
		return AccessMask;
	}

// Warning: this prefix expects a string argument for the failure reason, make sure you add it.
#define BARRIER_TRACKER_LOG_PREFIX_REASON TEXT("RHI validation failed: %s\n\n")\
	TEXT("--------------------------------------------------------------------\n")\
	TEXT("              RHI Resource Transition Validation Error              \n")\
	TEXT("--------------------------------------------------------------------\n")\
	TEXT("\n\n")
	
// Warning: this prefix expects a string argument for the resource name, make sure you add it.
#define BARRIER_TRACKER_LOG_PREFIX_RESNAME TEXT("RHI validation failed for resource \"%s\":\n\n")\
	TEXT("--------------------------------------------------------------------\n")\
	TEXT("              RHI Resource Transition Validation Error              \n")\
	TEXT("--------------------------------------------------------------------\n")\
	TEXT("\n\n")

#define BARRIER_TRACKER_LOG_SUFFIX TEXT("\n\n")\
	TEXT("--------------------------------------------------------------------\n")\
	TEXT("\n\n")

	static inline const TCHAR* GetResourceDebugName(FResource* Resource)
	{
		const TCHAR* DebugName = Resource->GetDebugName();
		return DebugName ? DebugName : TEXT("Unnamed");
	}

	static inline FString GetReasonString_MissingBarrier(
		FResource* Resource, FSubresourceIndex const& SubresourceIndex, 
		const FState& CurrentState,
		const FState& RequiredState)
	{
		const TCHAR* DebugName = GetResourceDebugName(Resource);
		return FString::Printf(
			BARRIER_TRACKER_LOG_PREFIX_RESNAME
			TEXT("Attempted to access resource \"%s\" (0x%p) (%s) from a hardware unit it is not currently accessible from. A resource transition is required.\n\n")
			TEXT("    --- Allowed access states for this resource are: %s\n")
			TEXT("    --- Required access states are:                  %s\n")
			TEXT("    --- Allowed pipelines for this resource are:     %s\n")
			TEXT("    --- Required pipelines are:                      %s\n")
			BARRIER_TRACKER_LOG_SUFFIX,
			DebugName,
			DebugName,
			Resource,
			*SubresourceIndex.ToString(),
			*GetRHIAccessName(CurrentState.Access),
			*GetRHIAccessName(RequiredState.Access),
			*GetRHIPipelineName(CurrentState.Pipelines),
			*GetRHIPipelineName(RequiredState.Pipelines));
	}

	static inline FString GetReasonString_BeginBacktrace(void* CreateTrace, void* BeginTrace)
	{
		if (CreateTrace || BeginTrace)
		{
			return FString::Printf(
				TEXT("    --- Callstack backtraces for the transition which has not been completed (resolve in the Watch window):\n")
				TEXT("        RHICreateTransition: (void**)0x%p,32\n")
				TEXT("        RHIBeginTransitions: (void**)0x%p,32\n"),
				CreateTrace,
				BeginTrace);
		}
		else
		{
			return TEXT("    --- Enable barrier logging for this resource to see a callstack backtrace for the RHIBeginTransitions() call which has not been completed. See RHIValidation.cpp.\n\n");
		}
	}

	static inline FString GetReasonString_AccessDuringTransition(
		FResource* Resource, FSubresourceIndex const& SubresourceIndex, 
		const FState& PendingState,
		const FState& AttemptedState,
		void* CreateTrace, void* BeginTrace)
	{
		const TCHAR* DebugName = GetResourceDebugName(Resource);
		return FString::Printf(
			BARRIER_TRACKER_LOG_PREFIX_RESNAME
			TEXT("Attempted to access resource \"%s\" (0x%p) (%s) whilst an asynchronous resource transition is in progress. A call to RHIEndTransitions() must be made before the resource can be accessed again.\n\n")
			TEXT("    --- Pending access states for this resource are: %s\n")
			TEXT("    --- Attempted access states are:                 %s\n")
			TEXT("    --- Pending pipelines for this resource are:     %s\n")
			TEXT("    --- Attempted pipelines are:                     %s\n")
			TEXT("%s")
			BARRIER_TRACKER_LOG_SUFFIX,
			DebugName,
			DebugName,
			Resource,
			*SubresourceIndex.ToString(),
			*GetRHIAccessName(PendingState.Access),
			*GetRHIAccessName(AttemptedState.Access),
			*GetRHIPipelineName(PendingState.Pipelines),
			*GetRHIPipelineName(AttemptedState.Pipelines),
			*GetReasonString_BeginBacktrace(CreateTrace, BeginTrace));
	}

	static inline FString GetReasonString_DuplicateBeginTransition(
		FResource* Resource, FSubresourceIndex const& SubresourceIndex,
		const FState& PendingState,
		const FState& TargetState,
		void* CreateTrace, void* BeginTrace)
	{
		const TCHAR* DebugName = GetResourceDebugName(Resource);
		return FString::Printf(
			BARRIER_TRACKER_LOG_PREFIX_RESNAME
			TEXT("Attempted to begin a resource transition for resource \"%s\" (0x%p) (%s) whilst a previous asynchronous resource transition is already in progress. A call to RHIEndTransitions() must be made before the resource can be transitioned again.\n\n")
			TEXT("    --- Pending access states for this resource are:              %s\n")
			TEXT("    --- Attempted access states for the duplicate transition are: %s\n")
			TEXT("    --- Pending pipelines for this resource are:                  %s\n")
			TEXT("    --- Attempted pipelines for the duplicate transition are:     %s\n")
			TEXT("%s")
			BARRIER_TRACKER_LOG_SUFFIX,
			DebugName,
			DebugName,
			Resource,
			*SubresourceIndex.ToString(),
			*GetRHIAccessName(PendingState.Access),
			*GetRHIAccessName(TargetState.Access),
			*GetRHIPipelineName(PendingState.Pipelines),
			*GetRHIPipelineName(TargetState.Pipelines),
			*GetReasonString_BeginBacktrace(CreateTrace, BeginTrace));
	}

	static inline FString GetReasonString_WrongPipeline(
		FResource* Resource, FSubresourceIndex const& SubresourceIndex,
		const FState& ActualCurrentState,
		const FState& CurrentStateFromRHI)
	{
		const TCHAR* DebugName = GetResourceDebugName(Resource);
		return FString::Printf(
			BARRIER_TRACKER_LOG_PREFIX_RESNAME
			TEXT("Attempted to begin a resource transition for resource \"%s\" (0x%p) (%s) on the wrong pipeline(s) (\"%s\"). The resource is currently accessible on the \"%s\" pipeline(s).\n\n")
			TEXT("    --- Current access states for this resource are: %s\n")
			TEXT("    --- Attempted access states are:                 %s\n\n")
			TEXT("    --- Ensure that resource transitions are issued on the correct pipeline.\n")
			BARRIER_TRACKER_LOG_SUFFIX,
			DebugName,
			DebugName,
			Resource,
			*SubresourceIndex.ToString(),
			*GetRHIPipelineName(CurrentStateFromRHI.Pipelines),
			*GetRHIPipelineName(ActualCurrentState.Pipelines),
			*GetRHIAccessName(ActualCurrentState.Access),
			*GetRHIAccessName(CurrentStateFromRHI.Access));
	}

	static inline FString GetReasonString_IncorrectPreviousState(
		FResource* Resource, FSubresourceIndex const& SubresourceIndex,
		const FState& CurrentState,
		const FState& CurrentStateFromRHI)
	{
		const TCHAR* DebugName = GetResourceDebugName(Resource);
		return FString::Printf(
			BARRIER_TRACKER_LOG_PREFIX_RESNAME
			TEXT("The explicit previous state \"%s\" does not match the tracked current state \"%s\" for the resource \"%s\" (0x%p) (%s).\n")
			TEXT("    --- Allowed pipelines for this resource are:                           %s\n")
			TEXT("    --- Previous pipelines passed as part of the resource transition were: %s\n\n")
			TEXT("    --- The best solution is to correct the explicit previous state passed for the resource in the call to RHICreateTransition().\n")
			TEXT("    --- Alternatively, use ERHIAccess::Unknown if the actual previous state cannot be determined. Unknown previous resource states have a performance impact so should be avoided if possible.\n")
			BARRIER_TRACKER_LOG_SUFFIX,
			DebugName,
			*GetRHIAccessName(CurrentStateFromRHI.Access),
			*GetRHIAccessName(CurrentState.Access),
			DebugName,
			Resource,
			*SubresourceIndex.ToString(),
			*GetRHIPipelineName(CurrentState.Pipelines),
			*GetRHIPipelineName(CurrentStateFromRHI.Pipelines));
	}

	static inline FString GetReasonString_MismatchedEndTransition(
		FResource* Resource, FSubresourceIndex const& SubresourceIndex,
		const FState& TargetState,
		const FState& TargetStateFromRHI)
	{
		const TCHAR* DebugName = GetResourceDebugName(Resource);
		return FString::Printf(
			BARRIER_TRACKER_LOG_PREFIX_RESNAME
			TEXT("The expected target state \"%s\" on pipe \"%s\" in end transition does not match the tracked target state \"%s\" on pipe \"%s\" for the resource \"%s\" (0x%p) (%s).\n")
			TEXT("    --- The call to EndTransition() is mismatched with the another BeginTransition() with different states.\n")
			BARRIER_TRACKER_LOG_SUFFIX,
			DebugName,
			*GetRHIAccessName(TargetStateFromRHI.Access),
			*GetRHIPipelineName(TargetState.Pipelines),
			*GetRHIAccessName(TargetState.Access),
			*GetRHIPipelineName(TargetStateFromRHI.Pipelines),
			DebugName,
			Resource,
			*SubresourceIndex.ToString());
	}

	static inline FString GetReasonString_UnnecessaryTransition(
		FResource* Resource, FSubresourceIndex const& SubresourceIndex,
		const FState& CurrentState)
	{
		const TCHAR* DebugName = GetResourceDebugName(Resource);
		return FString::Printf(
			BARRIER_TRACKER_LOG_PREFIX_RESNAME
			TEXT("Attempted to begin a resource transition for the resource \"%s\" (0x%p) (%s) to the \"%s\" state on the \"%s\" pipe, but the resource is already in this state. The resource transition is unnecessary.\n")
			TEXT("    --- This is not fatal, but does have an effect on CPU and GPU performance. Consider refactoring rendering code to avoid unnecessary resource transitions.\n")
			TEXT("    --- RenderGraph (RDG) is capable of handling resource transitions automatically.\n")
			BARRIER_TRACKER_LOG_SUFFIX,
			DebugName,
			DebugName,
			Resource,
			*SubresourceIndex.ToString(),
			*GetRHIAccessName(CurrentState.Access),
			*GetRHIPipelineName(CurrentState.Pipelines));
	}

	static inline FString GetReasonString_MismatchedAllUAVsOverlapCall(bool bAllow)
	{
		return FString::Printf(
			BARRIER_TRACKER_LOG_PREFIX_REASON
			TEXT("Mismatched call to %sUAVOverlap. Ensure all calls to RHICmdList.BeginUAVOverlap() are paired with a call to RHICmdList.EndUAVOverlap().")
			BARRIER_TRACKER_LOG_SUFFIX,
			TEXT("UAV overlap mismatch"),
			bAllow ? TEXT("Begin") : TEXT("End")
		);
	}

	static inline FString GetReasonString_MismatchedExplicitUAVOverlapCall(bool bAllow)
	{
		return FString::Printf(
			BARRIER_TRACKER_LOG_PREFIX_REASON
			TEXT("Mismatched call to %sUAVOverlap(FRHIUnorderedAccessView*). Ensure all calls to RHICmdList.BeginUAVOverlap() are paired with a call to RHICmdList.EndUAVOverlap().")
			BARRIER_TRACKER_LOG_SUFFIX,
			TEXT("UAV overlap mismatch"),
			bAllow ? TEXT("Begin") : TEXT("End")
		);
	}

	static inline FString GetReasonString_UAVOverlap(
		FResource* Resource, FSubresourceIndex const& SubresourceIndex,
		const FState& CurrentState, const FState& RequiredState)
	{
		const TCHAR* DebugName = GetResourceDebugName(Resource);
		return FString::Printf(
			BARRIER_TRACKER_LOG_PREFIX_RESNAME
			TEXT("Attempted to access resource \"%s\" (0x%p) (%s) which was previously used with overlapping UAV access, but has not been transitioned since UAV overlap was disabled. A resource transition is required.\n\n")
			TEXT("    --- Allowed access states for this resource are: %s\n")
			TEXT("    --- Required access states are:                  %s\n")
			TEXT("    --- Allowed pipelines for this resource are:     %s\n")
			TEXT("    --- Required pipelines are:                      %s\n")
			BARRIER_TRACKER_LOG_SUFFIX,
			DebugName,
			DebugName,
			Resource,
			*SubresourceIndex.ToString(),
			*GetRHIAccessName(CurrentState.Access),
			*GetRHIAccessName(RequiredState.Access),
			*GetRHIPipelineName(CurrentState.Pipelines),
			*GetRHIPipelineName(RequiredState.Pipelines));
	}

	void FResource::SetDebugName(const TCHAR* Name, const TCHAR* Suffix)
	{
		DebugName = Suffix
			? FString::Printf(TEXT("%s%s"), Name, Suffix)
			: Name;

		if (LoggingMode != ELoggingMode::Manual)
		{
			// Automatically enable/disable barrier logging if the resource name
			// does/doesn't match one in the AutoLogResourceNames array.
			if (Name)
			{
				for (int32 NameIndex = 0; NameIndex < UE_ARRAY_COUNT(GAutoLogResourceNames); ++NameIndex)
				{
					const TCHAR* Str = GAutoLogResourceNames[NameIndex];
					if (!Str)
						break;

					if (FCString::Strcmp(Name, Str) == 0)
					{
						LoggingMode = ELoggingMode::Automatic;
						return;
					}
				}
			}

			LoggingMode = ELoggingMode::None;
		}
	}

	void* FSubresourceState::Log(FResource* Resource, FSubresourceIndex const& SubresourceIndex, void* CreateTrace, const TCHAR* Type, const TCHAR* LogStr)
	{
		void* Trace = CaptureBacktrace();

		if (CreateTrace)
		{
			FPlatformMisc::LowLevelOutputDebugStringf(TEXT("\n%s: (0x%p) (%s), Type: %s, %s, CreateTrace: 0x%p, BeginTrace: 0x%p\n"),
				*Resource->DebugName,
				Resource,
				*SubresourceIndex.ToString(),
				Type,
				LogStr,
				CreateTrace,
				Trace);
		}
		else
		{
			FPlatformMisc::LowLevelOutputDebugStringf(TEXT("\n%s: (0x%p) (%s), Type: %s, %s, Trace: 0x%p\n"),
				*Resource->DebugName,
				Resource,
				*SubresourceIndex.ToString(),
				Type,
				LogStr,
				Trace);
		}

		return Trace;
	}

	void FSubresourceState::BeginTransition(FResource* Resource, FSubresourceIndex const& SubresourceIndex, const FState& CurrentStateFromRHI, const FState& TargetState, EResourceTransitionFlags NewFlags, ERHIPipeline ExecutingPipeline, void* CreateTrace)
	{
		FPipelineState& State = States[ExecutingPipeline];

		void* BeginTrace = nullptr;
		if (Resource->LoggingMode != ELoggingMode::None 
#if LOG_UNNAMED_RESOURCES
			|| Resource->GetDebugName() == nullptr
#endif
			)
		{
			BeginTrace = Log(Resource, SubresourceIndex, CreateTrace, TEXT("BeginTransition"), *FString::Printf(TEXT("Current: (%s) New: (%s), Flags: %s, Executing Pipeline: %s"),
				*State.Current.ToString(),
				*TargetState.ToString(),
				*GetResourceTransitionFlagsName(NewFlags),
				*GetRHIPipelineName(ExecutingPipeline)
			));
		}

		// Check we're not already transitioning
		RHI_VALIDATION_CHECK(!State.bTransitioning, *GetReasonString_DuplicateBeginTransition(Resource, SubresourceIndex, State.Current, TargetState, State.CreateTransitionBacktrace, BeginTrace));

		// Validate the explicit previous state from the RHI matches what we expect...
		{
			// Check for the correct pipeline
			RHI_VALIDATION_CHECK(EnumHasAllFlags(CurrentStateFromRHI.Pipelines, ExecutingPipeline), *GetReasonString_WrongPipeline(Resource, SubresourceIndex, State.Current, TargetState));

			// Check the current RHI state passed in matches the tracked state for the resource, or is "unknown".
			RHI_VALIDATION_CHECK(CurrentStateFromRHI.Access == ERHIAccess::Unknown || (CurrentStateFromRHI.Access == State.Previous.Access && CurrentStateFromRHI.Pipelines == State.Previous.Pipelines),
				*GetReasonString_IncorrectPreviousState(Resource, SubresourceIndex, State.Previous, CurrentStateFromRHI));
		}

		// Check for unnecessary transitions
		// @todo: this check is not particularly useful at the moment, as there are many unnecessary resource transitions.
		//RHI_VALIDATION_CHECK(CurrentState != TargetState, *GetReasonString_UnnecessaryTransition(Resource, SubresourceIndex, CurrentState));

		// Update the tracked state once all pipes have begun.
		State.Previous = TargetState;
		State.Current = TargetState;
		State.Flags = NewFlags;
		State.CreateTransitionBacktrace = CreateTrace;
		State.BeginTransitionBacktrace = BeginTrace;
		State.bUsedWithAllUAVsOverlap = false;
		State.bUsedWithExplicitUAVsOverlap = false;
		State.bTransitioning = true;

		// Replicate the state to other pipes that are not part of the begin pipe mask.
		for (ERHIPipeline OtherPipeline : GetRHIPipelines())
		{
			if (!EnumHasAnyFlags(CurrentStateFromRHI.Pipelines, OtherPipeline))
			{
				States[OtherPipeline] = State;
			}
		}
	}

	void FSubresourceState::EndTransition(FResource* Resource, FSubresourceIndex const& SubresourceIndex, const FState& CurrentStateFromRHI, const FState& TargetState, ERHIPipeline ExecutingPipeline, void* CreateTrace)
	{
		if (Resource->LoggingMode != ELoggingMode::None
#if LOG_UNNAMED_RESOURCES
			|| Resource->GetDebugName() == nullptr
#endif
			)
		{
			Log(Resource, SubresourceIndex, CreateTrace, TEXT("EndTransition"), *FString::Printf(TEXT("Access: %s, Pipeline: %s, Executing Pipeline: %s"),
				*GetRHIAccessName(TargetState.Access),
				*GetRHIPipelineName(TargetState.Pipelines),
				*GetRHIPipelineName(ExecutingPipeline)
			));
		}

		FPipelineState& State = States[ExecutingPipeline];

		// Check that we aren't ending a transition that never began.
		RHI_VALIDATION_CHECK(State.bTransitioning, TEXT("Unsolicited resource end transition call."));
		State.bTransitioning = false;
		State.BeginTransitionBacktrace = nullptr;

		// Check that the end matches the begin.
		RHI_VALIDATION_CHECK(TargetState == State.Current, *GetReasonString_MismatchedEndTransition(Resource, SubresourceIndex, State.Current, TargetState));

		// Replicate the state to other pipes that are not part of the end pipe mask.
		for (ERHIPipeline OtherPipeline : GetRHIPipelines())
		{
			if (!EnumHasAnyFlags(TargetState.Pipelines, OtherPipeline))
			{
				States[OtherPipeline] = State;
			}
		}
	}

	void FSubresourceState::Assert(FResource* Resource, FSubresourceIndex const& SubresourceIndex, const FState& RequiredState, bool bAllowAllUAVsOverlap)
	{
		if (Resource->LoggingMode != ELoggingMode::None
#if LOG_UNNAMED_RESOURCES
			|| Resource->GetDebugName() == nullptr
#endif
			)
		{
			Log(Resource, SubresourceIndex, nullptr, TEXT("Assert"), *FString::Printf(TEXT("Access: %s, Pipeline: %s"), 
				*GetRHIAccessName(RequiredState.Access),
				*GetRHIPipelineName(RequiredState.Pipelines)));
		}

		FPipelineState& State = States[RequiredState.Pipelines];

		// Check we're not trying to access the resource whilst a pending resource transition is in progress.
		RHI_VALIDATION_CHECK(!State.bTransitioning, *GetReasonString_AccessDuringTransition(Resource, SubresourceIndex, State.Current, RequiredState, State.CreateTransitionBacktrace, State.BeginTransitionBacktrace));

		// If UAV overlaps are now disabled, ensure the resource has been transitioned if it was previously used in UAV overlap state.
		RHI_VALIDATION_CHECK((bAllowAllUAVsOverlap || !State.bUsedWithAllUAVsOverlap) && (State.bExplicitAllowUAVOverlap || !State.bUsedWithExplicitUAVsOverlap), *GetReasonString_UAVOverlap(Resource, SubresourceIndex, State.Current, RequiredState));

		// Ensure the resource is in the required state for this operation
		RHI_VALIDATION_CHECK(EnumHasAllFlags(State.Current.Access, RequiredState.Access) && EnumHasAllFlags(State.Current.Pipelines, RequiredState.Pipelines), *GetReasonString_MissingBarrier(Resource, SubresourceIndex, State.Current, RequiredState));

		State.Previous = State.Current;

		if (EnumHasAnyFlags(RequiredState.Access, ERHIAccess::UAVMask))
		{
			if (bAllowAllUAVsOverlap) { State.bUsedWithAllUAVsOverlap = true; }
			if (State.bExplicitAllowUAVOverlap) { State.bUsedWithExplicitUAVsOverlap = true; }
		}

		// Disable all non-compatible access types
		State.Current.Access = DecayResourceAccess(State.Current.Access, RequiredState.Access, bAllowAllUAVsOverlap || State.bExplicitAllowUAVOverlap);
	}

	void FSubresourceState::SpecificUAVOverlap(FResource* Resource, ERHIPipeline Pipeline, FSubresourceIndex const& SubresourceIndex, bool bAllow)
	{
		if (Resource->LoggingMode != ELoggingMode::None
#if LOG_UNNAMED_RESOURCES
			|| Resource->GetDebugName() == nullptr
#endif
			)
		{
			Log(Resource, SubresourceIndex, nullptr, TEXT("UAVOverlap"), *FString::Printf(TEXT("Allow: %s"), bAllow ? TEXT("True") : TEXT("False")));
		}

		FPipelineState& State = States[Pipeline];
		RHI_VALIDATION_CHECK(State.bExplicitAllowUAVOverlap != bAllow, *GetReasonString_MismatchedExplicitUAVOverlapCall(bAllow));
		State.bExplicitAllowUAVOverlap = bAllow;
	}

	inline void FResource::EnumerateSubresources(FSubresourceRange const& SubresourceRange, TFunctionRef<void(FSubresourceState&, FSubresourceIndex const&)> Callback, bool bBeginTransition)
	{
		bool bWholeResource = SubresourceRange.IsWholeResource(*this);
		if (bWholeResource && SubresourceStates.Num() == 0)
		{
			Callback(WholeResourceState, FSubresourceIndex());
		}
		else
		{
			if (SubresourceStates.Num() == 0)
			{
				const uint32 NumSubresources = NumMips * NumArraySlices * NumPlanes;
				SubresourceStates.Reserve(NumSubresources);

				// Copy the whole resource state into all the subresource slots
				for (uint32 Index = 0; Index < NumSubresources; ++Index)
				{
					SubresourceStates.Add(WholeResourceState);
				}
			}

			uint32 LastMip = SubresourceRange.MipIndex + SubresourceRange.NumMips;
			uint32 LastArraySlice = SubresourceRange.ArraySlice + SubresourceRange.NumArraySlices;
			uint32 LastPlaneIndex = SubresourceRange.PlaneIndex + SubresourceRange.NumPlanes;

			for (uint32 PlaneIndex = SubresourceRange.PlaneIndex; PlaneIndex < LastPlaneIndex; ++PlaneIndex)
			{
				for (uint32 MipIndex = SubresourceRange.MipIndex; MipIndex < LastMip; ++MipIndex)
				{
					for (uint32 ArraySlice = SubresourceRange.ArraySlice; ArraySlice < LastArraySlice; ++ArraySlice)
					{
						uint32 SubresourceIndex = PlaneIndex + (MipIndex + ArraySlice * NumMips) * NumPlanes;
						Callback(SubresourceStates[SubresourceIndex], FSubresourceIndex(MipIndex, ArraySlice, PlaneIndex));
					}
				}
			}
		}

		if (bWholeResource && bBeginTransition && SubresourceStates.Num() != 0)
		{
			// Switch back to whole resource state tracking on begin transitions
			WholeResourceState = SubresourceStates[0];
			SubresourceStates.Reset();
		}
	}

	RHI_API EReplayStatus FOperation::Replay(ERHIPipeline Pipeline, bool& bAllowAllUAVsOverlap) const
	{
		switch (Type)
		{
		case EOpType::Rename:
			Data_Rename.Resource->SetDebugName(Data_Rename.DebugName, Data_Rename.Suffix);
			delete[] Data_Rename.DebugName;
			Data_Rename.Resource->ReleaseOpRef();
			break;

		case EOpType::BeginTransition:
			Data_BeginTransition.Identity.Resource->EnumerateSubresources(Data_BeginTransition.Identity.SubresourceRange, [this, Pipeline](FSubresourceState& State, FSubresourceIndex const& SubresourceIndex)
			{
				State.BeginTransition(
					Data_BeginTransition.Identity.Resource,
					SubresourceIndex,
					Data_BeginTransition.PreviousState,
					Data_BeginTransition.NextState,
					Data_BeginTransition.Flags,
					Pipeline,
					Data_BeginTransition.CreateBacktrace);

			}, true);
			Data_BeginTransition.Identity.Resource->ReleaseOpRef();
			break;

		case EOpType::EndTransition:
			Data_EndTransition.Identity.Resource->EnumerateSubresources(Data_EndTransition.Identity.SubresourceRange, [this, Pipeline](FSubresourceState& State, FSubresourceIndex const& SubresourceIndex)
			{
				State.EndTransition(
					Data_EndTransition.Identity.Resource,
					SubresourceIndex,
					Data_EndTransition.PreviousState,
					Data_EndTransition.NextState,
					Pipeline,
					Data_EndTransition.CreateBacktrace);
			});
			Data_EndTransition.Identity.Resource->ReleaseOpRef();
			break;

		case EOpType::Assert:
			Data_Assert.Identity.Resource->EnumerateSubresources(Data_Assert.Identity.SubresourceRange, [this, Pipeline, &bAllowAllUAVsOverlap](FSubresourceState& State, FSubresourceIndex const& SubresourceIndex)
			{
				State.Assert(
					Data_Assert.Identity.Resource,
					SubresourceIndex,
					Data_Assert.RequiredState,
					bAllowAllUAVsOverlap);
			});
			Data_Assert.Identity.Resource->ReleaseOpRef();
			break;

		case EOpType::Signal:
			if (Data_Signal.Pipeline != Pipeline)
			{
				break;
			}

			Data_Signal.Fence->bSignaled = true;
			return EReplayStatus::Signaled;

		case EOpType::Wait:
			if (Data_Wait.Pipeline != Pipeline)
			{
				break;
			}

			if (Data_Wait.Fence->bSignaled)
			{
				// The fence has been completed. Free it now.
				delete Data_Wait.Fence;

				return EReplayStatus::Normal;
			}
			else
			{
				return EReplayStatus::Waiting;
			}

		case EOpType::AllUAVsOverlap:
			RHI_VALIDATION_CHECK(bAllowAllUAVsOverlap != Data_AllUAVsOverlap.bAllow, *GetReasonString_MismatchedAllUAVsOverlapCall(Data_AllUAVsOverlap.bAllow));
			bAllowAllUAVsOverlap = Data_AllUAVsOverlap.bAllow;
			break;

		case EOpType::SpecificUAVOverlap:
			Data_SpecificUAVOverlap.Identity.Resource->EnumerateSubresources(Data_SpecificUAVOverlap.Identity.SubresourceRange, [this, Pipeline](FSubresourceState& State, FSubresourceIndex const& SubresourceIndex)
			{
				State.SpecificUAVOverlap(
					Data_SpecificUAVOverlap.Identity.Resource,
					Pipeline,
					SubresourceIndex,
					Data_SpecificUAVOverlap.bAllow);
			});
			Data_SpecificUAVOverlap.Identity.Resource->ReleaseOpRef();
			break;
		}

		return EReplayStatus::Normal;
	}

	void FTracker::ReplayOpQueue(ERHIPipeline DstOpQueue, FOperationsList&& InOpsList)
	{
		int32 DstOpQueueIndex = GetOpQueueIndex(DstOpQueue);
		FOpQueueState& DstQueue = OpQueues[DstOpQueueIndex];

		// Replay any barrier operations to validate resource barrier usage.
		EReplayStatus Status;
		OpQueues[DstOpQueueIndex].bWaiting |= InOpsList.Incomplete();
		do
		{
			Status = EReplayStatus::Normal;
			for (int32 CurrentIndex = 0; CurrentIndex < int32(ERHIPipeline::Num); ++CurrentIndex)
			{
				const ERHIPipeline CurrentPipeline = ERHIPipeline(1 << CurrentIndex);
				FOpQueueState& CurrentQueue = OpQueues[CurrentIndex];
				if (CurrentQueue.bWaiting)
				{
					Status = CurrentQueue.Ops.Replay(CurrentPipeline, CurrentQueue.bAllowAllUAVsOverlap);
					if (!EnumHasAllFlags(Status, EReplayStatus::Waiting))
					{
						CurrentQueue.Ops.Reset();
						if (CurrentIndex == DstOpQueueIndex && InOpsList.Incomplete())
						{
							Status |= InOpsList.Replay(CurrentPipeline, CurrentQueue.bAllowAllUAVsOverlap);
							CurrentQueue.bWaiting = InOpsList.Incomplete();
						}
						else
						{
							CurrentQueue.bWaiting = false;
						}
					}

					if (EnumHasAllFlags(Status, EReplayStatus::Signaled))
					{
						// run through the queues again to release any waits
						break;
					}
				}
			}
		} while (EnumHasAllFlags(Status, EReplayStatus::Signaled));

		// enqueue incomplete operations
		if (InOpsList.Incomplete())
		{
			DstQueue.Ops.Append(InOpsList);
			InOpsList.Reset();
			DstQueue.bWaiting = true;
		}
	}


	void FUniformBufferResource::InitLifetimeTracking(uint64 FrameID, EUniformBufferUsage Usage)
	{
		AllocatedFrameID = FrameID;
		UniformBufferUsage = Usage;

#if CAPTURE_UNIFORMBUFFER_ALLOCATION_BACKTRACES
		AllocatedCallstack = (UniformBufferUsage != UniformBuffer_MultiFrame) ? RHIValidation::CaptureBacktrace() : nullptr;
#else
		AllocatedCallstack = nullptr;
#endif
	}

	void FUniformBufferResource::UpdateAllocation(uint64 FrameID)
	{
		AllocatedFrameID = FrameID;

#if CAPTURE_UNIFORMBUFFER_ALLOCATION_BACKTRACES
		AllocatedCallstack = (UniformBufferUsage != UniformBuffer_MultiFrame) ? RHIValidation::CaptureBacktrace() : nullptr;
#else
		AllocatedCallstack = nullptr;
#endif
	}

	void FUniformBufferResource::ValidateLifeTime()
	{
		FValidationRHI* ValidateRHI = (FValidationRHI*)GDynamicRHI;
		if (UniformBufferUsage != UniformBuffer_MultiFrame && AllocatedFrameID != ValidateRHI->RHIThreadFrameID)
		{
			FString ErrorMessage = TEXT("Non MultiFrame Uniform buffer has been allocated in a previous frame. The data could have been deleted already!");
			if (AllocatedCallstack != nullptr)
			{
				ErrorMessage += FString::Printf(TEXT("\nAllocation callstack: (void**)0x%p,32"), AllocatedCallstack);
			}
			RHI_VALIDATION_CHECK(false, *ErrorMessage);
		}		
	}

	
	FTracker::FOpQueueState FTracker::OpQueues[int32(ERHIPipeline::Num)] = {};

	RHI_API void* CaptureBacktrace()
	{
		// Back traces will leak. Don't leave this turned on.
		const uint32 MaxDepth = 32;
		uint64* Backtrace = new uint64[MaxDepth];
		FPlatformStackWalk::CaptureStackBackTrace(Backtrace, MaxDepth);

		return Backtrace;
	}
}

TLockFreePointerListUnordered<FValidationContext, PLATFORM_CACHE_LINE_SIZE> FValidationRHICommandContextContainer::ParallelCommandContexts;

#endif	// ENABLE_RHI_VALIDATION
