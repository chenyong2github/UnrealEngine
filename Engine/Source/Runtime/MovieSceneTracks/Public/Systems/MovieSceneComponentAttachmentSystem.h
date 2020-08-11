// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EntitySystem/MovieSceneEntityInstantiatorSystem.h"
#include "MovieSceneTracksComponentTypes.h"
#include "EntitySystem/MovieScenePreAnimatedStateSystem.h"
#include "EntitySystem/MovieSceneOverlappingEntityTracker.h"

#include "MovieSceneComponentAttachmentSystem.generated.h"

struct FMovieSceneAnimTypeID;

class USceneComponent;
class UMovieScenePreAnimatedComponentTransformSystem;

namespace UE
{
namespace MovieScene
{

struct FPreAnimAttachment
{
	TWeakObjectPtr<USceneComponent> OldAttachParent;
	FName OldAttachSocket;
	UE::MovieScene::FComponentDetachParams DetachParams;
};

} // namespace MovieScene
} // namespace UE


UCLASS(MinimalAPI)
class UMovieSceneComponentAttachmentInvalidatorSystem
	: public UMovieSceneEntityInstantiatorSystem
{
public:

	GENERATED_BODY()

	UMovieSceneComponentAttachmentInvalidatorSystem(const FObjectInitializer& ObjInit);

private:

	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override final;
};

UCLASS(MinimalAPI)
class UMovieSceneComponentAttachmentSystem
	: public UMovieSceneEntityInstantiatorSystem
	, public IMovieScenePreAnimatedStateSystemInterface
{
public:

	GENERATED_BODY()

	UMovieSceneComponentAttachmentSystem(const FObjectInitializer& ObjInit);

	void AddPendingDetach(USceneComponent* SceneComponent, const UE::MovieScene::FPreAnimAttachment& Attachment);

	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

private:

	virtual void OnLink() override final;
	virtual void OnUnlink() override final;
	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override final;

	//~ IMovieScenePreAnimatedStateSystemInterface interface
	virtual void SaveGlobalPreAnimatedState(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override;
	virtual void RestorePreAnimatedState(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override;

	void TagGarbage(UMovieSceneEntitySystemLinker*);

	UE::MovieScene::TOverlappingEntityTracker_BoundObject<UE::MovieScene::FPreAnimAttachment> AttachmentTracker;

	TArray<TTuple<USceneComponent*, UE::MovieScene::FPreAnimAttachment>> PendingAttachmentsToRestore;
};



