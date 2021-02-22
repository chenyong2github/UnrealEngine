// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Templates/SubclassOf.h"
#include "ContextualAnimPreviewManager.generated.h"

class ACharacter;
class UContextualAnimSceneAsset;

UCLASS()
class UContextualAnimPreviewManager : public UObject
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = "Preview")
	TSubclassOf<ACharacter> DefaultPreviewClass;

	UPROPERTY(EditAnywhere, Category = "Debug")
	bool bDrawDebugScene;

	UPROPERTY(EditAnywhere, Category = "Debug", meta = (EditCondition = "bDrawDebugScene"))
	float Time;

	UPROPERTY(EditAnywhere, Category = "Debug", meta = (EditCondition = "bDrawDebugScene"))
	FTransform ScenePivot;

	UPROPERTY()
	TWeakObjectPtr<ACharacter> TestCharacter;

	UPROPERTY()
	TMap<FName, AActor*> PreviewActors;

	UContextualAnimPreviewManager(const FObjectInitializer& ObjectInitializer);

	void SpawnPreviewActors(const UContextualAnimSceneAsset* SceneAsset, const FTransform& SceneOrigin);

	AActor* SpawnPreviewActor(UClass* Class, const FTransform& SpawnTransform) const;

	void MoveForward(float Value);

	void MoveRight(float Value);

	void MoveToLocation(const FVector& GoalLocation);
};