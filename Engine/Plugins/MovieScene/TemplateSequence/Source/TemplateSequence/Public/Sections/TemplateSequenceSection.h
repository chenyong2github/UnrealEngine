// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "EntitySystem/IMovieSceneEntityProvider.h"
#include "EntitySystem/MovieSceneEntityIDs.h"
#include "Sections/MovieSceneSubSection.h"
#include "TemplateSequenceSection.generated.h"

UCLASS(MinimalAPI)
class UTemplateSequenceSection 
	: public UMovieSceneSubSection
	, public IMovieSceneEntityProvider
{
public:

	GENERATED_BODY()

	UTemplateSequenceSection(const FObjectInitializer& ObjInitializer);

	// UMovieSceneSection interface
	virtual void OnDilated(float DilationFactor, FFrameNumber Origin) override;

private:

	virtual void ImportEntityImpl(UMovieSceneEntitySystemLinker* EntityLinker, const FEntityImportParams& Params, FImportedEntity* OutImportedEntity) override;
};
