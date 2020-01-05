// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetData.h"

class USceneComponent;
class FJsonObject;
class FString;
class AActor;
struct FBox;

namespace DatasmithImportFactoryHelper
{
	TSharedPtr<FJsonObject> LoadJsonFile(const FString& JsonFilename);

	void ComputeBounds( USceneComponent* ActorComponent, FBox& Bounds );

	void CaptureSceneThumbnail(AActor* SceneActor, TArray<FAssetData>& AssetDataList);
}
