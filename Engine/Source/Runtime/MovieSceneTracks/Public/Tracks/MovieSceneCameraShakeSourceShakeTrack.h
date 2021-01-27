// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Templates/SubclassOf.h"
#include "Camera/CameraShakeBase.h"
#include "Misc/InlineValue.h"
#include "MovieSceneNameableTrack.h"
#include "Compilation/IMovieSceneTrackTemplateProducer.h"
#include "MovieSceneCameraShakeSourceShakeTrack.generated.h"

struct FMovieSceneEvaluationTrack;
struct FMovieSceneSegmentCompilerRules;
class UCameraShakeSourceComponent;

/**
 * 
 */
UCLASS(MinimalAPI)
class UMovieSceneCameraShakeSourceShakeTrack : public UMovieSceneNameableTrack, public IMovieSceneTrackTemplateProducer
{
	GENERATED_BODY()

public:
	MOVIESCENETRACKS_API UMovieSceneSection* AddNewCameraShake(const FFrameNumber KeyTime, const UCameraShakeSourceComponent& ShakeSourceComponent);
	MOVIESCENETRACKS_API UMovieSceneSection* AddNewCameraShake(const FFrameNumber KeyTime, const TSubclassOf<UCameraShakeBase> ShakeClass, bool bIsAutomaticShake);
	
public:
	// UMovieSceneTrack interface
	virtual bool HasSection(const UMovieSceneSection& Section) const override;
	virtual void AddSection(UMovieSceneSection& Section) override;
	virtual void RemoveSection(UMovieSceneSection& Section) override;
	virtual void RemoveSectionAt(int32 SectionIndex) override;
	virtual bool IsEmpty() const override;
	virtual bool SupportsMultipleRows() const override { return true; }
	virtual const TArray<UMovieSceneSection*>& GetAllSections() const override;
	virtual bool SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const override;
	virtual UMovieSceneSection* CreateNewSection() override;
	virtual void RemoveAllAnimationData() override;

	virtual FMovieSceneEvalTemplatePtr CreateTemplateForSection(const UMovieSceneSection& InSection) const override;

#if WITH_EDITORONLY_DATA
	virtual FText GetDisplayName() const override;
#endif
	
private:
	/** List of all sections */
	UPROPERTY()
	TArray<TObjectPtr<UMovieSceneSection>> CameraShakeSections;
};

