// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimMetadata.h"
#include "ContextualAnimSceneAsset.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

UContextualAnimMetadata::UContextualAnimMetadata(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

}

UContextualAnimSceneAssetBase* UContextualAnimMetadata::GetSceneAssetOwner() const
{
	return Cast<UContextualAnimSceneAssetBase>(GetOuter());
}