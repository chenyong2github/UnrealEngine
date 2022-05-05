// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeReference.h"
#include "StateTree.h"
#include "StateTreeDelegates.h"
#if WITH_EDITOR
#include "Editor.h"
#endif

#if WITH_EDITOR
void UStateTreeReferenceWrapper::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	UObject::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.MemberProperty && PropertyChangedEvent.Property)
	{
		if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(FStateTreeReference, StateTree))
		{
			StateTreeReference.SyncParameters();
		}
	}
}

void UStateTreeReferenceWrapper::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	UObject::PostEditChangeChainProperty(PropertyChangedEvent);
}

void UStateTreeReferenceWrapper::PostInitProperties()
{
	UObject::PostInitProperties();

	// Registers a delegate to be notified when the associated StateTree asset get successfully recompiled
	// to make sure that the parameters in the StateTreeReference are still valid.
	PostCompileHandle = UE::StateTree::Delegates::OnPostCompile.AddLambda([this](const UStateTree& InStateTree)
	{
		if (StateTreeReference.StateTree == &InStateTree)
		{
			if (StateTreeReference.SyncParameters())
			{
				MarkPackageDirty();
			}
		}
	});

	// Make sure any modifications to the parameters since the last sync (PostLoad) won't cause runtime issues.
	// This shouldn't be necessary if only parameters values could be modified but until that is enforce this will
	// prevent parameters inconsistencies at runtime.
	PIEHandle = FEditorDelegates::PreBeginPIE.AddLambda([this](const bool bBegan)
	{
		const bool bModified = StateTreeReference.SyncParameters();
		UE_CLOG(bModified, LogStateTree, Warning, TEXT("Parameters for StateTree '%s' stored in %s were auto-fixed to be usable at runtime."),
			*GetNameSafe(StateTreeReference.StateTree), *GetName());
	});
}

void UStateTreeReferenceWrapper::PostLoad()
{
	UObject::PostLoad();

	// This might modify the object but we don't want to dirty on load
	StateTreeReference.SyncParameters();
}

void UStateTreeReferenceWrapper::BeginDestroy()
{
	UObject::BeginDestroy();

	// Unregister all our delegates
	UE::StateTree::Delegates::OnPostCompile.Remove(PostCompileHandle);
	FEditorDelegates::PreBeginPIE.Remove(PIEHandle);
}

bool FStateTreeReference::SyncParameters()
{
	if (StateTree == nullptr)
	{
		const bool bHadParameters = Parameters.IsValid();
		Parameters.Reset();
		return bHadParameters;
	}

	const FInstancedPropertyBag& DefaultParameters = StateTree->GetDefaultParameters();
	const bool bAreParametersSynced = DefaultParameters.GetPropertyBagStruct() == Parameters.GetPropertyBagStruct();
	if (!bAreParametersSynced)
	{
		Parameters.MigrateToNewBagInstance(DefaultParameters);	
	}

	return !bAreParametersSynced;
}
#endif // WITH_EDITOR
