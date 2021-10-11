// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMCore/RigVMDebugInfo.h"

void FRigVMDebugInfo::ResetState()
{
	BreakpointHits.Empty();
	BreakpointActivationOnHit.Empty();
	TemporaryBreakpoint = nullptr;
	CurrentActiveBreakpoint = nullptr;
	CurrentActiveBreakpointCallstack.Empty();
}

void FRigVMDebugInfo::StartExecution()
{
	BreakpointHits.Empty();
	CurrentActiveBreakpoint = nullptr;
	CurrentActiveBreakpointCallstack.Empty();
}

TSharedPtr<FRigVMBreakpoint> FRigVMDebugInfo::FindBreakpoint(const uint16 InInstructionIndex, const UObject* InSubject)
{
	TArray<TSharedPtr<FRigVMBreakpoint>> BreakpointAtInstruction = FindBreakpointsAtInstruction(InInstructionIndex);
	for (TSharedPtr<FRigVMBreakpoint> BP : BreakpointAtInstruction)
	{
		if (BP->Subject == InSubject)
		{
			return BP;
		}
	}
	return nullptr;
}

TArray<TSharedPtr<FRigVMBreakpoint>> FRigVMDebugInfo::FindBreakpointsAtInstruction(const uint16 InInstructionIndex) 
{
	TArray<TSharedPtr<FRigVMBreakpoint>> Result;
	for (TSharedPtr<FRigVMBreakpoint> BP : Breakpoints)
	{
		if (BP->InstructionIndex == InInstructionIndex)
		{
			Result.Add(BP);
		}
	}

	if (TemporaryBreakpoint.IsValid() && TemporaryBreakpoint->InstructionIndex == InInstructionIndex)
	{
		Result.Add(TemporaryBreakpoint);
	}
	return Result;
}

TSharedPtr<FRigVMBreakpoint> FRigVMDebugInfo::AddBreakpoint(const uint16 InstructionIndex, UObject* InNode, const uint16 InDepth,
	const bool bIsTemporary)
{
	for (const TSharedPtr<FRigVMBreakpoint>& BP : FindBreakpointsAtInstruction(InstructionIndex))
	{
		if (BP->Subject == InNode)
		{
			return nullptr;
		}
	}
	
	
	if (bIsTemporary)
	{
		TemporaryBreakpoint = MakeShared<FRigVMBreakpoint>(InstructionIndex, InNode, 0);
			
		// Do not override the state if it already exists
		BreakpointActivationOnHit.FindOrAdd(TemporaryBreakpoint, 0);
		BreakpointHits.FindOrAdd(TemporaryBreakpoint, 0);
		return TemporaryBreakpoint;
	}
	else
	{
		// Breakpoints are sorted by instruction index and callstack depth
		int32 Index = 0;
		for(; Index<Breakpoints.Num(); ++Index)
		{
			if (InstructionIndex < Breakpoints[Index]->InstructionIndex ||
				(InstructionIndex == Breakpoints[Index]->InstructionIndex && InDepth < Breakpoints[Index]->Depth))
			{
				break;
			}
		}
		TSharedRef<FRigVMBreakpoint> NewBP = MakeShared<FRigVMBreakpoint>(InstructionIndex, InNode, InDepth);
		Breakpoints.Insert(NewBP, Index);
		
		// Do not override the state if it already exists
		BreakpointActivationOnHit.FindOrAdd(NewBP, 0);
		BreakpointHits.FindOrAdd(NewBP, 0);
		return NewBP;
	}
	
	return nullptr;
}

bool FRigVMDebugInfo::RemoveBreakpoint(const TSharedPtr<FRigVMBreakpoint> InBreakpoint)
{
	TSharedRef<FRigVMBreakpoint> BreakpointRef = InBreakpoint.ToSharedRef();

	bool Found = false;
	int32 Index = Breakpoints.Find(BreakpointRef);
	if (Index != INDEX_NONE)
	{
		BreakpointHits.Remove(BreakpointRef);
		BreakpointActivationOnHit.Remove(BreakpointRef);
		Breakpoints.RemoveAt(Index);
		Found = true;
	}
	if (TemporaryBreakpoint.IsValid() && InBreakpoint == TemporaryBreakpoint)
	{
		BreakpointHits.Remove(TemporaryBreakpoint);
		BreakpointActivationOnHit.Remove(TemporaryBreakpoint);
		TemporaryBreakpoint = nullptr;
		Found = true;
	}
	return Found;
}

bool FRigVMDebugInfo::IsActive(const TSharedPtr<FRigVMBreakpoint> InBreakpoint) const
{
	TSharedPtr<FRigVMBreakpoint> Breakpoint = InBreakpoint;
	if (!Breakpoint && TemporaryBreakpoint.IsValid() && TemporaryBreakpoint == InBreakpoint)
	{
		Breakpoint = TemporaryBreakpoint;
	}
	if (Breakpoint)
	{
		if (Breakpoint->bIsActive)
		{
			uint16 Hits = 0;
			uint16 OnHit = 0;
			if (BreakpointHits.Contains(Breakpoint))
			{
				Hits = BreakpointHits.FindChecked(Breakpoint);
			}
			if (BreakpointActivationOnHit.Contains(Breakpoint))
			{
				OnHit = BreakpointActivationOnHit.FindChecked(Breakpoint);
			}
			return Hits == OnHit;
		}				
	}
	return false;		
}

void FRigVMDebugInfo::SetBreakpointHits(const TSharedPtr<FRigVMBreakpoint> InBreakpoint, const uint16 InBreakpointHits)
{
	if (BreakpointHits.Contains(InBreakpoint))
	{
		BreakpointHits[InBreakpoint] = InBreakpointHits;
	}
	else
	{
		BreakpointHits.Add(InBreakpoint, InBreakpointHits);
	}
}

void FRigVMDebugInfo::HitBreakpoint(const TSharedPtr<FRigVMBreakpoint> InBreakpoint)
{
	if (BreakpointHits.Contains(InBreakpoint))
	{
		BreakpointHits[InBreakpoint]++;
	}
	else
	{
		BreakpointHits.Add(InBreakpoint, 1);
	}
}

void FRigVMDebugInfo::SetBreakpointActivationOnHit(const TSharedPtr<FRigVMBreakpoint> InBreakpoint, const uint16 InActivationOnHit)
{
	if (BreakpointActivationOnHit.Contains(InBreakpoint))
	{
		BreakpointActivationOnHit[InBreakpoint] = InActivationOnHit;
	}
	else
	{
		BreakpointActivationOnHit.Add(InBreakpoint, InActivationOnHit);
	}
}

void FRigVMDebugInfo::IncrementBreakpointActivationOnHit(const TSharedPtr<FRigVMBreakpoint> InBreakpoint)
{
	if (BreakpointActivationOnHit.Contains(InBreakpoint))
	{
		BreakpointActivationOnHit[InBreakpoint]++;
	}
	else
	{
		BreakpointActivationOnHit.Add(InBreakpoint, 1);
	}
}

uint16 FRigVMDebugInfo::GetBreakpointHits(const TSharedPtr<FRigVMBreakpoint> InBreakpoint) const
{
	if (BreakpointActivationOnHit.Contains(InBreakpoint))
	{
		return BreakpointActivationOnHit[InBreakpoint];
	}
	return 0;
}
