// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Interfaces/TypedElementWorldInterface.h"

#include "Elements/Framework/TypedElementRegistry.h"

#include "UObject/Stack.h"

bool ITypedElementWorldInterface::IsTemplateElement(const FScriptTypedElementHandle& InElementHandle)
{
	FTypedElementHandle NativeHandle = InElementHandle.GetTypedElementHandle();
	if (!NativeHandle)
	{
		FFrame::KismetExecutionMessage(TEXT("InElementHandle is not a valid handle."), ELogVerbosity::Error);
		return false;
	}

	return IsTemplateElement(NativeHandle);
}

bool ITypedElementWorldInterface::CanEditElement(const FScriptTypedElementHandle& InElementHandle)
{
	FTypedElementHandle NativeHandle = InElementHandle.GetTypedElementHandle();
	if (!NativeHandle)
	{
		FFrame::KismetExecutionMessage(TEXT("InElementHandle is not a valid handle."), ELogVerbosity::Error);
		return false;
	}

	return CanEditElement(NativeHandle);
}

ULevel* ITypedElementWorldInterface::GetOwnerLevel(const FScriptTypedElementHandle& InElementHandle)
{
	FTypedElementHandle NativeHandle = InElementHandle.GetTypedElementHandle();
	if (!NativeHandle)
	{
		FFrame::KismetExecutionMessage(TEXT("InElementHandle is not a valid handle."), ELogVerbosity::Error);
		return nullptr;
	}

	return GetOwnerLevel(NativeHandle);
}

UWorld* ITypedElementWorldInterface::GetOwnerWorld(const FScriptTypedElementHandle& InElementHandle)
{
	FTypedElementHandle NativeHandle = InElementHandle.GetTypedElementHandle();
	if (!NativeHandle)
	{
		FFrame::KismetExecutionMessage(TEXT("InElementHandle is not a valid handle."), ELogVerbosity::Error);
		return nullptr;
	}

	return GetOwnerWorld(NativeHandle);
}

bool ITypedElementWorldInterface::GetBounds(const FScriptTypedElementHandle& InElementHandle, FBoxSphereBounds& OutBounds)
{
	FTypedElementHandle NativeHandle = InElementHandle.GetTypedElementHandle();
	if (!NativeHandle)
	{
		FFrame::KismetExecutionMessage(TEXT("InElementHandle is not a valid handle."), ELogVerbosity::Error);
		return false;
	}

	return GetBounds(NativeHandle, OutBounds);
}


bool ITypedElementWorldInterface::CanMoveElement(const FScriptTypedElementHandle& InElementHandle, const ETypedElementWorldType InWorldType)
{
	FTypedElementHandle NativeHandle = InElementHandle.GetTypedElementHandle();
	if (!NativeHandle)
	{
		FFrame::KismetExecutionMessage(TEXT("InElementHandle is not a valid handle."), ELogVerbosity::Error);
		return false;
	}

	return CanMoveElement(NativeHandle, InWorldType);
}

bool ITypedElementWorldInterface::GetWorldTransform(const FScriptTypedElementHandle& InElementHandle, FTransform& OutTransform)
{
	FTypedElementHandle NativeHandle = InElementHandle.GetTypedElementHandle();
	if (!NativeHandle)
	{
		FFrame::KismetExecutionMessage(TEXT("InElementHandle is not a valid handle."), ELogVerbosity::Error);
		return false;
	}

	return GetWorldTransform(NativeHandle, OutTransform);
}

bool ITypedElementWorldInterface::SetWorldTransform(const FScriptTypedElementHandle& InElementHandle, const FTransform& InTransform)
{
	FTypedElementHandle NativeHandle = InElementHandle.GetTypedElementHandle();
	if (!NativeHandle)
	{
		FFrame::KismetExecutionMessage(TEXT("InElementHandle is not a valid handle."), ELogVerbosity::Error);
		return false;
	}

	return SetWorldTransform(NativeHandle, InTransform);
}

bool ITypedElementWorldInterface::GetRelativeTransform(const FScriptTypedElementHandle& InElementHandle, FTransform& OutTransform)
{
	FTypedElementHandle NativeHandle = InElementHandle.GetTypedElementHandle();
	if (!NativeHandle)
	{
		FFrame::KismetExecutionMessage(TEXT("InElementHandle is not a valid handle."), ELogVerbosity::Error);
		return false;
	}

	return GetRelativeTransform(NativeHandle, OutTransform);
}

bool ITypedElementWorldInterface::SetRelativeTransform(const FScriptTypedElementHandle& InElementHandle, const FTransform& InTransform)
{
	FTypedElementHandle NativeHandle = InElementHandle.GetTypedElementHandle();
	if (!NativeHandle)
	{
		FFrame::KismetExecutionMessage(TEXT("InElementHandle is not a valid handle."), ELogVerbosity::Error);
		return false;
	}

	return SetRelativeTransform(NativeHandle, InTransform);
}

