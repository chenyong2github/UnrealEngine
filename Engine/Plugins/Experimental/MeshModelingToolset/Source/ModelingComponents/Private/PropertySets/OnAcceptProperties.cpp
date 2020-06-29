// Copyright Epic Games, Inc. All Rights Reserved.

#include "PropertySets/OnAcceptProperties.h"

#include "GameFramework/Actor.h"
#include "UObject/UObjectGlobals.h"
#include "InteractiveToolManager.h"

#define LOCTEXT_NAMESPACE "UOnAcceptHandleSourcesProperties"


void UOnAcceptHandleSourcesProperties::ApplyMethod(const TArray<AActor*>& Actors, UInteractiveToolManager* ToolManager)
{
	// Hide or destroy the sources
	if (OnToolAccept != EHandleSourcesMethod::KeepSources)
	{
		bool bDelete = OnToolAccept == EHandleSourcesMethod::DeleteSources;
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

		for (AActor* Actor : Actors)
		{
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
