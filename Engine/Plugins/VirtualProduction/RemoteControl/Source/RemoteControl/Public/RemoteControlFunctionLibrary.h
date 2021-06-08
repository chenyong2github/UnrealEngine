// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "RemoteControlActor.h"
#include "RemoteControlPreset.h"
#include "RemoteControlFunctionLibrary.generated.h"

class URemoteControlPreset;
class AActor;

USTRUCT(Blueprintable)
struct FRemoteControlOptionalExposeArgs
{
	GENERATED_BODY()
	
	/**
	 * The display name of the exposed entity in the panel.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RemoteControlPreset")
	FString DisplayName;

	/**
	 * The name of the group to expose the entity in.
	 * If it does not exist, a group with that name will be created.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RemoteControlPreset")
	FString GroupName;
};

UCLASS()
class URemoteControlFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	
	/**
	 * Expose a property in a remote control preset.
	 * @param Preset the preset to expose the property in.
	 * @param SourceObject the object that contains the property to expose.
	 * @param Property the name or path of the property to expose.
	 * @param Args optional arguments.
	 * @return Whether the operation was successful.
	 */
	UFUNCTION(BlueprintCallable, Category = "RemoteControlPreset")
	static bool ExposeProperty(URemoteControlPreset* Preset, UObject* SourceObject, const FString& Property, FRemoteControlOptionalExposeArgs Args);

	/**
	 * Expose a function in a remote control preset.
	 * @param Preset the preset to expose the property in.
	 * @param SourceObject the object that contains the property to expose.
	 * @param Function the name of the function to expose.
	 * @param Args optional arguments.
	 * @return Whether the operation was successful.
	 */
	UFUNCTION(BlueprintCallable, Category = "RemoteControlPreset")
	static bool ExposeFunction(URemoteControlPreset* Preset, UObject* SourceObject, const FString& Function, FRemoteControlOptionalExposeArgs Args);

	/**
	 * Expose an actor in a remote control preset.
	 * @param Preset the preset to expose the property in.
	 * @param Actor the actor to expose.
	 * @param Args optional arguments.
	 * @return Whether the operation was successful.
	 */
	UFUNCTION(BlueprintCallable, Category = "RemoteControlPreset")
	static bool ExposeActor(URemoteControlPreset* Preset, AActor* Actor, FRemoteControlOptionalExposeArgs Args);
};