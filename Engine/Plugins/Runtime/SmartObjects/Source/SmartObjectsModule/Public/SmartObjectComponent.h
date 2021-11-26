// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/SceneComponent.h"
#include "SmartObjectTypes.h"
#include "SmartObjectDefinition.h"
#include "SmartObjectComponent.generated.h"

UCLASS(Blueprintable, ClassGroup = Gameplay, meta = (BlueprintSpawnableComponent), config = Game, HideCategories = (Activation, AssetUserData, Collision, Cooking, HLOD, Lighting, LOD, Mobile, Mobility, Navigation, Physics, RayTracing, Rendering, Tags, TextureStreaming))
class SMARTOBJECTSMODULE_API USmartObjectComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	explicit USmartObjectComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	FBox GetSmartObjectBounds() const;

	const USmartObjectDefinition* GetDefinition() const { return DefinitionAsset; }
	void SetDefinition(USmartObjectDefinition* Definition) { DefinitionAsset = Definition; }

	FSmartObjectID GetRegisteredID() const { return RegisteredID; }
	void SetRegisteredID(const FSmartObjectID Value) { RegisteredID = Value; }

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

	UPROPERTY(EditAnywhere, Category = SmartObject, BlueprintReadWrite)
	TObjectPtr<USmartObjectDefinition> DefinitionAsset;

	/** RegisteredID != FSmartObject::InvalidID when registered into a collection by SmartObjectSubsystem */
	UPROPERTY(Transient, VisibleAnywhere, Category = SmartObject)
	FSmartObjectID RegisteredID;
};
