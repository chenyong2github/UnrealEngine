// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"

#include "DisplayClusterConfigurationTypes.h"

#include "DisplayClusterSceneComponent.generated.h"


class UDisplayClusterSceneComponentSync;
class UDisplayClusterConfigurationSceneComponent;


/**
 * Extended scene component
 */
UCLASS(ClassGroup = (Custom), Blueprintable, meta = (BlueprintSpawnableComponent))
class DISPLAYCLUSTER_API UDisplayClusterSceneComponent
	: public USceneComponent
{
	GENERATED_BODY()

	friend class UDisplayClusterRootComponent;

public:
	UDisplayClusterSceneComponent(const FObjectInitializer& ObjectInitializer);

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

public:
	FString GetTrackerId() const
	{
		return TrackerId;
	}

	void SetTrackerId(const FString& InTrackerId)
	{
		TrackerId = InTrackerId;
	}

	int GetTrackerChannel() const
	{
		return TrackerChannel;
	}

	void SetTrackerChannel(int InTrackerChannel)
	{
		TrackerChannel = InTrackerChannel;
	}

protected:
	void SetId(const FString& ComponentId)
	{
		CompId = ComponentId;
	}

	FString GetId() const
	{
		return CompId;
	}

	void SetConfigParameters(const UDisplayClusterConfigurationSceneComponent* InConfigData)
	{
		ConfigData = InConfigData;
	}

	const UDisplayClusterConfigurationSceneComponent* GetConfigParameters() const
	{
		return ConfigData;
	}

protected:
	UPROPERTY(BlueprintReadOnly, Category = "DisplayCluster")
	FString CompId;

	UPROPERTY(BlueprintReadOnly, Category = "DisplayCluster")
	FString SyncId;

	UPROPERTY(EditAnywhere, Category = "DisplayCluster")
	FString TrackerId;
	
	UPROPERTY(EditAnywhere, Category = "DisplayCluster")
	int TrackerChannel;

protected:
	TMap<FString, FString> ConfigParameters;
	const UDisplayClusterConfigurationSceneComponent* ConfigData;
};
