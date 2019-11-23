// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Blueprint/BlueprintExtension.h"

#include "MovieSceneEventBlueprintExtension.generated.h"

class UBlueprint;
class FKismetCompilerContext;
class UMovieSceneEventSectionBase;

UCLASS()
class UMovieSceneEventBlueprintExtension : public UBlueprintExtension
{
public:

	GENERATED_BODY()

	void Add(UMovieSceneEventSectionBase* EventSection)
	{
		EventSections.AddUnique(EventSection);
	}

private:

	virtual void PostLoad() override final;
	virtual void HandlePreloadObjectsForCompilation(UBlueprint* OwningBlueprint) override final;
	virtual void HandleGenerateFunctionGraphs(FKismetCompilerContext* CompilerContext) override final;

	/** List of event sections that are bound to the blueprint */
	UPROPERTY()
	TArray<UMovieSceneEventSectionBase*> EventSections;
};