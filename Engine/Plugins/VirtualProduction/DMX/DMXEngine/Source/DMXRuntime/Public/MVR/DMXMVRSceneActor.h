// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "DMXMVRSceneActor.generated.h"

class UDMXImportGDTF;
class UDMXLibrary;
class UDMXMVRSceneComponent;

class UFactory;


USTRUCT(BlueprintType)
struct FDMXMVRSceneActorGDTFToActorClassPair
{
	GENERATED_BODY()

#if WITH_EDITORONLY_DATA
	/** The GDTF Asset */
	UPROPERTY(EditAnywhere, Category = "MVR")
	TSoftObjectPtr<UDMXImportGDTF> GDTF;

	UPROPERTY(EditAnywhere, Category = "MVR", Meta = (MustImplement = "DMXMVRFixtureActorInterface"))
	TSoftClassPtr<AActor> Actor;
#endif // WITH_EDITORONLY_DATA
};


UCLASS(NotBlueprintable)
class DMXRUNTIME_API ADMXMVRSceneActor
	: public AActor
{
	GENERATED_BODY()

public:
	/** Constructor */
	ADMXMVRSceneActor();

	/** Destructor */
	~ADMXMVRSceneActor();

	//~ Begin AActor interface
	virtual void PostLoad() override;
	virtual void PostRegisterAllComponents() override;
	//~ End AActor interface

	/** Sets the dmx library for this MVR actor. Should only be called once, further calls will have no effect and hit an ensure condition */
	void SetDMXLibrary(UDMXLibrary* NewDMXLibrary);

	/** Returns the DMX Library of this MVR Scene Actor */
	FORCEINLINE UDMXLibrary* GetDMXLibrary() const { return DMXLibrary; }

#if WITH_EDITORONLY_DATA
	/** The actor class that is spawned for a specific GDTF by default (can be overriden per MVR UUID, see below) */
	UPROPERTY(EditAnywhere, Category = "MVR", Meta = (DispayName = "Default Actor used for GDTF"))
	TArray<FDMXMVRSceneActorGDTFToActorClassPair> GDTFToDefaultActorClassArray;
#endif // WITH_EDITORONLY_DATA

private:
	/** Set MVR UUIDs for related Actors */
	void SetMVRUUIDsForRelatedActors();

#if WITH_EDITOR
	/** Called when a sub-level is loaded */
	void OnMapChange(uint32 MapChangeFlags);

	/** Called when an actor got deleted in editor */
	void OnActorDeleted(AActor* ActorDeleted);

	/** Called when an asset was imported */
	void OnAssetPostImport(UFactory* InFactory, UObject* ActorAdded);
#endif // WITH_EDITOR

private:
	/** The DMX Library this Scene Actor uses */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "MVR", Meta = (AllowPrivateAccess = true))
	TObjectPtr<UDMXLibrary> DMXLibrary;

	/** The actors that created along with this scene */
	UPROPERTY(VisibleAnywhere, Category = "Actor", AdvancedDisplay, Meta = (AllowPrivateAccess = true))
	TMap<FGuid, TSoftObjectPtr<AActor>> MVRUUIDToRelatedActorMap;

	/** The root component to which all actors are attached initially */
	UPROPERTY(VisibleAnywhere, Category = "Actor", AdvancedDisplay, Meta = (AllowPrivateAccess = true))
	USceneComponent* MVRSceneRoot;
};
