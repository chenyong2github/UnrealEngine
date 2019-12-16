// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

class ADisplayClusterRootActor;
class UDisplayClusterCameraComponent;
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
	virtual ADisplayClusterRootActor*               GetRootActor() const = 0;
	virtual UDisplayClusterRootComponent*           GetRootComponent() const = 0;

	virtual TArray<UDisplayClusterScreenComponent*> GetAllScreens() const = 0;
	virtual UDisplayClusterScreenComponent*         GetScreenById(const FString& id) const = 0;
	virtual int32                                   GetScreensAmount() const = 0;

	virtual TArray<UDisplayClusterCameraComponent*> GetAllCameras() const = 0;
	virtual UDisplayClusterCameraComponent*         GetCameraById(const FString& id) const = 0;
	virtual int32                                   GetCamerasAmount() const = 0;
	virtual UDisplayClusterCameraComponent*         GetDefaultCamera() const = 0;
	virtual void                                    SetDefaultCamera(const FString& id) = 0;

	virtual TArray<UDisplayClusterSceneComponent*>  GetAllNodes() const = 0;
	virtual UDisplayClusterSceneComponent*          GetNodeById(const FString& id) const = 0;

	virtual UWorld* GetWorld() const = 0;
};
