// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMCore/RigVMDebugInfo.h"

void FRigVMDebugInfo::ResetState()
{
	BreakpointHits.Empty();
	BreakpointActivationOnHit.Empty();
	TemporaryBreakpoint.Clear();
}

void FRigVMDebugInfo::StartExecution()
{
	BreakpointHits.Empty();
	SteppingOriginBreakpoint = nullptr;
	SteppingOriginBreakpointCallstack.Empty();
}

FRigVMBreakpoint* FRigVMDebugInfo::FindBreakpoint(const uint16 InstructionIndex) 
{
	if (FRigVMBreakpoint* Found = Breakpoints.Find(InstructionIndex))
	{
		return Found;
	}
	if (TemporaryBreakpoint.InstructionIndex == InstructionIndex)
	{
		return &TemporaryBreakpoint;
	}
	return nullptr;
}

FRigVMBreakpoint* FRigVMDebugInfo::AddBreakpoint(const uint16 InstructionIndex, UObject* InNode, const bool bIsTemporary)
{
	if (!Breakpoints.Contains(InstructionIndex))
	{
		if (bIsTemporary)
		{
			TemporaryBreakpoint = FRigVMBreakpoint(InstructionIndex, InNode);
				
			// Do not override the state if it already exists
			BreakpointActivationOnHit.FindOrAdd(InstructionIndex, 0);
			BreakpointHits.FindOrAdd(InstructionIndex, 0);
			return &TemporaryBreakpoint;
		}
		else if (FRigVMBreakpoint* NewBP = &Breakpoints.Add(InstructionIndex, FRigVMBreakpoint(InstructionIndex, InNode)))
		{
			// Do not override the state if it already exists
			BreakpointActivationOnHit.FindOrAdd(InstructionIndex, 0);
			BreakpointHits.FindOrAdd(InstructionIndex, 0);
			return NewBP;
		}
	}
	return nullptr;
}

bool FRigVMDebugInfo::RemoveBreakpoint(const uint16 InstructionIndex)
{
	if (FRigVMBreakpoint* Found = Breakpoints.Find(InstructionIndex))
	{
		Breakpoints.Remove(InstructionIndex);
		BreakpointHits.Remove(InstructionIndex);
		BreakpointActivationOnHit.Remove(InstructionIndex);
		return true;
	}
	if (TemporaryBreakpoint.InstructionIndex == InstructionIndex)
	{
		TemporaryBreakpoint.Clear();
		BreakpointHits.Remove(InstructionIndex);
		BreakpointActivationOnHit.Remove(InstructionIndex);
		return true;
	}
	return false;
}

bool FRigVMDebugInfo::IsActive(const uint16 InstructionIndex) const
{
	const FRigVMBreakpoint* Breakpoint = Breakpoints.Find(InstructionIndex);
	if (!Breakpoint && TemporaryBreakpoint.InstructionIndex == InstructionIndex)
	{
		Breakpoint = &TemporaryBreakpoint;
	}
	if (Breakpoint)
	{
		if (Breakpoint->bIsActive)
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

void FRigVMDebugInfo::HitBreakpoint(const uint16 InstructionIndex)
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

void FRigVMDebugInfo::IncrementBreakpointActivationOnHit(const uint16 InstructionIndex)
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

uint16 FRigVMDebugInfo::GetBreakpointHits(const uint16 InstructionIndex) const
{
	if (BreakpointActivationOnHit.Contains(InstructionIndex))
	{
		return BreakpointActivationOnHit[InstructionIndex];
	}
	return 0;
}
