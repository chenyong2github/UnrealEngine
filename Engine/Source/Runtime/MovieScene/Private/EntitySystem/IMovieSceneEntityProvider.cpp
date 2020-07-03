// Copyright Epic Games, Inc. All Rights Reserved.

#include "EntitySystem/IMovieSceneEntityProvider.h"
#include "EntitySystem/MovieSceneEntitySystemTask.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/MovieSceneEntityManager.h"

#include "Evaluation/MovieSceneEvaluationField.h"

#include "MovieSceneSection.h"
namespace UE
{
namespace MovieScene
{

FMovieSceneEntityID FImportedEntity::Manufacture(const FEntityImportParams& Params, FEntityManager* EntityManager)
{
	using namespace MovieScene;

	FBuiltInComponentTypes* Components = FBuiltInComponentTypes::Get();

	auto BaseBuilder = FEntityBuilder()
	.AddTag(Components->Tags.NeedsLink)
	.AddTag(Components->Tags.ImportedEntity)
	.Add(Components->InstanceHandle, Params.Sequence.InstanceHandle);

	FComponentMask NewMask;
	BaseBuilder.GenerateType(EntityManager, NewMask);
	for (TInlineValue<IEntityBuilder>& Builder : Builders)
	{
		Builder->GenerateType(EntityManager, NewMask);
	}

	FEntityInfo NewEntity = EntityManager->AllocateEntity(NewMask);

	BaseBuilder.Initialize(EntityManager, NewEntity);
	for (TInlineValue<IEntityBuilder>& Builder : Builders)
	{
		Builder->Initialize(EntityManager, NewEntity);
	}

	return NewEntity.EntityID;
}

} // namespace MovieScene
} // namespace UE

void IMovieSceneEntityProvider::ImportEntity(UMovieSceneEntitySystemLinker* EntityLinker, const FEntityImportParams& Params, FImportedEntity* OutImportedEntity)
{
	ImportEntityImpl(EntityLinker, Params, OutImportedEntity);
}
