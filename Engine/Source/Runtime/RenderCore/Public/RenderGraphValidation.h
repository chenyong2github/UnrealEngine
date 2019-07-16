// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraphPass.h"

/** Returns whether render graph runtime debugging is enabled. */
extern bool IsRDGDebugEnabled();

/** Returns whether render graph is running in immediate mode. */
extern bool IsRDGImmediateModeEnabled();

/** Emits a render graph warning. */
extern void EmitRDGWarning(const FString& WarningMessage);

#define EmitRDGWarningf(WarningMessageFormat, ...) \
	EmitRDGWarning(FString::Printf(WarningMessageFormat, ##__VA_ARGS__));

#if RDG_ENABLE_DEBUG

/** Used by the render graph builder to validate correct usage of the graph API from setup to execution.
 *  Validation is compiled out in shipping builds. This class tracks resources and passes as they are
 *  added to the graph. It will then validate execution of the graph, including whether resources are
 *  used during execution, and that they are properly produced before being consumed. All found issues
 *  must be clear enough to help the user identify the problem in client code. Validation should occur
 *  as soon as possible in the graph lifecycle. It's much easier to catch an issue at the setup location
 *  rather than during deferred execution.
 *
 *  Finally, this class is designed for user validation, not for internal graph validation. In other words,
 *  if the user can break the graph externally via the client-facing API, this validation layer should catch it.
 *  Any internal validation of the graph state should be kept out of this class in order to provide a clear
 *  and modular location to extend the validation layer as well as clearly separate the graph implementation
 *  details from events in the graph.
 */
class RENDERCORE_API FRDGUserValidation final
{
public:
	FRDGUserValidation() = default;
	FRDGUserValidation(const FRDGUserValidation&) = delete;
	~FRDGUserValidation();

	/** Validates that the graph has not executed yet. */
	void ExecuteGuard(const TCHAR* Operation, const TCHAR* ResourceName);

	/** Tracks and validates the creation of a new resource in the graph. */
	void ValidateCreateResource(FRDGTrackedResourceRef Resource);

	/** Tracks and validates the creation of a new externally registered resource. */
	void ValidateCreateExternalResource(FRDGTrackedResourceRef Resource);

	/** Tracks and validates usage of a pass parameters allocation. */
	void ValidateAllocPassParameters(const void* Parameters);

	/** Validates a resource extraction operation. */
	void ValidateExtractResource(FRDGTrackedResourceRef Resource);

	/** Tracks and validates the addition of a new pass to the graph. */
	void ValidateAddPass(const FRDGPass* Pass);

	/** Validate pass state before and after execution. */
	void ValidateExecutePassBegin(const FRDGPass* Pass);
	void ValidateExecutePassEnd(const FRDGPass* Pass);

	/** Validate graph state before and after execution. */
	void ValidateExecuteBegin();
	void ValidateExecuteEnd();

	/** Removes the 'produced but not used' warning from the requested resource. */
	void RemoveUnusedWarning(FRDGTrackedResourceRef Resource);

private:
	/** Traverses all resources in the pass and marks whether they are externally accessible by user pass implementations. */
	static void SetAllowRHIAccess(const FRDGPass* Pass, bool bAllowAccess);

	/** All recently allocated pass parameter structure, but not used by a AddPass() yet. */
	TSet<const void*, DefaultKeyFuncs<const void*>, SceneRenderingSetAllocator> AllocatedUnusedPassParameters;

	/** List of tracked resources for validation prior to shutdown. */
	TArray<FRDGTrackedResourceRef, SceneRenderingAllocator> TrackedResources;

	/** Whether the Execute() has already been called. */
	bool bHasExecuted = false;
};

#endif