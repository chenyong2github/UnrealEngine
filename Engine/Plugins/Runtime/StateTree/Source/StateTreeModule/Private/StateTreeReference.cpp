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
	TArray<FStateTreeParameterDesc>& Params = Parameters.Parameters;
	if (StateTree == nullptr)
	{
		const bool bHadParameters = !Params.IsEmpty();
		Params.Reset();
		return bHadParameters;
	}

	// First implementation is to make sure that the StateTreeReference holds values for each parameter of the StateTree.
	// This is the easiest way to expose them to the user but that also means that user can't specify only a subset of parameters
	// and use default value for the others.
	const TConstArrayView<FStateTreeParameterDesc> AssetParameters = StateTree ? StateTree->GetParameterDescs() : TConstArrayView<FStateTreeParameterDesc>();
	bool bAreParametersSynced = AssetParameters.Num() == Params.Num();
	if (bAreParametersSynced)
	{
		for (int32 i = 0; i < Params.Num(); ++i)
		{
			if (!Params[i].IsMatching(AssetParameters[i]))
			{
				// Ok to mismatch only by the name if both ID and Type match. Simply update it since
				// it must also match when passed to the ExecutionContext at runtime.
				if (Params[i].ID == AssetParameters[i].ID && Params[i].IsSameType(AssetParameters[i]))
				{
					UE_LOG(LogStateTree, Warning, TEXT("StateTree parameter '%s' name was updated to match StateTree parameter '%s'."),
						*LexToString(Params[i]), *LexToString(AssetParameters[i]));
					
					Params[i].Name = AssetParameters[i].Name;
					checkf(Params[i].IsMatching(AssetParameters[i]),
						TEXT("After fixing the name the parameters should match."
							 " If this fails it indicates that same ID+Type is no longer the only required condition."));
				}
				else
				{
					bAreParametersSynced = false;
					break;	
				}
			}
		}
	}

	if (!bAreParametersSynced)
	{
		TArray<FStateTreeParameterDesc> DeprecatedParameters = MoveTemp(Params);
		Params = AssetParameters;

		for (int32 i = 0; i < Params.Num(); ++i)
		{
			FStateTreeParameterDesc& ParameterDesc = Params[i];

			// Find parameter corresponding index in the deprecated parameters using 'Type' and 'Id'.
			// Not using IsMatching() here since 'Name' might have change in addition to parameters added/removed since the last sync.
			const int32 IndexInDeprecated = DeprecatedParameters.IndexOfByPredicate([&ParameterDesc](const FStateTreeParameterDesc& DeprecatedDesc)
				{
					return DeprecatedDesc.ID == ParameterDesc.ID && DeprecatedDesc.IsSameType(ParameterDesc);
				});

			if (IndexInDeprecated != INDEX_NONE)
			{
				// Only update value
				ParameterDesc.Parameter = DeprecatedParameters[IndexInDeprecated].Parameter;
				DeprecatedParameters.RemoveAtSwap(IndexInDeprecated);
			}
		}
	}

	return !bAreParametersSynced;
}
#endif // WITH_EDITOR
