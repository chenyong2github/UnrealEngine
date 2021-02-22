// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ContextualAnimManager.generated.h"

class UContextualAnimSceneAssetBase;
class UContextualAnimSceneInstance;
class UWorld;

UCLASS()
class CONTEXTUALANIMATION_API UContextualAnimManager : public UObject
{
	GENERATED_BODY()

public:

	UContextualAnimManager(const FObjectInitializer& ObjectInitializer);

	virtual UWorld* GetWorld() const override;

	void Tick(float DeltaTime);

	bool TryStartScene(const UContextualAnimSceneAssetBase* SceneAsset, const TMap<FName, AActor*>& Bindings);

	bool TryStopSceneWithActor(AActor* Actor);

	bool IsActorInAnyScene(AActor* Actor) const;

	UContextualAnimSceneInstance* GetSceneWithActor(AActor* Actor);

	static UContextualAnimManager* Get(const UWorld* World);

protected:

	UPROPERTY()
	TArray<UContextualAnimSceneInstance*> Instances;

	void OnSceneInstanceEnded(UContextualAnimSceneInstance* SceneInstance);
};