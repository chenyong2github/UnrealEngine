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
	{

	}
	
	FRigVMBreakpoint(const uint16 InInstructionIndex, UObject* InNode)
	: bIsActive(true)
	, InstructionIndex(InInstructionIndex)
	, Subject(InNode)
	{

	}

	FORCEINLINE void Clear()
	{
		bIsActive = true;
		InstructionIndex = INDEX_NONE;
		Subject = nullptr;
	}

	bool bIsActive;
	uint16 InstructionIndex;
	UObject* Subject;
};

USTRUCT()
struct RIGVM_API FRigVMDebugInfo
{
	GENERATED_BODY()

	FORCEINLINE FRigVMDebugInfo()
		: SteppingOriginBreakpoint(nullptr)
	{
	}

	void ResetState();

	void StartExecution();

	FORCEINLINE void Clear()
	{
		Breakpoints.Empty();
		// Do not remove state
	}

	FRigVMBreakpoint* FindBreakpoint(const uint16 InstructionIndex);
	
	FRigVMBreakpoint* AddBreakpoint(const uint16 InstructionIndex, UObject* InNode, const bool bIsTemporary = false);

	bool RemoveBreakpoint(const uint16 InstructionIndex);

	TMap<uint16 , FRigVMBreakpoint> GetBreakpoints() const { return Breakpoints; }

	void SetBreakpoints(const TMap<uint16 , FRigVMBreakpoint>& InBreakpoints)
	{
		Breakpoints = InBreakpoints;
	}

	bool IsTemporaryBreakpoint(FRigVMBreakpoint* Breakpoint) const
	{
		return Breakpoint == &TemporaryBreakpoint;
	}

	bool IsActive(const uint16 InstructionIndex) const;

	void HitBreakpoint(const uint16 InstructionIndex);

	void IncrementBreakpointActivationOnHit(const uint16 InstructionIndex);

	uint16 GetBreakpointHits(const uint16 InstructionIndex) const;

	FRigVMBreakpoint* GetSteppingOriginBreakpoint() const { return SteppingOriginBreakpoint; }

	void SetSteppingOriginBreakpoint(FRigVMBreakpoint* Breakpoint) { SteppingOriginBreakpoint = Breakpoint; }

	TArray<UObject*>& GetSteppingOriginBreakpointCallstack() { return SteppingOriginBreakpointCallstack; }

	void SetSteppingOriginBreakpointCallstack(TArray<UObject*> Callstack) { SteppingOriginBreakpointCallstack = Callstack; }

private:
	TMap<uint16 , FRigVMBreakpoint> Breakpoints;
	FRigVMBreakpoint TemporaryBreakpoint;
	TMap<uint16 , uint16> BreakpointActivationOnHit; // After how many instruction executions, this breakpoint becomes active
	TMap<uint16 , uint16> BreakpointHits; // How many times this instruction has been executed

	FRigVMBreakpoint* SteppingOriginBreakpoint;
	TArray<UObject*> SteppingOriginBreakpointCallstack;
};
