// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ActorFactories/ActorFactory.h"

#include "ActorFactoryDatasmithScene.generated.h"

UCLASS(MinimalAPI)
class UActorFactoryDatasmithScene : public UActorFactory
{
	GENERATED_UCLASS_BODY()

public:
	static void SpawnRelatedActors( class ADatasmithSceneActor* DatasmithSceneActor, bool bReimportDeletedActors );

	//~ Begin UActorFactory Interface
	virtual bool CanCreateActorFrom( const FAssetData& AssetData, FText& OutErrorMsg ) override;

protected:
	virtual AActor* SpawnActor( UObject* Asset, ULevel* InLevel, const FTransform& Transform, EObjectFlags InObjectFlags, const FName Name ) override;
	//~ End UActorFactory Interface
};