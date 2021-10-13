// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/SceneComponent.h"
#include "SmartObjectTypes.h"
#include "SmartObjectConfig.h"
#include "SmartObjectComponent.generated.h"

// @todo SO: there is currently an issue with components relying on sparse class data. Until that we keep the config as a member.
//UCLASS(ClassGroup = Gameplay, SparseClassDataTypes = SmartObjectConfig, meta = (BlueprintSpawnableComponent), config = Game, HideCategories = (Variable, Tags, Activation, Cooking, AssetUserData, Collision, ComponentReplication, ComponentTick, Sockets, Events))
UCLASS(Blueprintable, ClassGroup = Gameplay, meta = (BlueprintSpawnableComponent), config = Game, HideCategories = (Variable, Tags, Activation, Cooking, AssetUserData, Collision, ComponentReplication, ComponentTick, Sockets, Events))
class SMARTOBJECTSMODULE_API USmartObjectComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	USmartObjectComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	//~ Begin UActorComponent Interface
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	//~ End UActorComponent Interface

	FBox GetSmartObjectBounds() const;

	// @todo SO: when sparse class data is fixed
	//const FSmartObjectConfig& GetConfig() const { return *GetSmartObjectConfig(); }
	const FSmartObjectConfig& GetConfig() const { return Config; }

	FSmartObjectID GetRegisteredID() const { return RegisteredID; }
	void SetRegisteredID(FSmartObjectID Value) { RegisteredID = Value; }

	/** Adds and returns a reference to a defaulted slot (used for testing purposes) */
	void DebugSetConfig(const FSmartObjectConfig& NewConfig) { Config = NewConfig; }

protected:

	UPROPERTY(EditDefaultsOnly, Category = SmartObject, meta = (ShowOnlyInnerProperties))
	FSmartObjectConfig Config;

	/** RegisteredID != FSmartObject::InvalidID when registered into a collection by SmartObjectSubsystem */
	UPROPERTY(Transient, VisibleAnywhere, Category = SmartObject)
	FSmartObjectID RegisteredID;
};
