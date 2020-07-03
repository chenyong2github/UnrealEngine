// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Interface.h"
#include "Containers/Array.h"
#include "Misc/InlineValue.h"
#include "EntitySystem/MovieSceneEntityIDs.h"
#include "EntitySystem/MovieSceneSequenceInstanceHandle.h"
#include "Math/Range.h"
#include "Evaluation/MovieSceneCompletionMode.h"

#include "IMovieSceneEntityProvider.generated.h"

class UClass;
class UMovieSceneSection;
class UMovieSceneEntitySystemLinker;

struct FMovieSceneTimeTransform;
struct FMovieSceneEntityComponentField;


namespace UE
{
namespace MovieScene
{

struct IEntityBuilder;
struct FEntityImportParams;

class FEntityManager;


struct FImportedEntity
{
	bool IsEmpty() const
	{
		return Builders.Num() == 0;
	}

	template<typename BuilderType>
	void AddBuilder(BuilderType&& InBuilder)
	{
		Builders.Add(Forward<BuilderType>(InBuilder));
	}

	FMovieSceneEntityID Manufacture(const FEntityImportParams& Params, FEntityManager* EntityManager);

private:

	TArray<TInlineValue<IEntityBuilder>, TInlineAllocator<1>> Builders;
};

struct FEntityImportSequenceParams
{
	UE::MovieScene::FInstanceHandle InstanceHandle;

	EMovieSceneCompletionMode DefaultCompletionMode;

	int32 HierarchicalBias;
};

struct FEntityImportParams
{
	FGuid ObjectBindingID;

	uint32 EntityID;

	FEntityImportSequenceParams Sequence;
};

} // namespace MovieScene
} // namespace UE


UINTERFACE()
class MOVIESCENE_API UMovieSceneEntityProvider : public UInterface
{
public:
	GENERATED_BODY()
};

/**
 * Interface to be added to UMovieSceneSection types when they contain entity data
 */
class MOVIESCENE_API IMovieSceneEntityProvider
{
public:

	using FEntityImportParams   = UE::MovieScene::FEntityImportParams;
	using FImportedEntity       = UE::MovieScene::FImportedEntity;


	GENERATED_BODY()


	/**
	 * Populate an evaluation field with this provider's entities
	 */
	bool PopulateEvaluationField(const TRange<FFrameNumber>& EffectiveRange, FMovieSceneEntityComponentField* OutField)
	{
		return PopulateEvaluationFieldImpl(EffectiveRange, OutField);
	}


	void ImportEntity(UMovieSceneEntitySystemLinker* EntityLinker, const FEntityImportParams& Params, FImportedEntity* OutImportedEntity);

private:

	virtual void ImportEntityImpl(UMovieSceneEntitySystemLinker* EntityLinker, const FEntityImportParams& Params, FImportedEntity* OutImportedEntity) = 0;

	/** Optional user-implementation function for populating an evaluation entity field */
	virtual bool PopulateEvaluationFieldImpl(const TRange<FFrameNumber>& EffectiveRange, FMovieSceneEntityComponentField* Field) { return false; }
};