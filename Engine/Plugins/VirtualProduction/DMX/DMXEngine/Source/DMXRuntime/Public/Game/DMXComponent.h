// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"

#include "DMXTypes.h"
#include "Library/DMXEntityReference.h"

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"

#include "DMXComponent.generated.h"

struct FDMXAttributeName;
class UDMXLibrary;
class UDMXEntityFixturePatch;

/** 
 * Component that receives DMX input each Tick from a fixture patch.  
 * NOTE: Does not support receive in Editor! Use the 'Get DMX Fixture Patch' and bind 'On Fixture Patch Received DMX' instead (requires the patch to be set to 'Receive DMX in Editor' in the library). 
 */
UCLASS( ClassGroup=(DMX), meta=(BlueprintSpawnableComponent), HideCategories = ("Variable", "Sockets", "Tags", "Activation", "Cooking", "ComponentReplication", "AssetUserData", "Collision", "Events"))
class DMXRUNTIME_API UDMXComponent
	: public UActorComponent
{
	GENERATED_BODY()

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDMXComponentFixturePatchReceivedSignature, UDMXEntityFixturePatch*, FixturePatch, const FDMXNormalizedAttributeValueMap&, ValuePerAttribute);

public:
	UDMXComponent();

	/** Broadcast when the component's fixture patch received DMX */
	UPROPERTY(BlueprintAssignable, Category = "Components|DMX");
	FDMXComponentFixturePatchReceivedSignature OnFixturePatchReceived;

	/** Gets the fixture patch used in the component */
	UFUNCTION(BlueprintPure, Category = "DMX")
	UDMXEntityFixturePatch* GetFixturePatch() const;

	/** Sets the fixture patch used in the component */
	UFUNCTION(BlueprintCallable, Category = "DMX")
	void SetFixturePatch(UDMXEntityFixturePatch* InFixturePatch);

	/** Sets whether the component receives dmx from the patch. Note, this is saved with the component when called in editor. */
	UFUNCTION(BlueprintCallable, Category = "DMX")
	void SetReceiveDMXFromPatch(bool bReceive);

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "DMX")
	FDMXEntityFixturePatchRef FixturePatchRef;

protected:
	/** Called when the fixture patch received DMX */
	UFUNCTION()
	void OnFixturePatchReceivedDMX(UDMXEntityFixturePatch* FixturePatch, const FDMXNormalizedAttributeValueMap& NormalizedValuePerAttribute);

	/** Sets up binding for receiving depending on the patch's and the component's properties */
	void SetupReceiveDMXBinding();

	/** If true, the component will receive DMX from the patch */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, AdvancedDisplay, Category = "DMX")
	bool bReceiveDMXFromPatch;

	// ~Begin UActorComponent interface
	virtual void BeginPlay() override;
	virtual void DestroyComponent(bool bPromoteChildren) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
	// ~End UActorComponent interface
};
