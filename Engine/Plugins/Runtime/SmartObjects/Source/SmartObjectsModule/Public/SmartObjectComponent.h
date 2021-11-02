// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/SceneComponent.h"
#include "SmartObjectTypes.h"
#include "SmartObjectConfig.h"
#include "SmartObjectComponent.generated.h"

UCLASS(Blueprintable, ClassGroup = Gameplay, meta = (BlueprintSpawnableComponent), config = Game, HideCategories = (Activation, AssetUserData, Collision, Cooking, HLOD, Lighting, LOD, Mobile, Mobility, Navigation, Physics, RayTracing, Rendering, Tags, TextureStreaming))
class SMARTOBJECTSMODULE_API USmartObjectComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	explicit USmartObjectComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	FBox GetSmartObjectBounds() const;

	const FSmartObjectConfig& GetConfig() const { return Config; }

	FSmartObjectID GetRegisteredID() const { return RegisteredID; }
	void SetRegisteredID(const FSmartObjectID Value) { RegisteredID = Value; }

	/** Returns whether or not this component is used as a config template in the collection. */
	bool IsTemplateFromCollection() const;

	/** Adds and returns a reference to a defaulted slot (used for testing purposes) */
	void DebugSetConfig(const FSmartObjectConfig& NewConfig) { Config = NewConfig; }

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

	UPROPERTY(EditDefaultsOnly, Category = SmartObject, meta = (ShowOnlyInnerProperties))
	FSmartObjectConfig Config;

	/** RegisteredID != FSmartObject::InvalidID when registered into a collection by SmartObjectSubsystem */
	UPROPERTY(Transient, VisibleAnywhere, Category = SmartObject)
	FSmartObjectID RegisteredID;
};
