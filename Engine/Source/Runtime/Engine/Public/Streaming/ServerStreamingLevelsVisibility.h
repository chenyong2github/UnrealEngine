// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "GameFramework/Actor.h"
#include "ServerStreamingLevelsVisibility.generated.h"

class ULevelStreaming;

/**
 * Actor used to replicate server's visible level streaming
 */
UCLASS(notplaceable, transient)
class ENGINE_API AServerStreamingLevelsVisibility : public AActor
{
	GENERATED_UCLASS_BODY()

public:
	static AServerStreamingLevelsVisibility* SpawnServerActor(UWorld* World);
	bool Contains(const FName& InPackageName) const;
	void SetIsVisible(ULevelStreaming* InStreamingLevel, bool bInIsVisible);
	ULevelStreaming* GetVisibleStreamingLevel(const FName& InPackageName) const;
private:
	TMap<FName, TWeakObjectPtr<ULevelStreaming>> ServerVisibleStreamingLevels;
};