// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/AnimSubsystem_PropertyAccess.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"

void FAnimSubsystem_PropertyAccess::OnUpdate(FAnimSubsystemUpdateContext& InContext) const
{
	// Process internal batched property copies
	PropertyAccess::ProcessCopies(InContext.AnimInstance, Library, EPropertyAccessCopyBatch::ExternalBatched);
}

void FAnimSubsystem_PropertyAccess::OnParallelUpdate(FAnimSubsystemParallelUpdateContext& InContext) const
{
	// Process internal batched property copies
	PropertyAccess::ProcessCopies(InContext.Proxy.GetAnimInstanceObject(), Library, EPropertyAccessCopyBatch::InternalBatched);
}

void FAnimSubsystem_PropertyAccess::OnPostLoad(FAnimSubsystemPostLoadContext& InContext)
{
	// Patch the library on load to fixup property offsets
	PropertyAccess::PostLoadLibrary(Library);
}