bool ITypedElementWorldInterface::GetPivotOffset(const FScriptTypedElementHandle& InElementHandle, FVector& OutPivotOffset)
{
	FTypedElementHandle NativeHandle = InElementHandle.GetTypedElementHandle();
	if (!NativeHandle)
	{
		FFrame::KismetExecutionMessage(TEXT("InElementHandle is not a valid handle."), ELogVerbosity::Error);
		return false;
	}

	return GetPivotOffset(NativeHandle, OutPivotOffset);
}

bool ITypedElementWorldInterface::SetPivotOffset(const FScriptTypedElementHandle& InElementHandle, const FVector& InPivotOffset)
{
	FTypedElementHandle NativeHandle = InElementHandle.GetTypedElementHandle();
	if (!NativeHandle)
	{
		FFrame::KismetExecutionMessage(TEXT("InElementHandle is not a valid handle."), ELogVerbosity::Error);
		return false;
	}

	return SetPivotOffset(NativeHandle, InPivotOffset);
}


void ITypedElementWorldInterface::NotifyMovementStarted(const FScriptTypedElementHandle& InElementHandle)
{
	FTypedElementHandle NativeHandle = InElementHandle.GetTypedElementHandle();
	if (!NativeHandle)
	{
		FFrame::KismetExecutionMessage(TEXT("InElementHandle is not a valid handle."), ELogVerbosity::Error);
		return;
	}

	NotifyMovementStarted(NativeHandle);
}


void ITypedElementWorldInterface::NotifyMovementOngoing(const FScriptTypedElementHandle& InElementHandle)
{
	FTypedElementHandle NativeHandle = InElementHandle.GetTypedElementHandle();
	if (!NativeHandle)
	{
		FFrame::KismetExecutionMessage(TEXT("InElementHandle is not a valid handle."), ELogVerbosity::Error);
		return;
	}

	NotifyMovementOngoing(NativeHandle);
}

void ITypedElementWorldInterface::NotifyMovementEnded(const FScriptTypedElementHandle& InElementHandle)
{
	FTypedElementHandle NativeHandle = InElementHandle.GetTypedElementHandle();
	if (!NativeHandle)
	{
		FFrame::KismetExecutionMessage(TEXT("InElementHandle is not a valid handle."), ELogVerbosity::Error);
		return;
	}

	NotifyMovementEnded(NativeHandle);
}

bool ITypedElementWorldInterface::CanDeleteElement(const FScriptTypedElementHandle& InElementHandle)
{
	FTypedElementHandle NativeHandle = InElementHandle.GetTypedElementHandle();
	if (!NativeHandle)
	{
		FFrame::KismetExecutionMessage(TEXT("InElementHandle is not a valid handle."), ELogVerbosity::Error);
		return false;
	}

	return CanDeleteElement(NativeHandle);
}

bool ITypedElementWorldInterface::DeleteElement(const FScriptTypedElementHandle& InElementHandle, UWorld* InWorld, UTypedElementSelectionSet* InSelectionSet, const FTypedElementDeletionOptions& InDeletionOptions)
{
	FTypedElementHandle NativeHandle = InElementHandle.GetTypedElementHandle();
	if (!NativeHandle)
	{
		FFrame::KismetExecutionMessage(TEXT("InElementHandle is not a valid handle."), ELogVerbosity::Error);
		return false;
	}

	if (!InWorld)
	{
		FFrame::KismetExecutionMessage(TEXT("InWorld is null"), ELogVerbosity::Error);
		return false;
	}

	if (!InSelectionSet)
	{
		FFrame::KismetExecutionMessage(TEXT("InSelectionSet is null"), ELogVerbosity::Error);
		return false;
	}

	return DeleteElement(NativeHandle, InWorld, InSelectionSet, InDeletionOptions);
}

bool ITypedElementWorldInterface::CanDuplicateElement(const FScriptTypedElementHandle& InElementHandle)
{
	FTypedElementHandle NativeHandle = InElementHandle.GetTypedElementHandle();
	if (!NativeHandle)
	{
		FFrame::KismetExecutionMessage(TEXT("InElementHandle is not a valid handle."), ELogVerbosity::Error);
		return false;
	}

	return CanDuplicateElement(NativeHandle);
}

FScriptTypedElementHandle ITypedElementWorldInterface::DuplicateElement(const FScriptTypedElementHandle& InElementHandle, UWorld* InWorld, const FVector& InLocationOffset)
{
	FTypedElementHandle NativeHandle = InElementHandle.GetTypedElementHandle();
	if (!NativeHandle)
	{
		FFrame::KismetExecutionMessage(TEXT("InElementHandle is not a valid handle."), ELogVerbosity::Error);
		return FScriptTypedElementHandle();
	}

	if (!InWorld)
	{
		FFrame::KismetExecutionMessage(TEXT("InWorld is null"), ELogVerbosity::Error);
		return FScriptTypedElementHandle();
	}

	return GetRegistry().CreateScriptHandle(DuplicateElement(NativeHandle, InWorld, InLocationOffset).GetId());
}

class UTypedElementRegistry& ITypedElementWorldInterface::GetRegistry() const
{
	return *UTypedElementRegistry::GetInstance();
}
