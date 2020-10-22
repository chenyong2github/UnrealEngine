// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Library/DMXEntityReference.h"

#include "DMXComponent.generated.h"

class FBufferUpdatedReceiver;
class UDMXLibrary;
class UDMXEntityFixturePatch;

UCLASS( ClassGroup=(DMX), meta=(BlueprintSpawnableComponent), HideCategories = ("Variable", "Sockets", "Tags", "Activation", "Cooking", "ComponentReplication", "AssetUserData", "Collision", "Events"))
class DMXRUNTIME_API UDMXComponent
	: public UActorComponent
{
	GENERATED_BODY()

	friend FBufferUpdatedReceiver;

protected:
	//~ Begin UObject Interface
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
	//~ End UObject Interface

	DECLARE_DYNAMIC_MULTICAST_SPARSE_DELEGATE_TwoParams(FDMXComponentFixturePatchReceivedSignature, UDMXComponent, OnFixturePatchReceived, UDMXEntityFixturePatch*, FixturePatch, const TArray<uint8>&, ChannelsArray);
public:
	UPROPERTY(EditAnywhere, Category = "DMX")
	FDMXEntityFixturePatchRef FixturePatchRef;

public:
	UFUNCTION(BlueprintPure, Category = "DMX")
	UDMXEntityFixturePatch* GetFixturePatch() const;

	UFUNCTION(BlueprintCallable, Category = "DMX")
	void SetFixturePatch(UDMXEntityFixturePatch* InFixturePatch);

	/** Called when the component has been activated, with parameter indicating if it was from a reset */
	UPROPERTY(BlueprintAssignable, Category = "Components|DMX")
	FDMXComponentFixturePatchReceivedSignature OnFixturePatchReceived;

public:
	UDMXComponent();

	void RestartPacketReceiver();

protected:
	// ~Begin UActorComponentInterface	
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	// ~End UActorComponentInterface

private:
	TArray<uint8> ChannelBuffer;

	TAtomic<bool> bBufferUpdated;

	TSharedPtr<FBufferUpdatedReceiver> BufferUpdatedReceiver;
};
