// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Curves/KeyHandle.h"
#include "Curves/RichCurve.h"
#include "MovieSceneSection.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "EntitySystem/IMovieSceneEntityProvider.h"
#include "MovieSceneFloatSection.generated.h"


/**
 * A single floating point section
 */
UCLASS( MinimalAPI )
class UMovieSceneFloatSection
	: public UMovieSceneSection
	, public IMovieSceneEntityProvider
{
	GENERATED_BODY()

	MOVIESCENETRACKS_API UMovieSceneFloatSection(const FObjectInitializer& ObjectInitializer);

public:

	/**
	 * Public access to this section's internal data function
	 */
	const FMovieSceneFloatChannel& GetChannel() const { return FloatCurve; }

protected:

	/** Float data */
	UPROPERTY()
	FMovieSceneFloatChannel FloatCurve;

private:

	virtual void ImportEntityImpl(UMovieSceneEntitySystemLinker* EntityLinker, const FEntityImportParams& Params, FImportedEntity* OutImportedEntity) override;
	virtual bool PopulateEvaluationFieldImpl(const TRange<FFrameNumber>& EffectiveRange, const FMovieSceneEvaluationFieldEntityMetaData& InMetaData, FMovieSceneEntityComponentFieldBuilder* OutFieldBuilder) override;

	virtual EMovieSceneChannelProxyType CacheChannelProxy() override;
};
