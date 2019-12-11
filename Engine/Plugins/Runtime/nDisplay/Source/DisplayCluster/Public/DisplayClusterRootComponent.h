// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/SceneComponent.h"

#include "DisplayClusterRootComponent.generated.h"


class UCameraComponent;
class UDisplayClusterCameraComponent;
class UDisplayClusterSceneComponent;
class UDisplayClusterScreenComponent;


/**
 * DisplayCluster root component (nDisplay root)
 */
UCLASS( ClassGroup=(Custom) )
class DISPLAYCLUSTER_API UDisplayClusterRootComponent
	: public USceneComponent
{
	GENERATED_BODY()

public:
	UDisplayClusterRootComponent(const FObjectInitializer& ObjectInitializer);

public:

public:
	virtual void BeginPlay() override;
	virtual void BeginDestroy() override;

public:
	TArray<UDisplayClusterScreenComponent*> GetAllScreens() const;
	UDisplayClusterScreenComponent*         GetScreenById(const FString& id) const;
	int32                                   GetScreensAmount() const;

	TArray<UDisplayClusterCameraComponent*> GetAllCameras() const;
	UDisplayClusterCameraComponent*         GetCameraById(const FString& id) const;
	int32                                   GetCamerasAmount() const;
	UDisplayClusterCameraComponent*         GetDefaultCamera() const;
	void                                    SetDefaultCamera(const FString& id);

	TArray<UDisplayClusterSceneComponent*>  GetAllNodes() const;
	UDisplayClusterSceneComponent*          GetNodeById(const FString& id) const;

protected:
	// Available screens (from config file)
	TMap<FString, UDisplayClusterScreenComponent*> ScreenComponents;
	// Available cameras (from config file)
	TMap<FString, UDisplayClusterCameraComponent*> CameraComponents;
	// All available DisplayCluster nodes in hierarchy
	TMap<FString, UDisplayClusterSceneComponent*>  SceneNodeComponents;

	// Default camera for this root
	UDisplayClusterCameraComponent* DefaultCameraComponent = nullptr;

protected:
	// Creates all hierarchy objects declared in a config file
	virtual bool InitializeHierarchy();
	virtual bool CreateScreens();
	virtual bool CreateNodes();
	virtual bool CreateCameras();

private:
	// Extracts array of values from a map
	template <typename ObjType>
	TArray<ObjType*> GetMapValues(const TMap<FString, ObjType*>& container) const;

	// Gets item by id. Performs checks and logging.
	template <typename DataType>
	DataType* GetItem(const TMap<FString, DataType*>& container, const FString& id, const FString& logHeader) const;

private:
	mutable FCriticalSection InternalsSyncScope;
};
