// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Tickable.h"
#include "ContextualAnimTypes.h"
#include "ContextualAnimManager.generated.h"

class UContextualAnimSceneActorComponent;
class UContextualAnimSceneAsset;
class UContextualAnimSceneInstance;
class AActor;
class UWorld;

UCLASS()
class CONTEXTUALANIMATION_API UContextualAnimManager : public UObject, public FTickableGameObject
{
	GENERATED_BODY()

public:

	UContextualAnimManager(const FObjectInitializer& ObjectInitializer);

	virtual UWorld* GetWorld() const override;

	// FTickableGameObject begin
	virtual UWorld* GetTickableGameObjectWorld() const override { return GetWorld(); }
	virtual void Tick(float DeltaTime) override;
	virtual ETickableTickType GetTickableTickType() const override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableInEditor() const { return true; }
	// FTickableGameObject end

	static UContextualAnimManager* Get(const UWorld* World);

	void RegisterSceneActorComponent(UContextualAnimSceneActorComponent* SceneActorComp);

	void UnregisterSceneActorComponent(UContextualAnimSceneActorComponent* SceneActorComp);

	/** Attempt to start an scene instance with the supplied bindings for each role */
	bool TryStartScene(const UContextualAnimSceneAsset* SceneAsset, const FContextualAnimSceneBindings& Bindings);

	/** Attempt to start an scene instance with PrimaryActor bound to the primary role and the first component valid for each of the other roles */
	bool TryStartScene(const UContextualAnimSceneAsset* SceneAsset, AActor* PrimaryActor, const TSet<UContextualAnimSceneActorComponent*>& SceneActorComps);

	UFUNCTION(BlueprintCallable, Category = "Contextual Anim|Manager")
	UContextualAnimSceneActorComponent* FindClosestSceneActorCompToActor(const AActor* Actor) const;
	
	UFUNCTION(BlueprintCallable, Category = "Contextual Anim|Manager")
	bool TryStopSceneWithActor(AActor* Actor);

	UFUNCTION(BlueprintCallable, Category = "Contextual Anim|Manager")
	bool IsActorInAnyScene(AActor* Actor) const;

	UFUNCTION(BlueprintCallable, Category = "Contextual Anim|Manager")
	UContextualAnimSceneInstance* GetSceneWithActor(AActor* Actor);

	UFUNCTION(BlueprintPure, Category = "Contextual Anim|Manager", meta = (WorldContext = "WorldContextObject"))
	static UContextualAnimManager* GetContextualAnimManager(UObject* WorldContextObject);

	FORCEINLINE const TSet<UContextualAnimSceneActorComponent*>& GetSceneActorCompContainer() const { return SceneActorCompContainer; };

	UFUNCTION(BlueprintCallable, Category = "Contextual Anim|Manager", meta = (DisplayName = "TryStartSceneWithBindings"))
	bool BP_TryStartSceneWithBindings(const UContextualAnimSceneAsset* SceneAsset, const FContextualAnimSceneBindings& Bindings) { return TryStartScene(SceneAsset, Bindings); }

protected:

	/** Container for all SceneActorComps in the world */
	UPROPERTY()
	TSet<UContextualAnimSceneActorComponent*> SceneActorCompContainer;

	UPROPERTY()
	TArray<UContextualAnimSceneInstance*> Instances;

	void OnSceneInstanceEnded(UContextualAnimSceneInstance* SceneInstance);
};