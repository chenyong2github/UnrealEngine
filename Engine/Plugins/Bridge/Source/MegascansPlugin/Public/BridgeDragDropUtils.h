// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"

DECLARE_DELEGATE_ThreeParams(FOnAddProgressiveStageDataCallback, FAssetData AssetData, FString AssetId, AStaticMeshActor* SpawnedActor);

class FBridgeDragDropImpl;

class MEGASCANSPLUGIN_API FBridgeDragDropImpl : public TSharedFromThis<FBridgeDragDropImpl>
{
public:
    FOnAddProgressiveStageDataCallback OnAddProgressiveStageDataDelegate;
    
    void SetOnAddProgressiveStageData(FOnAddProgressiveStageDataCallback InDelegate);
};

class MEGASCANSPLUGIN_API FBridgeDragDrop
{
public:
	static void Initialize();
	static TSharedPtr<FBridgeDragDropImpl> Instance;
};
