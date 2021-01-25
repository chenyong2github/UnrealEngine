// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RHIValidation.cpp: Public RHI Validation layer definitions.
=============================================================================*/

#include "RHIValidation.h"
#include "RHIValidationContext.h"
#include "HAL/PlatformStackWalk.h"

#if ENABLE_RHI_VALIDATION

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

FValidationRHI::FValidationRHI(FDynamicRHI* InRHI)
	: RHI(InRHI)
{
	check(RHI);
	UE_LOG(LogRHI, Warning, TEXT("FValidationRHI on, intercepting %s RHI!"), InRHI && InRHI->GetName() ? InRHI->GetName() : TEXT("<NULL>"));
	GRHIValidationEnabled = true;
}

static inline bool IsTessellationPrimitive(EPrimitiveType Type)
{
	return (Type >= PT_1_ControlPointPatchList && Type <= PT_32_ControlPointPatchList);
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
				ensureMsgf(!Initializer.bEnableFrontFaceStencil
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
				ensureMsgf(PSOInitializer.StencilTargetLoadAction == ERenderTargetLoadAction::ENoAction,
					TEXT("No stencil target set, yet PSO wants to load from it!"));
				ensureMsgf(PSOInitializer.StencilTargetStoreAction == ERenderTargetStoreAction::ENoAction,
					TEXT("No stencil target set, yet PSO wants to store into it!"));
*/
			}
		}
		else
		{
			ensureMsgf(!Initializer.bEnableDepthWrite && Initializer.DepthTest == CF_Always, TEXT("No depth render target set, yet PSO wants to use depth operations!"));
			ensureMsgf(PSOInitializer.DepthTargetLoadAction == ERenderTargetLoadAction::ENoAction
				&& PSOInitializer.StencilTargetLoadAction == ERenderTargetLoadAction::ENoAction,
				TEXT("No depth/stencil target set, yet PSO wants to load from it!"));
			ensureMsgf(PSOInitializer.DepthTargetStoreAction == ERenderTargetStoreAction::ENoAction
				&& PSOInitializer.StencilTargetStoreAction == ERenderTargetStoreAction::ENoAction,
				TEXT("No depth/stencil target set, yet PSO wants to store into it!"));
		}
	}

	if (IsTessellationPrimitive(PSOInitializer.PrimitiveType ))
	{
		ensureMsgf(RHISupportsTessellation(GMaxRHIShaderPlatform), TEXT("Tried to create a tessellation PSO but RHI doesn't support it!"));
		ensureMsgf(PSOInitializer.BoundShaderState.HullShaderRHI && PSOInitializer.BoundShaderState.DomainShaderRHI, 
			TEXT("Tried to create a tessellation PSO but no Hull or Domain shader set!"));
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
}

namespace RHIValidation
{
#define BARRIER_TRACKER_LOG_PREFIX TEXT("\n\n")\
	TEXT("--------------------------------------------------------------------\n")\
	TEXT("              RHI Resource Transition Validation Error              \n")\
	TEXT("--------------------------------------------------------------------\n")\
	TEXT("\n\n")
#define BARRIER_TRACKER_LOG_SUFFIX TEXT("\n\n")\
	TEXT("--------------------------------------------------------------------\n")\
	TEXT("\n\n")

