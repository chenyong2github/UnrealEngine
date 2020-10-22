// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

class ADisplayClusterRootActor;
class UDisplayClusterCameraComponent;
class UDisplayClusterMeshComponent;
class UDisplayClusterRootComponent;
class UDisplayClusterSceneComponent;
class UDisplayClusterScreenComponent;
class UWorld;


/**
 * Public game manager interface
 */
class IDisplayClusterGameManager
{
public:
	virtual ~IDisplayClusterGameManager() = 0
	{ }

public:
	/**
	* @return - Current root actor
	*/
	virtual ADisplayClusterRootActor* GetRootActor() const = 0;

	/**
	* @return - Current world
	*/
	virtual UWorld* GetWorld() const = 0;

public:
	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// DEPRECATED
	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	UE_DEPRECATED(4.26, "UDisplayClusterRootComponent is deprecated. Please use UDisplayClusterRootActor.")
	virtual UDisplayClusterRootComponent* GetRootComponent() const
	{
		return nullptr;
	}

	UE_DEPRECATED(4.26, "This feature has been moved to UDisplayClusterRootActor.")
	virtual TArray<UDisplayClusterScreenComponent*> GetAllScreens() const
	{
		return TArray<UDisplayClusterScreenComponent*>();
	}

	UE_DEPRECATED(4.26, "This feature has been moved to UDisplayClusterRootActor.")
	virtual UDisplayClusterScreenComponent* GetScreenById(const FString& ScreenId) const
	{
		return nullptr;
	}

	UE_DEPRECATED(4.26, "This feature has been moved to UDisplayClusterRootActor.")
	virtual int32 GetScreensAmount() const
	{
		return 0;
	}

	UE_DEPRECATED(4.26, "This feature has been moved to UDisplayClusterRootActor.")
	virtual TArray<UDisplayClusterCameraComponent*> GetAllCameras() const
	{
		return TArray<UDisplayClusterCameraComponent*>();
	}

	UE_DEPRECATED(4.26, "This feature has been moved to UDisplayClusterRootActor.")
	virtual UDisplayClusterCameraComponent* GetCameraById(const FString& CameraId) const
	{
		return nullptr;
	}

	UE_DEPRECATED(4.26, "This feature has been moved to UDisplayClusterRootActor.")
	virtual int32 GetCamerasAmount() const
	{
		return 0;
	}

	UE_DEPRECATED(4.26, "This feature has been moved to UDisplayClusterRootActor.")
	virtual UDisplayClusterCameraComponent* GetDefaultCamera() const
	{
		return nullptr;
	}

	UE_DEPRECATED(4.26, "This feature has been moved to UDisplayClusterRootActor.")
	virtual void SetDefaultCamera(const FString& CameraId)
	{ }

	UE_DEPRECATED(4.26, "This feature has been moved to UDisplayClusterRootActor.")
	virtual TArray<UDisplayClusterSceneComponent*>  GetAllNodes() const
	{
		return TArray<UDisplayClusterSceneComponent*>();
	}

	UE_DEPRECATED(4.26, "This feature has been moved to UDisplayClusterRootActor.")
	virtual UDisplayClusterSceneComponent* GetNodeById(const FString& SceneNodeId) const
	{
		return nullptr;
	}
};
