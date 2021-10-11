// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMDebugInfo.generated.h"

class URigVMNode;

USTRUCT()
struct RIGVM_API FRigVMBreakpoint
{
	GENERATED_BODY()

	FRigVMBreakpoint()
	: bIsActive(true)
	, InstructionIndex(INDEX_NONE)
	, Subject(nullptr)
	, Depth(0)
	{

	}
	
	FRigVMBreakpoint(const uint16 InInstructionIndex, UObject* InNode, const uint16 InDepth)
	: bIsActive(true)
	, InstructionIndex(InInstructionIndex)
	, Subject(InNode)
	, Depth(InDepth)
	{

	}

	FORCEINLINE void Clear()
	{
		bIsActive = true;
		InstructionIndex = INDEX_NONE;
		Subject = nullptr;
		Depth = 0;
	}

	// Whether or not the breakpoint is active
	bool bIsActive;

	// Instruction where this breakpoint is set
	uint16 InstructionIndex;

	// Node related to the breakpoint
	UObject* Subject; 

	// The same instruction might be related to multiple breakpoints at different callstack depths
	uint16 Depth; 

	bool operator==(const FRigVMBreakpoint& Other) const
	{
		return InstructionIndex == Other.InstructionIndex && Subject == Other.Subject;
	}
};

USTRUCT()
struct RIGVM_API FRigVMDebugInfo
{
	GENERATED_BODY()

	FORCEINLINE FRigVMDebugInfo()
		: TemporaryBreakpoint(nullptr)
	, CurrentActiveBreakpoint(nullptr)		
	{
	}

	void ResetState();

	void StartExecution();

	FORCEINLINE void Clear()
	{
		Breakpoints.Empty();
		// Do not remove state
	}

	TSharedPtr<FRigVMBreakpoint> FindBreakpoint(const uint16 InInstructionIndex, const UObject* InSubject);
	TArray<TSharedPtr<FRigVMBreakpoint>> FindBreakpointsAtInstruction(const uint16 InInstructionIndex);
	
	TSharedPtr<FRigVMBreakpoint> AddBreakpoint(const uint16 InstructionIndex, UObject* InNode, const uint16 InDepth, const bool bIsTemporary = false);

	bool RemoveBreakpoint(const TSharedPtr<FRigVMBreakpoint> Breakpoint);

	TArray<TSharedRef<FRigVMBreakpoint>> GetBreakpoints() const { return Breakpoints; }

	void SetBreakpoints(const TArray<TSharedRef<FRigVMBreakpoint>>& InBreakpoints)
	{
		Breakpoints = InBreakpoints;

		if (CurrentActiveBreakpoint.IsValid() && !Breakpoints.Contains(CurrentActiveBreakpoint))
		{
			CurrentActiveBreakpoint = nullptr;
			CurrentActiveBreakpointCallstack.Reset();
		}
	}

	bool IsTemporaryBreakpoint(TSharedPtr<FRigVMBreakpoint> Breakpoint) const
	{
		if (Breakpoint.IsValid())
		{
			return Breakpoint == TemporaryBreakpoint;
		}
		return false;
	}

	bool IsActive(const TSharedPtr<FRigVMBreakpoint> InBreakpoint) const;

	void SetBreakpointHits(const TSharedPtr<FRigVMBreakpoint> InBreakpoint, const uint16 InBreakpointHits);

	void HitBreakpoint(const TSharedPtr<FRigVMBreakpoint> InBreakpoint);

	void SetBreakpointActivationOnHit(const TSharedPtr<FRigVMBreakpoint> InBreakpoint, const uint16 InActivationOnHit);

	void IncrementBreakpointActivationOnHit(const TSharedPtr<FRigVMBreakpoint> InBreakpoint);

	uint16 GetBreakpointHits(const TSharedPtr<FRigVMBreakpoint> InBreakpoint) const;

	TSharedPtr<FRigVMBreakpoint> GetCurrentActiveBreakpoint() const { return CurrentActiveBreakpoint; }

	void SetCurrentActiveBreakpoint(TSharedPtr<FRigVMBreakpoint> Breakpoint) { CurrentActiveBreakpoint = Breakpoint; }

	TArray<UObject*>& GetCurrentActiveBreakpointCallstack() { return CurrentActiveBreakpointCallstack; }

	void SetCurrentActiveBreakpointCallstack(TArray<UObject*> Callstack) { CurrentActiveBreakpointCallstack = Callstack; }

private:
	TArray<TSharedRef<FRigVMBreakpoint>> Breakpoints;
	TSharedPtr<FRigVMBreakpoint> TemporaryBreakpoint;
	TMap<TSharedPtr<FRigVMBreakpoint> , uint16> BreakpointActivationOnHit; // After how many instruction executions, this breakpoint becomes active
	TMap<TSharedPtr<FRigVMBreakpoint> , uint16> BreakpointHits; // How many times this instruction has been executed

	TSharedPtr<FRigVMBreakpoint> CurrentActiveBreakpoint;
	TArray<UObject*> CurrentActiveBreakpointCallstack;
};