	static inline FString GetReasonString_MissingBarrier(
		FResource* Resource, FSubresourceIndex const& SubresourceIndex, 
		const FState& CurrentState,
		const FState& RequiredState)
	{
		const TCHAR* DebugName = Resource->GetDebugName();
		return FString::Printf(
			BARRIER_TRACKER_LOG_PREFIX
			TEXT("Attempted to access resource \"%s\" (0x%p) (%s) from a hardware unit it is not currently accessible from. A resource transition is required.\n\n")
			TEXT("    --- Allowed access states for this resource are: %s\n")
			TEXT("    --- Required access states are:                  %s\n")
			TEXT("    --- Allowed pipelines for this resource are:     %s\n")
			TEXT("    --- Required pipelines are:                      %s\n")
			BARRIER_TRACKER_LOG_SUFFIX,
			DebugName ? DebugName : TEXT("Unnamed"),
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
		const TCHAR* DebugName = Resource->GetDebugName();
		return FString::Printf(
			BARRIER_TRACKER_LOG_PREFIX
			TEXT("Attempted to access resource \"%s\" (0x%p) (%s) whilst an asynchronous resource transition is in progress. A call to RHIEndTransitions() must be made before the resource can be accessed again.\n\n")
			TEXT("    --- Pending access states for this resource are: %s\n")
			TEXT("    --- Attempted access states are:                 %s\n")
			TEXT("    --- Pending pipelines for this resource are:     %s\n")
			TEXT("    --- Attempted pipelines are:                     %s\n")
			TEXT("%s")
			BARRIER_TRACKER_LOG_SUFFIX,
			DebugName ? DebugName : TEXT("Unnamed"),
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
		const TCHAR* DebugName = Resource->GetDebugName();
		return FString::Printf(
			BARRIER_TRACKER_LOG_PREFIX
			TEXT("Attempted to begin a resource transition for resource \"%s\" (0x%p) (%s) whilst a previous asynchronous resource transition is already in progress. A call to RHIEndTransitions() must be made before the resource can be transitioned again.\n\n")
			TEXT("    --- Pending access states for this resource are:              %s\n")
			TEXT("    --- Attempted access states for the duplicate transition are: %s\n")
			TEXT("    --- Pending pipelines for this resource are:                  %s\n")
			TEXT("    --- Attempted pipelines for the duplicate transition are:     %s\n")
			TEXT("%s")
			BARRIER_TRACKER_LOG_SUFFIX,
			DebugName ? DebugName : TEXT("Unnamed"),
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
		const TCHAR* DebugName = Resource->GetDebugName();
		return FString::Printf(
			BARRIER_TRACKER_LOG_PREFIX
			TEXT("Attempted to begin a resource transition for resource \"%s\" (0x%p) (%s) on the wrong pipeline(s) (\"%s\"). The resource is currently accessible on the \"%s\" pipeline(s).\n\n")
			TEXT("    --- Current access states for this resource are: %s\n")
			TEXT("    --- Attempted access states are:                 %s\n\n")
			TEXT("    --- Ensure that resource transitions are issued on the correct pipeline.\n")
			BARRIER_TRACKER_LOG_SUFFIX,
			DebugName ? DebugName : TEXT("Unnamed"),
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
		const TCHAR* DebugName = Resource->GetDebugName();
		return FString::Printf(
			BARRIER_TRACKER_LOG_PREFIX
			TEXT("The explicit previous state \"%s\" does not match the tracked current state \"%s\" for the resource \"%s\" (0x%p) (%s).\n")
			TEXT("    --- Allowed pipelines for this resource are:                           %s\n")
			TEXT("    --- Previous pipelines passed as part of the resource transition were: %s\n\n")
			TEXT("    --- The best solution is to correct the explicit previous state passed for the resource in the call to RHICreateTransition().\n")
			TEXT("    --- Alternatively, use ERHIAccess::Unknown if the actual previous state cannot be determined. Unknown previous resource states have a performance impact so should be avoided if possible.\n")
			BARRIER_TRACKER_LOG_SUFFIX,
			*GetRHIAccessName(CurrentStateFromRHI.Access),
			*GetRHIAccessName(CurrentState.Access),
			DebugName ? DebugName : TEXT("Unnamed"),
			Resource,
			*SubresourceIndex.ToString(),
			*GetRHIPipelineName(CurrentState.Pipelines),
			*GetRHIPipelineName(CurrentStateFromRHI.Pipelines));
	}

	static inline FString GetReasonString_UnnecessaryTransition(
		FResource* Resource, FSubresourceIndex const& SubresourceIndex,
		const FState& CurrentState)
	{
		const TCHAR* DebugName = Resource->GetDebugName();
		return FString::Printf(
			BARRIER_TRACKER_LOG_PREFIX
			TEXT("Attempted to begin a resource transition for the resource \"%s\" (0x%p) (%s) to the \"%s\" state on the \"%s\" pipe, but the resource is already in this state. The resource transition is unnecessary.\n")
			TEXT("    --- This is not fatal, but does have an effect on CPU and GPU performance. Consider refactoring rendering code to avoid unnecessary resource transitions.\n")
			TEXT("    --- RenderGraph (RDG) is capable of handling resource transitions automatically.\n")
			BARRIER_TRACKER_LOG_SUFFIX,
			DebugName ? DebugName : TEXT("Unnamed"),
			Resource,
			*SubresourceIndex.ToString(),
			*GetRHIAccessName(CurrentState.Access),
			*GetRHIPipelineName(CurrentState.Pipelines));
	}

	static inline FString GetReasonString_MismatchedAllUAVsOverlapCall(bool bAllow)
	{
		return FString::Printf(
			BARRIER_TRACKER_LOG_PREFIX
			TEXT("Mismatched call to %sUAVOverlap. Ensure all calls to RHICmdList.BeginUAVOverlap() are paired with a call to RHICmdList.EndUAVOverlap().")
			BARRIER_TRACKER_LOG_SUFFIX,
			bAllow ? TEXT("Begin") : TEXT("End")
		);
	}

	static inline FString GetReasonString_MismatchedExplicitUAVOverlapCall(bool bAllow)
	{
		return FString::Printf(
			BARRIER_TRACKER_LOG_PREFIX
			TEXT("Mismatched call to %sUAVOverlap(FRHIUnorderedAccessView*). Ensure all calls to RHICmdList.BeginUAVOverlap() are paired with a call to RHICmdList.EndUAVOverlap().")
			BARRIER_TRACKER_LOG_SUFFIX,
			bAllow ? TEXT("Begin") : TEXT("End")
		);
	}

	static inline FString GetReasonString_UAVOverlap(
		FResource* Resource, FSubresourceIndex const& SubresourceIndex,
		const FState& CurrentState, const FState& RequiredState)
	{
		const TCHAR* DebugName = Resource->GetDebugName();
		return FString::Printf(
			BARRIER_TRACKER_LOG_PREFIX
			TEXT("Attempted to access resource \"%s\" (0x%p) (%s) which was previously used with overlapping UAV access, but has not been transitioned since UAV overlap was disabled. A resource transition is required.\n\n")
			TEXT("    --- Allowed access states for this resource are: %s\n")
			TEXT("    --- Required access states are:                  %s\n")
			TEXT("    --- Allowed pipelines for this resource are:     %s\n")
			TEXT("    --- Required pipelines are:                      %s\n")
			BARRIER_TRACKER_LOG_SUFFIX,
			DebugName ? DebugName : TEXT("Unnamed"),
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

	void FSubresourceState::BeginTransition(FResource* Resource, FSubresourceIndex const& SubresourceIndex, const FState& CurrentStateFromRHI, const FState& TargetState, EResourceTransitionFlags NewFlags, void* CreateTrace)
	{
		void* BeginTrace = nullptr;
		if (Resource->LoggingMode != ELoggingMode::None 
#if LOG_UNNAMED_RESOURCES
			|| Resource->GetDebugName() == nullptr
#endif
			)
		{
			BeginTrace = Log(Resource, SubresourceIndex, CreateTrace, TEXT("BeginTransition"), *FString::Printf(TEXT("Current: (%s) New: (%s), Flags: %s"),
				*CurrentState.ToString(),
				*TargetState.ToString(),
				*GetResourceTransitionFlagsName(NewFlags)
			));
		}

		// Check we're not already transitioning
		ensureMsgf(!bTransitioning, TEXT("%s"), *GetReasonString_DuplicateBeginTransition(Resource, SubresourceIndex, CurrentState, TargetState, CreateTransitionBacktrace, BeginTransitionBacktrace));

		// Validate the explicit previous state from the RHI matches what we expect...
		{
			// Check for the correct pipeline
			ensureMsgf(EnumHasAllFlags(CurrentState.Pipelines, CurrentStateFromRHI.Pipelines), TEXT("%s"), *GetReasonString_WrongPipeline(Resource, SubresourceIndex, CurrentState, TargetState));

			// Check the current RHI state passed in matches the tracked state for the resource, or is "unknown".
			ensureMsgf(CurrentStateFromRHI.Access == ERHIAccess::Unknown || CurrentStateFromRHI.Access == PreviousState.Access, TEXT("%s"), *GetReasonString_IncorrectPreviousState(Resource, SubresourceIndex, PreviousState, CurrentStateFromRHI));
		}

		// Check for unnecessary transitions
		// @todo: this check is not particularly useful at the moment, as there are many unnecessary resource transitions.
		//ensureMsgf(CurrentState != TargetState, TEXT("%s"), *GetReasonString_UnnecessaryTransition(Resource, SubresourceIndex, CurrentState));

		// Update the tracked state
		PreviousState = TargetState;
		CurrentState = TargetState;
		Flags = NewFlags;
		CreateTransitionBacktrace = CreateTrace;
		BeginTransitionBacktrace = BeginTrace;
		bUsedWithAllUAVsOverlap = false;
		bUsedWithExplicitUAVsOverlap = false;

		bTransitioning = true;
	}

	void FSubresourceState::EndTransition(FResource* Resource, FSubresourceIndex const& SubresourceIndex, void* CreateTrace)
	{
		if (Resource->LoggingMode != ELoggingMode::None
#if LOG_UNNAMED_RESOURCES
			|| Resource->GetDebugName() == nullptr
#endif
			)
		{
			Log(Resource, SubresourceIndex, CreateTrace, TEXT("EndTransition"), *FString::Printf(TEXT("Access: %s, Pipeline: %s"),
				*GetRHIAccessName(CurrentState.Access),
				*GetRHIPipelineName(CurrentState.Pipelines)
			));
		}

		// This should not really be possible given the RHI API design. If this fires, it's more likely an RHI bug.
		ensureMsgf(bTransitioning, TEXT("Unsolicited resource end transition call."));
		bTransitioning = false;
		BeginTransitionBacktrace = nullptr;
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

		// Check we're not trying to access the resource whilst a pending resource transition is in progress.
		ensureMsgf(!bTransitioning, TEXT("%s"), *GetReasonString_AccessDuringTransition(Resource, SubresourceIndex, CurrentState, RequiredState, CreateTransitionBacktrace, BeginTransitionBacktrace));

		// If UAV overlaps are now disabled, ensure the resource has been transitioned if it was previously used in UAV overlap state.
		ensureMsgf((bAllowAllUAVsOverlap || !bUsedWithAllUAVsOverlap) && (bExplicitAllowUAVOverlap || !bUsedWithExplicitUAVsOverlap), TEXT("%s"), *GetReasonString_UAVOverlap(Resource, SubresourceIndex, CurrentState, RequiredState));

		// Ensure the resource is in the required state for this operation
		ensureMsgf(EnumHasAllFlags(CurrentState.Access, RequiredState.Access) && EnumHasAllFlags(CurrentState.Pipelines, RequiredState.Pipelines), TEXT("%s"), *GetReasonString_MissingBarrier(Resource, SubresourceIndex, CurrentState, RequiredState));

		PreviousState = CurrentState;

		if (EnumHasAnyFlags(RequiredState.Access, ERHIAccess::UAVMask))
		{
			if (bAllowAllUAVsOverlap) { bUsedWithAllUAVsOverlap = true; }
			if (bExplicitAllowUAVOverlap) { bUsedWithExplicitUAVsOverlap = true; }
		}

		// Disable all non-compatible access types
		CurrentState.Access = RHIDecayResourceAccess(CurrentState.Access, RequiredState.Access, bAllowAllUAVsOverlap || bExplicitAllowUAVOverlap);
		CurrentState.Pipelines = CurrentState.Pipelines & RequiredState.Pipelines;
	}

	void FSubresourceState::SpecificUAVOverlap(FResource* Resource, FSubresourceIndex const& SubresourceIndex, bool bAllow)
	{
		if (Resource->LoggingMode != ELoggingMode::None
#if LOG_UNNAMED_RESOURCES
			|| Resource->GetDebugName() == nullptr
#endif
			)
		{
			Log(Resource, SubresourceIndex, nullptr, TEXT("UAVOverlap"), *FString::Printf(TEXT("Allow: %s"), bAllow ? TEXT("True") : TEXT("False")));
		}

		ensureMsgf(bExplicitAllowUAVOverlap != bAllow, TEXT("%s"), *GetReasonString_MismatchedExplicitUAVOverlapCall(bAllow));
		bExplicitAllowUAVOverlap = bAllow;
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

	RHI_API EReplayStatus FOperation::Replay(bool& bAllowAllUAVsOverlap) const
	{
		switch (Type)
		{
		case EOpType::Rename:
			Data_Rename.Resource->SetDebugName(Data_Rename.DebugName, Data_Rename.Suffix);
			delete[] Data_Rename.DebugName;
			Data_Rename.Resource->ReleaseOpRef();
			break;

		case EOpType::BeginTransition:
			Data_BeginTransition.Identity.Resource->EnumerateSubresources(Data_BeginTransition.Identity.SubresourceRange, [this](FSubresourceState& State, FSubresourceIndex const& SubresourceIndex)
			{
				State.BeginTransition(
					Data_BeginTransition.Identity.Resource,
					SubresourceIndex,
					Data_BeginTransition.PreviousState,
					Data_BeginTransition.NextState,
					Data_BeginTransition.Flags,
					Data_BeginTransition.CreateBacktrace);

			}, true);
			Data_BeginTransition.Identity.Resource->ReleaseOpRef();
			break;

		case EOpType::EndTransition:
			Data_EndTransition.Identity.Resource->EnumerateSubresources(Data_EndTransition.Identity.SubresourceRange, [this](FSubresourceState& State, FSubresourceIndex const& SubresourceIndex)
			{
				State.EndTransition(
					Data_EndTransition.Identity.Resource, 
					SubresourceIndex,
					Data_EndTransition.CreateBacktrace);
			});
			Data_EndTransition.Identity.Resource->ReleaseOpRef();
			break;

		case EOpType::Assert:
			Data_Assert.Identity.Resource->EnumerateSubresources(Data_Assert.Identity.SubresourceRange, [this, &bAllowAllUAVsOverlap](FSubresourceState& State, FSubresourceIndex const& SubresourceIndex)
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
			Data_Signal.Fence->bSignaled = true;
			return EReplayStatus::Signaled;

		case EOpType::Wait:
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
			ensureMsgf(bAllowAllUAVsOverlap != Data_AllUAVsOverlap.bAllow, TEXT("%s"), *GetReasonString_MismatchedAllUAVsOverlapCall(Data_AllUAVsOverlap.bAllow));
			bAllowAllUAVsOverlap = Data_AllUAVsOverlap.bAllow;
			break;

		case EOpType::SpecificUAVOverlap:
			Data_SpecificUAVOverlap.Identity.Resource->EnumerateSubresources(Data_SpecificUAVOverlap.Identity.SubresourceRange, [this](FSubresourceState& State, FSubresourceIndex const& SubresourceIndex)
			{
				State.SpecificUAVOverlap(
					Data_SpecificUAVOverlap.Identity.Resource, 
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
				FOpQueueState& CurrentQueue = OpQueues[CurrentIndex];
				if (CurrentQueue.bWaiting)
				{
					Status = CurrentQueue.Ops.Replay(CurrentQueue.bAllowAllUAVsOverlap);
					if (!EnumHasAllFlags(Status, EReplayStatus::Waiting))
					{
						CurrentQueue.Ops.Reset();
						if (CurrentIndex == DstOpQueueIndex && InOpsList.Incomplete())
						{
							Status |= InOpsList.Replay(CurrentQueue.bAllowAllUAVsOverlap);
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
			ensureMsgf(false, TEXT("%s"), *ErrorMessage);
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
