// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ISequencer.h"
#include "ISequencerSection.h"
#include "Camera/CameraShakeBase.h"

/**
 * Section interface for shake sections
 */
class FCameraShakeSectionBase : public ISequencerSection
{
public:
	FCameraShakeSectionBase(const TSharedPtr<ISequencer> InSequencer, UMovieSceneSection& InSection, const FGuid& InObjectBindingId);

	virtual FText GetSectionTitle() const override;
	virtual UMovieSceneSection* GetSectionObject() override;
	virtual bool IsReadOnly() const override;
	virtual int32 OnPaintSection(FSequencerSectionPainter& Painter) const override;

protected:
	virtual TSubclassOf<UCameraShakeBase> GetCameraShakeClass() const = 0;

	TSharedPtr<ISequencer> GetSequencer() const;
	FGuid GetObjectBinding() const;

	template<typename SectionClass>
	SectionClass* GetSectionObjectAs() const
	{
		return Cast<SectionClass>(SectionPtr.Get());
	}

private:
	const UCameraShakeBase* GetCameraShakeDefaultObject() const;

private:
	TWeakPtr<ISequencer> SequencerPtr;
	TWeakObjectPtr<UMovieSceneSection> SectionPtr;
	FGuid ObjectBindingId;
};

