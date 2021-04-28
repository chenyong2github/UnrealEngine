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
	, Subject(nullptr)
	{

	}
	
	FRigVMBreakpoint(URigVMNode* InNode)
	: bIsActive(true)
	, Subject(InNode)
	{

	}

	bool bIsActive;
	URigVMNode* Subject;
};

USTRUCT()
struct RIGVM_API FRigVMDebugInfo
{
	GENERATED_BODY()

	FORCEINLINE FRigVMDebugInfo()
	{
	}

	FORCEINLINE void ResetState()
	{
		BreakpointHits.Empty();
		BreakpointActivationOnHit.Empty();
	}

	FORCEINLINE void StartExecution()
	{
		BreakpointHits.Empty();
	}

	FORCEINLINE void Clear()
	{
		Breakpoints.Empty();
		// Do not remove state
	}

	FRigVMBreakpoint* FindBreakpoint(const uint16 InstructionIndex) 
	{
		return Breakpoints.Find(InstructionIndex);
	}
	
	FRigVMBreakpoint* AddBreakpoint(const uint16 InstructionIndex, URigVMNode* InNode)
	{
		if (!Breakpoints.Contains(InstructionIndex))
		{
			if (FRigVMBreakpoint* NewBP = &Breakpoints.Add(InstructionIndex, InNode))
			{
				// Do not override the state if it already exists
				BreakpointActivationOnHit.FindOrAdd(InstructionIndex, 0);
				BreakpointHits.FindOrAdd(InstructionIndex, 0);
				return NewBP;
			}
		}
		return nullptr;
	}

	TMap<uint16 , FRigVMBreakpoint> GetBreakpoints() const { return Breakpoints; }

	void SetBreakpoints(const TMap<uint16 , FRigVMBreakpoint>& InBreakpoints) { Breakpoints = InBreakpoints; }

	bool HasBreakpoint(const uint16 InstructionIndex) { return Breakpoints.Contains(InstructionIndex); }

	bool IsActive(const uint16 InstructionIndex)
	{
		if (FRigVMBreakpoint* BP = Breakpoints.Find(InstructionIndex))
		{
			if (BP->bIsActive)
			{
				uint16 Hits = 0;
				uint16 OnHit = 0;
				if (BreakpointHits.Contains(InstructionIndex))
				{
					Hits = BreakpointHits.FindChecked(InstructionIndex);
				}
				if (BreakpointActivationOnHit.Contains(InstructionIndex))
				{
					OnHit = BreakpointActivationOnHit.FindChecked(InstructionIndex);
				}
				return Hits == OnHit;
			}				
		}
		return false;		
	}

	void HitBreakpoint(const uint16 InstructionIndex)
	{
		if (BreakpointHits.Contains(InstructionIndex))
		{
			BreakpointHits[InstructionIndex]++;
		}
		else
		{
			BreakpointHits.Add(InstructionIndex, 1);
		}
	}

	void IncrementBreakpointActivationOnHit(const uint16 InstructionIndex)
	{
		if (BreakpointActivationOnHit.Contains(InstructionIndex))
		{
			BreakpointActivationOnHit[InstructionIndex]++;
		}
		else
		{
			BreakpointActivationOnHit.Add(InstructionIndex, 1);
		}
	}

private:
	TMap<uint16 , FRigVMBreakpoint> Breakpoints;
	TMap<uint16 , uint16> BreakpointActivationOnHit; // After how many instruction executions, this breakpoint becomes active
	TMap<uint16 , uint16> BreakpointHits; // How many times this instruction has been executed
};
