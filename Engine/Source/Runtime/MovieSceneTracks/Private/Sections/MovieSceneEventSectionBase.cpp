// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Sections/MovieSceneEventSectionBase.h"
#include "Modules/ModuleManager.h"

#if WITH_EDITOR

#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"

UMovieSceneEventSectionBase::FGenerateEventEntryPointFunctionsEvent UMovieSceneEventSectionBase::GenerateEventEntryPointsEvent;
UMovieSceneEventSectionBase::FFixupPayloadParameterNameEvent UMovieSceneEventSectionBase::FixupPayloadParameterNameEvent;
UMovieSceneEventSectionBase::FUpgradeLegacyEventEndpoint UMovieSceneEventSectionBase::UpgradeLegacyEventEndpoint;


void UMovieSceneEventSectionBase::OnPostCompile(UBlueprint* Blueprint)
{
	if (Blueprint->GeneratedClass)
	{
		for (FMovieSceneEvent& EntryPoint : GetAllEntryPoints())
		{
			if (EntryPoint.CompiledFunctionName != NAME_None)
			{
				// @todo: Validate that the function is good
				EntryPoint.Ptrs.Function = Blueprint->GeneratedClass->FindFunctionByName(EntryPoint.CompiledFunctionName);

				if (EntryPoint.Ptrs.Function && EntryPoint.BoundObjectPinName != NAME_None)
				{
					EntryPoint.Ptrs.BoundObjectProperty = EntryPoint.Ptrs.Function->FindPropertyByName(EntryPoint.BoundObjectPinName);
					check(!EntryPoint.Ptrs.BoundObjectProperty || EntryPoint.Ptrs.BoundObjectProperty->GetOuter() == EntryPoint.Ptrs.Function);
					if (Cast<UObjectProperty>(EntryPoint.Ptrs.BoundObjectProperty) || Cast<UInterfaceProperty>(EntryPoint.Ptrs.BoundObjectProperty))
					{
					}
				}
				else
				{
					EntryPoint.Ptrs.BoundObjectProperty = nullptr;
				}
			}
			else
			{
				EntryPoint.Ptrs.Function = nullptr;
				EntryPoint.Ptrs.BoundObjectProperty = nullptr;
			}

			EntryPoint.CompiledFunctionName = NAME_None;
		}

		if (!Blueprint->bIsRegeneratingOnLoad)
		{
			MarkAsChanged();
			MarkPackageDirty();
		}
	}

	Blueprint->OnCompiled().RemoveAll(this);
}

#endif

void UMovieSceneEventSectionBase::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITORONLY_DATA
	if (UBlueprint* Blueprint = DirectorBlueprint_DEPRECATED.Get())
	{
		UpgradeLegacyEventEndpoint.Broadcast(this, Blueprint);

		Blueprint->GenerateFunctionGraphsEvent.AddUniqueDynamic(this, &UMovieSceneEventSectionBase::HandleGenerateEntryPoints);

		// If the BP has already been compiled (eg regenerate on load) we must perform PostCompile fixup immediately since
		// We will not have had a chance to generate function entries. In this case we just bind directly to the already compiled functions.
		if (Blueprint->bHasBeenRegenerated)
		{
			OnPostCompile(Blueprint);
		}

		// We're done with data upgrade now
		DirectorBlueprint_DEPRECATED = nullptr;
	}
#endif
}