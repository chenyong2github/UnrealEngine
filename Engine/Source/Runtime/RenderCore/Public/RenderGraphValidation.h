// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraphPass.h"

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
	void ValidateCreateTexture(FRDGTextureRef Texture);
	void ValidateCreateBuffer(FRDGBufferRef Buffer);

	/** Tracks and validates the creation of a new externally registered resource. */
	void ValidateCreateExternalTexture(FRDGTextureRef Texture);
	void ValidateCreateExternalBuffer(FRDGBufferRef Buffer);

	/** Validates a resource extraction operation. */
	void ValidateExtractResource(FRDGParentResourceRef Resource);

	/** Tracks and validates the addition of a new pass to the graph.
	 *  @param bSkipPassAccessMarking Skips marking the pass as a producer or incrementing the pass access. Useful when
	 *      the builder needs to inject a pass for debugging while preserving error messages and warnings for the original
	 *      graph structure.
	 */
	void ValidateAddPass(const FRDGPass* Pass, bool bSkipPassAccessMarking);

	/** Validate pass state before and after execution. */
	void ValidateExecutePassBegin(const FRDGPass* Pass);
	void ValidateExecutePassEnd(const FRDGPass* Pass);

	/** Validate graph state before and after execution. */
	void ValidateExecuteBegin();
	void ValidateExecuteEnd();

	/** Removes the 'produced but not used' warning from the requested resource. */
	void RemoveUnusedWarning(FRDGParentResourceRef Resource);

	/** Attempts to mark a resource for clobbering. If already marked, returns false.  */
	bool TryMarkForClobber(FRDGParentResourceRef Resource) const;

private:
	/** Traverses all resources in the pass and marks whether they are externally accessible by user pass implementations. */
	static void SetAllowRHIAccess(const FRDGPass* Pass, bool bAllowAccess);

	/** List of tracked resources for validation prior to shutdown. */
	TArray<FRDGTextureRef, SceneRenderingAllocator> TrackedTextures;
	TArray<FRDGBufferRef, SceneRenderingAllocator> TrackedBuffers;

	/** Whether the Execute() has already been called. */
	bool bHasExecuted = false;
};

/** This class validates and logs barriers submitted by the graph. */
class FRDGBarrierValidation
{
public:
	FRDGBarrierValidation(const FRDGPassRegistry* InPasses, const FRDGEventName& InGraphName);
	FRDGBarrierValidation(const FRDGBarrierValidation&) = delete;

	/** Validates a begin barrier batch just prior to submission to the command list. */
	void ValidateBarrierBatchBegin(const FRDGPass* Pass, const FRDGBarrierBatchBegin& Batch);

	/** Validates an end barrier batch just prior to submission to the command list. */
	void ValidateBarrierBatchEnd(const FRDGPass* Pass, const FRDGBarrierBatchEnd& Batch);

	/** Validates that all barrier batches were flushed at execution end. */
	void ValidateExecuteEnd();

private:
	struct FResourceMap
	{
		TMap<FRDGTextureRef, TArray<FRHITransitionInfo>> Textures;
		TMap<FRDGBufferRef, FRHITransitionInfo> Buffers;
	};

	using FBarrierBatchMap = TMap<const FRDGBarrierBatchBegin*, FResourceMap>;

	FBarrierBatchMap BatchMap;

	const FRDGPassRegistry* Passes = nullptr;
	const TCHAR* GraphName = nullptr;
};

class FRDGLogFile
{
public:
	FRDGLogFile() = default;

	void Begin(
		const FRDGEventName& GraphName,
		const FRDGPassRegistry* InPassRegistry,
		FRDGPassBitArray InPassesCulled,
		FRDGPassHandle InProloguePassHandle,
		FRDGPassHandle InEpiloguePassHandle);

	void AddFirstEdge(const FRDGTextureRef Texture, FRDGPassHandle FirstPass);

	void AddFirstEdge(const FRDGBufferRef Buffer, FRDGPassHandle FirstPass);

	void AddAliasEdge(const FRDGTextureRef TextureBefore, FRDGPassHandle BeforePass, const FRDGTextureRef TextureAfter, FRDGPassHandle PassAfter);

	void AddAliasEdge(const FRDGBufferRef BufferBefore, FRDGPassHandle BeforePass, const FRDGBufferRef BufferAfter, FRDGPassHandle PassAfter);

	void AddTransitionEdge(FRDGPassHandle PassHandle, FRDGSubresourceState StateBefore, FRDGSubresourceState StateAfter, const FRDGTextureRef Texture);

	void AddTransitionEdge(FRDGPassHandle PassHandle, FRDGSubresourceState StateBefore, FRDGSubresourceState StateAfter, const FRDGTextureRef Texture, FRDGTextureSubresource Subresource);

	void AddTransitionEdge(FRDGPassHandle PassHandle, FRDGSubresourceState StateBefore, FRDGSubresourceState StateAfter, const FRDGBufferRef Buffer);

	void End();

private:
	void AddLine(const FString& Line);
	void AddBraceBegin();
	void AddBraceEnd();

	FString GetProducerName(FRDGPassHandle PassHandle);
	FString GetConsumerName(FRDGPassHandle PassHandle);

	FString GetNodeName(FRDGPassHandle Pass);
	FString GetNodeName(const FRDGTexture* Texture);
	FString GetNodeName(const FRDGBuffer* Buffer);

	bool IncludeTransitionEdgeInGraph(FRDGPassHandle PassBefore, FRDGPassHandle PassAfter) const;
	bool IncludeTransitionEdgeInGraph(FRDGPassHandle Pass) const;

	bool bOpen = false;

	TSet<FRDGPassHandle> PassesReferenced;
	TArray<const FRDGTexture*> Textures;
	TArray<const FRDGBuffer*> Buffers;

	const FRDGPassRegistry* Passes = nullptr;
	FRDGPassBitArray PassesCulled;
	FRDGPassHandle ProloguePassHandle;
	FRDGPassHandle EpiloguePassHandle;

	FString Indentation;
	FString File;
	FString GraphName;
};

#endif