// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "MovieSceneTrack.h"
#include "Compilation/IMovieSceneTrackTemplateProducer.h"
#include "MovieSceneCameraShakeSourceTriggerTrack.generated.h"

UCLASS()
class MOVIESCENETRACKS_API UMovieSceneCameraShakeSourceTriggerTrack
	: public UMovieSceneTrack
	, public IMovieSceneTrackTemplateProducer
{
public:

	GENERATED_BODY()

	UMovieSceneCameraShakeSourceTriggerTrack(const FObjectInitializer& Obj);

	// UMovieSceneTrack interface
	virtual bool SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const override;
	virtual UMovieSceneSection* CreateNewSection() override;
	virtual bool HasSection(const UMovieSceneSection& Section) const override;
	virtual void AddSection(UMovieSceneSection& Section) override;
	virtual void RemoveSection(UMovieSceneSection& Section) override;
	virtual void RemoveSectionAt(int32 SectionIndex) override;
	virtual void RemoveAllAnimationData() override;
	virtual bool IsEmpty() const override;
	virtual const TArray<UMovieSceneSection*>& GetAllSections() const override;
	virtual void PostCompile(FMovieSceneEvaluationTrack& Track, const FMovieSceneTrackCompilerArgs& Args) const override;

	virtual FMovieSceneEvalTemplatePtr CreateTemplateForSection(const UMovieSceneSection& InSection) const override;

#if WITH_EDITORONLY_DATA
	virtual FText GetDisplayName() const override;
#endif

protected:

	/** All the sections in this track */
	UPROPERTY()
	TArray<TObjectPtr<UMovieSceneSection>> Sections;
};

