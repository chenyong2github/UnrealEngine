// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MovieScene.h"
#include "MovieSceneTrack.h"
#include "MovieSceneSection.h"
#include "MovieSceneSequence.h"
#include "Evaluation/MovieSceneEvalTemplate.h"
#include "Sections/MovieSceneSubSection.h"
#include "Tracks/MovieSceneSubTrack.h"
#include "Compilation/IMovieSceneTrackTemplateProducer.h"
#include "MovieSceneTestObjects.generated.h"

USTRUCT()
struct FTestMovieSceneEvalTemplate : public FMovieSceneEvalTemplate
{
	GENERATED_BODY()

	virtual UScriptStruct& GetScriptStructImpl() const { return *StaticStruct(); }
};

UCLASS(MinimalAPI)
class UTestMovieSceneTrack : public UMovieSceneTrack, public IMovieSceneTrackTemplateProducer
{
public:

	GENERATED_BODY()

	virtual const TArray<UMovieSceneSection*>& GetAllSections() const override { return SectionArray; }
	virtual FMovieSceneEvalTemplatePtr CreateTemplateForSection(const UMovieSceneSection& InSection) const override;

	UPROPERTY()
	bool bHighPassFilter;

	UPROPERTY()
	TArray<UMovieSceneSection*> SectionArray;
};

UCLASS(MinimalAPI)
class UTestMovieSceneSection : public UMovieSceneSection
{
public:
	GENERATED_BODY()
};

UCLASS(MinimalAPI)
class UTestMovieSceneSequence : public UMovieSceneSequence
{
public:
	GENERATED_BODY()

	UTestMovieSceneSequence(const FObjectInitializer& ObjInit)
		: Super(ObjInit)
	{
		if (!HasAnyFlags(RF_ClassDefaultObject))
		{
			MovieScene = ObjInit.CreateDefaultSubobject<UMovieScene>(this, "MovieScene");
		}
	}

	//~ UMovieSceneSequence interface
	virtual void BindPossessableObject(const FGuid& ObjectId, UObject& PossessedObject, UObject* Context) override {}
	virtual void LocateBoundObjects(const FGuid& ObjectId, UObject* Context, TArray<UObject*, TInlineAllocator<1>>& OutObjects) const override {}
	virtual UObject* GetParentObject(UObject* Object) const override { return nullptr; }
	virtual void UnbindPossessableObjects(const FGuid& ObjectId) override {}
	virtual void UnbindObjects(const FGuid& ObjectId, const TArray<UObject*>& InObjects, UObject* Context) override {}
	virtual void UnbindInvalidObjects(const FGuid& ObjectId, UObject* Context) override {}
	virtual UMovieScene* GetMovieScene() const override { return MovieScene; }

	UPROPERTY()
	UMovieScene* MovieScene;
};

UCLASS(MinimalAPI)
class UTestMovieSceneSubTrack : public UMovieSceneSubTrack
{
public:
	GENERATED_BODY()

	virtual const TArray<UMovieSceneSection*>& GetAllSections() const override { return SectionArray; }

	UPROPERTY()
	TArray<UMovieSceneSection*> SectionArray;
};

UCLASS(MinimalAPI)
class UTestMovieSceneSubSection : public UMovieSceneSubSection
{
	GENERATED_BODY()
};

