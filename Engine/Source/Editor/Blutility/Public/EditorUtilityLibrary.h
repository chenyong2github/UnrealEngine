// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetData.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "UObject/ScriptMacros.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "EditorUtilityLibrary.generated.h"

class AActor;
class UEditorPerProjectUserSettings;

UCLASS()
class BLUTILITY_API UEditorUtilityBlueprintAsyncActionBase : public UBlueprintAsyncActionBase
{
	GENERATED_UCLASS_BODY()

public:
	virtual void RegisterWithGameInstance(UObject* WorldContextObject) override;
	virtual void SetReadyToDestroy() override;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FAsyncDelayComplete);

UCLASS()
class BLUTILITY_API UAsyncEditorDelay : public UEditorUtilityBlueprintAsyncActionBase
{
	GENERATED_UCLASS_BODY()

public:
#if WITH_EDITOR

	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true"))
	static UAsyncEditorDelay* AsyncEditorDelay(float Seconds);

#endif

public:

	UPROPERTY(BlueprintAssignable)
	FAsyncDelayComplete Complete;

public:

	void Start(float Seconds);

private:

	bool HandleComplete(float DeltaTime);
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAsyncEditorWaitForGameWorldEvent, UWorld*, World);

UCLASS()
class BLUTILITY_API UAsyncEditorWaitForGameWorld : public UEditorUtilityBlueprintAsyncActionBase
{
	GENERATED_UCLASS_BODY()

public:
#if WITH_EDITOR

	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true"))
	static UAsyncEditorWaitForGameWorld* AsyncWaitForGameWorld(int32 Index = 0, bool Server = false);

#endif

public:

	UPROPERTY(BlueprintAssignable)
	FAsyncEditorWaitForGameWorldEvent Complete;

public:

	void Start(int32 Index, bool Server);

private:

	bool OnTick(float DeltaTime);

	int32 Index;
	bool Server;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDownloadImageDelegate, UTexture2DDynamic*, Texture);

// Expose editor utility functions to Blutilities 
UCLASS()
class BLUTILITY_API UEditorUtilityLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()

public:
#if WITH_EDITOR

	UFUNCTION(BlueprintCallable, Category = "Development|Editor")
	static TArray<AActor*> GetSelectionSet();

	UFUNCTION(BlueprintCallable, Category = "Development|Editor")
	static void GetSelectionBounds(FVector& Origin, FVector& BoxExtent, float& SphereRadius);

	// Gets the set of currently selected assets
	UFUNCTION(BlueprintCallable, Category = "Development|Editor")
	static TArray<UObject*> GetSelectedAssets();

	// Gets the set of currently selected classes
	UFUNCTION(BlueprintCallable, Category = "Development|Editor")
	static TArray<UClass*> GetSelectedBlueprintClasses();

	// Gets the set of currently selected asset data
	UFUNCTION(BlueprintCallable, Category = "Development|Editor")
	static TArray<FAssetData> GetSelectedAssetData();

	// Renames an asset (cannot move folders)
	UFUNCTION(BlueprintCallable, Category = "Development|Editor")
	static void RenameAsset(UObject* Asset, const FString& NewName);

	/**
	* Attempts to find the actor specified by PathToActor in the current editor world
	* @param	PathToActor	The path to the actor (e.g. PersistentLevel.PlayerStart)
	* @return	A reference to the actor, or none if it wasn't found
	*/
	UFUNCTION(BlueprintPure, Category = "Development|Editor")
	AActor* GetActorReference(FString PathToActor);

#endif
};
