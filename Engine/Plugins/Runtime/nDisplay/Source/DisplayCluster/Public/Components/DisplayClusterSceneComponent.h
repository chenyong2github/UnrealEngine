// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/SceneComponent.h"
#include "CoreMinimal.h"

#include "DisplayClusterConfigurationTypes.h"

#if WITH_EDITOR
#include "Interfaces/Views/Viewport/IDisplayClusterConfiguratorViewportItem.h"
#endif

#include "DisplayClusterSceneComponent.generated.h"

class UDisplayClusterSceneComponentSync;
class UDisplayClusterConfigurationSceneComponent;


#if WITH_EDITOR
typedef IDisplayClusterConfiguratorViewportItem IDisplayClusterEditorFeatureInterface;
#else
typedef struct {} IDisplayClusterEditorFeatureInterface;
#endif


/**
 * nDisplay scene component
 */
UCLASS(Abstract, ClassGroup = (DisplayCluster), HideCategories=(Sockets, Rendering, Tags, ComponentTick, ComponentReplication, Activation, Cooking, Events, Physics, LOD, AssetUserData, Collision))
class DISPLAYCLUSTER_API UDisplayClusterSceneComponent
	: public USceneComponent
	, public IDisplayClusterEditorFeatureInterface
{
	GENERATED_BODY()

public:
	friend class ADisplayClusterRootActor;

public:
	UDisplayClusterSceneComponent(const FObjectInitializer& ObjectInitializer);

public:
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

	/** Checks if the component is owned by a Display Cluster Blueprint. */
	bool DoesComponentBelongToBlueprint() const;
	
protected:
	void SetConfigParameters(UDisplayClusterConfigurationSceneComponent* InConfigData)
	{
		ConfigData = InConfigData;
	}

	const UDisplayClusterConfigurationSceneComponent* GetConfigParameters() const
	{
		return ConfigData;
	}

	virtual void ApplyConfigurationData();

protected:
	UPROPERTY(BlueprintReadOnly, Category = "DisplayCluster")
	FString SyncId;

	/*
	 * TODO: Delete TrackerId
	 */
	UPROPERTY()
	FString TrackerId;

	/*
	 * TODO: Delete TrackerChannel
	 */
	UPROPERTY()
	int32 TrackerChannel;

protected:
	UDisplayClusterConfigurationSceneComponent* ConfigData;

#if WITH_EDITOR 
public:
	virtual UObject* GetObject() const override;
	virtual bool IsSelected() override;

	virtual void OnSelection() override
	{ }

	virtual void SetNodeSelection(bool bSelect)
	{ }

#endif
};
