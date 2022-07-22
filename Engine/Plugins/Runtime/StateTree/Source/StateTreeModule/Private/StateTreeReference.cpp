// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeReference.h"
#include "StateTree.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

FStateTreeReference::FStateTreeReference()
{
#if WITH_EDITOR
	// Make sure any modifications to the parameters since the last sync (PostSerialize) won't cause runtime issues.
	PIEHandle = FEditorDelegates::PreBeginPIE.AddLambda([this](const bool bBegan)
	{
		if (RequiresParametersSync())
		{
			SyncParameters();
        	UE_LOG(LogStateTree, Warning, TEXT("Parameters for '%s' stored in StateTreeReference were auto-fixed to be usable at runtime."), *GetNameSafe(StateTree));	
		}
	});
#endif
}

FStateTreeReference::~FStateTreeReference()
{
#if WITH_EDITOR
	// Unregister all our delegates
	FEditorDelegates::PreBeginPIE.Remove(PIEHandle);
#endif
}

#if WITH_EDITOR
void FStateTreeReference::PostSerialize(const FArchive& Ar)
{
	if (Ar.IsLoading() && Ar.IsPersistent())
	{
		// Make sure the StateTree asset is fully loaded, so that the SyncParameters() will execute on valid data.
		if (StateTree != nullptr)
		{
			FArchive* NonConstAr = const_cast<FArchive*>(&Ar);
			NonConstAr->Preload(StateTree);
		}

		// This might modify the object but we don't want to dirty on load
		SyncParameters();
	}
}

void FStateTreeReference::SyncParameters(FInstancedPropertyBag& ParametersToSync) const
{
	if (StateTree == nullptr)
	{
		ParametersToSync.Reset();
	}
	else
	{
		ParametersToSync.MigrateToNewBagInstance(StateTree->GetDefaultParameters());
	}
}

bool FStateTreeReference::RequiresParametersSync() const
{
	return (StateTree == nullptr && Parameters.IsValid())
		|| (StateTree != nullptr && StateTree->GetDefaultParameters().GetPropertyBagStruct() != Parameters.GetPropertyBagStruct());
}
#endif // WITH_EDITOR
