// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"

#include "DatasmithSceneActor.generated.h"

UCLASS()
class DATASMITHCONTENT_API ADatasmithSceneActor : public AActor
{
	GENERATED_BODY()

public:

	ADatasmithSceneActor();

	virtual ~ADatasmithSceneActor();

	UPROPERTY(VisibleAnywhere, Category="Datasmith")
	class UDatasmithScene* Scene;

	/** Map of all the actors related to this Datasmith Scene */
	UPROPERTY(VisibleAnywhere, Category="Datasmith", AdvancedDisplay)
	TMap< FName, TSoftObjectPtr< AActor > > RelatedActors;

private:
#if WITH_EDITOR
	// Cleans the invalid Soft Object Ptr
	void OnActorDeleted(AActor* ActorDeleted);

	FDelegateHandle OnActorDeletedDelegateHandle;
#endif // WITH_EDITOR
};
