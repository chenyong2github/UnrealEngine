// Copyright Epic Games, Inc. All Rights Reserved.

#include "TickableEditorObject.h"
#include "Tickable.h"

TArray<FTickableObjectBase::FTickableObjectEntry>& FTickableEditorObject::GetTickableObjects()
{
	static TTickableObjectsCollection TickableObjects;
	return TickableObjects;
}

TArray<FTickableEditorObject*>& FTickableEditorObject::GetPendingTickableObjects()
{
	static TArray<FTickableEditorObject*> PendingTickableObjects;
	return PendingTickableObjects;
}
