// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/SceneComponent.h"

#include "DisplayClusterRootComponent.generated.h"


class UCameraComponent;
class UDisplayClusterCameraComponent;
class UDisplayClusterMeshComponent;
class UDisplayClusterSceneComponent;
class UDisplayClusterScreenComponent;
class UDisplayClusterXformComponent;


/**
 * DisplayCluster root component (nDisplay root)
 */
UCLASS(ClassGroup = (Custom), Blueprintable, meta = (BlueprintSpawnableComponent))
class DISPLAYCLUSTER_API UDisplayClusterRootComponent
	: public USceneComponent
{
	GENERATED_BODY()

public:
	UDisplayClusterRootComponent(const FObjectInitializer& ObjectInitializer);

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	int32                                   GetScreensAmount() const;
	UDisplayClusterScreenComponent*         GetScreenById(const FString& ScreenId) const;
	void                                    GetAllScreens(TMap<FString, UDisplayClusterScreenComponent*>& OutScreens) const;

	int32                                   GetCamerasAmount() const;
	UDisplayClusterCameraComponent*         GetCameraById(const FString& CameraId) const;
	void                                    GetAllCameras(TMap<FString, UDisplayClusterCameraComponent*>& OutCameras) const;

	/** Returns amount of VRPN axis devices. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get Default Camera"), Category = "DisplayClusterRootComponent|Camera")
	UDisplayClusterCameraComponent*         GetDefaultCamera() const;

	void                                    SetDefaultCamera(const FString& CameraId);

	int32                                   GetMeshesAmount() const;
	UDisplayClusterMeshComponent*           GetMeshById(const FString& MeshId) const;
	void                                    GetAllMeshes(TMap<FString, UDisplayClusterMeshComponent*>& OutMeshes) const;

	int32                                   GetXformsAmount() const;
	UDisplayClusterXformComponent*          GetXformById(const FString& XformId) const;
	void                                    GetAllXforms(TMap<FString, UDisplayClusterXformComponent*>& OutXforms) const;

	int32                                   GetComponentsAmount() const;
	void                                    GetAllComponents(TMap<FString, UDisplayClusterSceneComponent*>& OutComponents) const;
	UDisplayClusterSceneComponent*          GetComponentById(const FString& ComponentId) const;


	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// DEPRECATED
	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	UE_DEPRECATED(4.26, "Please use TMap based version.")
	TArray<UDisplayClusterScreenComponent*> GetAllScreens() const
	{
		return TArray<UDisplayClusterScreenComponent*>();
	}

	UE_DEPRECATED(4.26, "Please use TMap based version.")
	TArray<UDisplayClusterCameraComponent*> GetAllCameras() const
	{
		return TArray<UDisplayClusterCameraComponent*>();
	}

	UE_DEPRECATED(4.26, "Please use GetAllComponents.")
	TArray<UDisplayClusterSceneComponent*>  GetAllNodes() const
	{
		return TArray<UDisplayClusterSceneComponent*>();
	}

	UE_DEPRECATED(4.26, "Please use GetComponentById.")
	UDisplayClusterSceneComponent* GetNodeById(const FString& id) const
	{
		return nullptr;
	}

protected:
	// All available components
	TMap<FString, UDisplayClusterSceneComponent*> AllComponents;

	// Typed components
	TMap<FString, UDisplayClusterXformComponent*>  XformComponents;
	TMap<FString, UDisplayClusterCameraComponent*> CameraComponents;
	TMap<FString, UDisplayClusterScreenComponent*> ScreenComponents;
	TMap<FString, UDisplayClusterMeshComponent*>   MeshComponents;

	// Default camera for this root
	UDisplayClusterCameraComponent* DefaultCameraComponent;

protected:
	// Creates all hierarchy objects declared in a config file
	virtual bool InitializeHierarchy();

private:
	template <typename TComp, typename TCfgData>
	void SpawnComponents(const TMap<FString, TCfgData*>& InConfigData, TMap<FString, TComp*>& OutTypedMap, TMap<FString, UDisplayClusterSceneComponent*>& OutAllMap);

private:
	mutable FCriticalSection InternalsSyncScope;
};
