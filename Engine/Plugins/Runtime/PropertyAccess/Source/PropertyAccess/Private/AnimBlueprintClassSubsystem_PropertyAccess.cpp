// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimBlueprintClassSubsystem_PropertyAccess.h"
#include "PropertyAccess.h"
#include "Animation/AnimInstanceProxy.h"

void UAnimBlueprintClassSubsystem_PropertyAccess::OnUpdateAnimation(UAnimInstance* InAnimInstance, FAnimInstanceSubsystemData& InSubsystemData, float InDeltaTime)
{
	// Process internal batched property copies
	PropertyAccess::ProcessCopies(InAnimInstance, PropertyAccessLibrary, EPropertyAccessCopyBatch::ExternalBatched);
}

void UAnimBlueprintClassSubsystem_PropertyAccess::OnParallelUpdateAnimation(FAnimInstanceProxy& InProxy, FAnimInstanceSubsystemData& InSubsystemData, float InDeltaTime)
{
	// Process internal batched property copies
	PropertyAccess::ProcessCopies(InProxy.GetAnimInstanceObject(), PropertyAccessLibrary, EPropertyAccessCopyBatch::InternalBatched);
}

void UAnimBlueprintClassSubsystem_PropertyAccess::PostLoadSubsystem()
{
	PropertyAccess::PostLoadLibrary(PropertyAccessLibrary);
}

void UAnimBlueprintClassSubsystem_PropertyAccess::ProcessCopies(UObject* InObject, EPropertyAccessCopyBatch InBatchType) const
{
	PropertyAccess::ProcessCopies(InObject, PropertyAccessLibrary, InBatchType);
}

void UAnimBlueprintClassSubsystem_PropertyAccess::ProcessCopy(UObject* InObject, EPropertyAccessCopyBatch InBatchType, int32 InCopyIndex, TFunctionRef<void(const FProperty*, void*)> InPostCopyOperation) const
{
	PropertyAccess::ProcessCopy(InObject, PropertyAccessLibrary, InBatchType, InCopyIndex, InPostCopyOperation);
}

void UAnimBlueprintClassSubsystem_PropertyAccess::BindEvents(UObject* InObject) const
{
	PropertyAccess::BindEvents(InObject, PropertyAccessLibrary);
}

int32 UAnimBlueprintClassSubsystem_PropertyAccess::GetEventId(const UClass* InClass, TArrayView<const FName> InPath) const
{
	return PropertyAccess::GetEventId(InClass, InPath);
}
