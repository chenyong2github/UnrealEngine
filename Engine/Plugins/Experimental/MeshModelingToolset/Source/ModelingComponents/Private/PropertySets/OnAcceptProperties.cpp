// Copyright Epic Games, Inc. All Rights Reserved.

#include "PropertySets/OnAcceptProperties.h"

#include "GameFramework/Actor.h"
#include "UObject/UObjectGlobals.h"
#include "InteractiveToolManager.h"

#define LOCTEXT_NAMESPACE "UOnAcceptHandleSourcesProperties"


void UOnAcceptHandleSourcesProperties::ApplyMethod(const TArray<AActor*>& Actors, UInteractiveToolManager* ToolManager)
{
	// Hide or destroy the sources
	bool bKeepSources = OnToolAccept == EHandleSourcesMethod::KeepSources;
	if (Actors.Num() == 1 && (OnToolAccept == EHandleSourcesMethod::KeepFirstSource || OnToolAccept == EHandleSourcesMethod::KeepLastSource))
	{
		// if there's only one actor, keeping any source == keeping all sources
		bKeepSources = true;
	}
	if (!bKeepSources)
	{
		bool bDelete = OnToolAccept == EHandleSourcesMethod::DeleteSources
					|| OnToolAccept == EHandleSourcesMethod::KeepFirstSource
					|| OnToolAccept == EHandleSourcesMethod::KeepLastSource;
		if (bDelete)
		{
			ToolManager->BeginUndoTransaction(LOCTEXT("RemoveSources", "Remove Sources"));
		}
		else
		{
#if WITH_EDITOR
			ToolManager->BeginUndoTransaction(LOCTEXT("HideSources", "Hide Sources"));
#endif
		}

		int32 SkipIdx = -1;
		if (OnToolAccept == EHandleSourcesMethod::KeepFirstSource)
		{
			SkipIdx = 0;
		}
		else if (OnToolAccept == EHandleSourcesMethod::KeepLastSource)
		{
			SkipIdx = Actors.Num() - 1;
		}
		for (int32 ActorIdx = 0; ActorIdx < Actors.Num(); ActorIdx++)
		{
			if (ActorIdx == SkipIdx)
			{
				continue;
			}

			AActor* Actor = Actors[ActorIdx];
			if (bDelete)
			{
				Actor->Destroy();
			}
			else
			{
#if WITH_EDITOR
				// Save the actor to the transaction buffer to support undo/redo, but do
				// not call Modify, as we do not want to dirty the actor's package and
				// we're only editing temporary, transient values
				SaveToTransactionBuffer(Actor, false);
				Actor->SetIsTemporarilyHiddenInEditor(true);
#endif
			}
		}
		if (bDelete)
		{
			ToolManager->EndUndoTransaction();
		}
		else
		{
#if WITH_EDITOR
			ToolManager->EndUndoTransaction();
#endif
		}
	}
}

#undef LOCTEXT_NAMESPACE
