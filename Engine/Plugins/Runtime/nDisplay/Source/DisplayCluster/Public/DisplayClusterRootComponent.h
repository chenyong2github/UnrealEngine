// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/SceneComponent.h"
#include "Camera/PlayerCameraManager.h"

#include "DisplayClusterEnums.h"

#include "DisplayClusterRootComponent.generated.h"


class UCameraComponent;
class UDisplayClusterCameraComponent;
class UDisplayClusterSceneComponent;
class UDisplayClusterScreenComponent;
class UMaterial;


/**
 * DisplayCluster root component (nDisplay root)
 */
UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
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
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

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

public:
	bool GetShowProjectionScreens() const
	{ return bShowProjectionScreens; }

	UMaterial* GetProjectionScreenMaterial() const
	{ return ProjectionScreensMaterial; }

protected:
	UPROPERTY(EditAnywhere, Category = "DisplayCluster", meta = (DisplayName = "Exit when ESC pressed"))
	bool bExitOnEsc;

	UPROPERTY(EditAnywhere, Category = "DisplayCluster", meta = (DisplayName = "Show projection screens"))
	bool bShowProjectionScreens;

	UPROPERTY(EditAnywhere, Category = "DisplayCluster", meta = (DisplayName = "Projection screens material"))
	UMaterial* ProjectionScreensMaterial;

#if WITH_EDITORONLY_DATA
public:
	FString GetEditorConfigPath() const
	{ return EditorConfigPath; }

	FString GetEditorNodeId() const
	{ return EditorNodeId; }

protected:
	UPROPERTY(EditAnywhere, Category = "nDisplay (Editor only)", meta = (DisplayName = "Config file"))
	FString EditorConfigPath;

	UPROPERTY(EditAnywhere, Category = "nDisplay (Editor only)", meta = (DisplayName = "Node ID"))
	FString EditorNodeId;
#endif

protected:
	// Creates all hierarchy objects declared in a config file
	virtual bool InitializeHierarchy();
	virtual bool InitializeExtra();
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

	// Current operation mode
	EDisplayClusterOperationMode OperationMode = EDisplayClusterOperationMode::Disabled;

	int32 NativeInputSyncPolicy = 0;
};
