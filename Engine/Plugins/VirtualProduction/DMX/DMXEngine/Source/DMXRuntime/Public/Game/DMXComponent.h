// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"

#include "DMXTypes.h"
#include "Library/DMXEntityReference.h"

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"

#include "DMXComponent.generated.h"

struct FDMXAttributeName;
class FDMXSharedListener;
class UDMXLibrary;
class UDMXEntityFixturePatch;

/** 
 * Component that receives DMX input each Tick from a fixture patch.  
 * Only useful if updates are required each tick (otherwise use DMX Fixture Patch Ref variable and acess Data on demand from there).
 */
UCLASS( ClassGroup=(DMX), meta=(BlueprintSpawnableComponent), HideCategories = ("Variable", "Sockets", "Tags", "Activation", "Cooking", "ComponentReplication", "AssetUserData", "Collision", "Events"))
class DMXRUNTIME_API UDMXComponent
	: public UActorComponent
{
	GENERATED_BODY()

	friend FDMXSharedListener;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDMXComponentFixturePatchReceivedSignature, UDMXEntityFixturePatch*, FixturePatch, const FDMXNormalizedAttributeValueMap&, ValuePerAttribute);

public:
	UDMXComponent();

protected:
	// ~Begin UActorComponent interface
	virtual void OnRegister() override;
	virtual void BeginPlay() override;
	virtual void DestroyComponent(bool bPromoteChildren) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
	// ~End UActorComponent interface

protected:
	/** Called when the fixture patch received DMX */
	UFUNCTION()
	void OnFixturePatchReceivedDMX(UDMXEntityFixturePatch* FixturePatch, const FDMXNormalizedAttributeValueMap& NormalizedValuePerAttribute);

	/** Broadcast when the component's fixture patch received DMX */
	UPROPERTY(BlueprintAssignable, Category = "Components|DMX");
	FDMXComponentFixturePatchReceivedSignature OnFixturePatchReceived;

public:
	UPROPERTY(EditAnywhere, Category = "DMX")
	FDMXEntityFixturePatchRef FixturePatchRef;

public:
	UFUNCTION(BlueprintPure, Category = "DMX")
	UDMXEntityFixturePatch* GetFixturePatch() const;

	UFUNCTION(BlueprintCallable, Category = "DMX")
	void SetFixturePatch(UDMXEntityFixturePatch* InFixturePatch);

private:
	TSharedPtr<FDMXSharedListener> SharedListener;
};
