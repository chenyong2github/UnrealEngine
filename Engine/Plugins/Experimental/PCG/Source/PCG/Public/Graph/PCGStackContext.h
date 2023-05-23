// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "UObject/WeakObjectPtr.h"

class FString;
class UObject;
class UPCGGraph;
class UPCGNode;
class UPCGPin;

/** A single frame of a call stack, represented as a pointer to the associated object (graph/subgraph or node) or a loop index. */
struct PCG_API FPCGStackFrame
{
	explicit FPCGStackFrame(TWeakObjectPtr<const UObject> InObject)
		: Object(InObject)
	{}

	explicit FPCGStackFrame(int32 InLoopIndex)
		: LoopIndex(InLoopIndex)
	{}

	bool operator==(const FPCGStackFrame& Other) const { return Object == Other.Object && LoopIndex == Other.LoopIndex; }
	bool operator!=(const FPCGStackFrame& Other) const { return !(*this == Other); };

	// A valid frame should either point to an object or have a loop index >= 0.
	TWeakObjectPtr<const UObject> Object;
	int32 LoopIndex = INDEX_NONE;
};

/** A call stack, represented as an array of stack frames. */
class PCG_API FPCGStack
{
	friend class FPCGStackContext;

public:
	/** Push frame onto top of stack. */
	void PushFrame(const FPCGStackFrame& InFrame) { StackFrames.Add(InFrame); }

	/** Pop frame from the stack. */
	void PopFrame();

	/** Construct a string version of this stack. Postfixed by optional node/pin if provided. */
	bool CreateStackFramePath(FString& OutString, const UPCGNode* InNode = nullptr, const UPCGPin* InPin = nullptr) const;

	const TArray<FPCGStackFrame>& GetStackFrames() const { return StackFrames; }
	TArray<FPCGStackFrame>& GetStackFramesMutable() { return StackFrames; }

	bool operator==(const FPCGStack& Other) const;
	bool operator!=(const FPCGStack& Other) const { return !(*this == Other); }

private:
	TArray<FPCGStackFrame> StackFrames;
};

/** A collection of call stacks. */
class PCG_API FPCGStackContext
{
public:
	int32 GetNumStacks() const { return Stacks.Num(); }
	int32 GetCurrentStackIndex() const { return CurrentStackIndex; }
	const FPCGStack* GetStack(int32 InStackIndex) const;

	/** Create a new stack and create a frame from the provided object (typically graph or node pointer). Returns index of newly added stack. */
	int32 PushFrame(const UObject* InFrameObject);

	/** Remove a frame from the current stack. Returns current stack index. */
	int32 PopFrame();

	/** Takes the current stack and appends each of the stacks in InStacks. Called during compilation when inlining a static subgraph. */
	void AppendStacks(const FPCGStackContext& InStacks);

	/** Called during execution when invoking a dynamic subgraph, to prepend the caller stack to form the complete callstacks. */
	void PrependParentStack(const FPCGStack* InParentStack);

private:
	/** List of all stacks encountered top graph and all (nested) subgraphs. Order is simply order of encountering during compilation. */
	TArray<FPCGStack> Stacks;

	/** Index of element in Stacks that is the current stack. */
	int32 CurrentStackIndex = INDEX_NONE;
};
