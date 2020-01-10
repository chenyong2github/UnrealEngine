// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterRootComponent.h"

#include "Config/DisplayClusterConfigTypes.h"
#include "Cluster/IPDisplayClusterClusterManager.h"
#include "Config/IPDisplayClusterConfigManager.h"
#include "Config/DisplayClusterConfigTypes.h"
#include "Game/IPDisplayClusterGameManager.h"

#include "Camera/CameraComponent.h"

#include "DisplayClusterCameraComponent.h"
#include "DisplayClusterSceneComponent.h"
#include "DisplayClusterScreenComponent.h"

#include "DisplayClusterGlobals.h"
#include "DisplayClusterHelpers.h"
#include "DisplayClusterLog.h"
#include "DisplayClusterStrings.h"


UDisplayClusterRootComponent::UDisplayClusterRootComponent(const FObjectInitializer& ObjectInitializer)
	: USceneComponent(ObjectInitializer)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterGame);

	// This component settings
	PrimaryComponentTick.bCanEverTick = false;
}

void UDisplayClusterRootComponent::BeginPlay()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterGame);

	// Store current operation mode on BeginPlay (not in the constructor)
	const EDisplayClusterOperationMode OperationMode = GDisplayCluster->GetOperationMode();

	if (OperationMode == EDisplayClusterOperationMode::Cluster ||
		OperationMode == EDisplayClusterOperationMode::Editor)
	{
		// Build nDisplay hierarchy
		InitializeHierarchy();
	}

	Super::BeginPlay();
}

void UDisplayClusterRootComponent::BeginDestroy()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterGame);

	{
		FScopeLock lock(&InternalsSyncScope);

		// Clean containers. We store only pointers so there is no need to do any additional
		// operations. All components will be destroyed by the engine.
		ScreenComponents.Reset();
		CameraComponents.Reset();
		SceneNodeComponents.Reset();
	}

	Super::BeginDestroy();
}

bool UDisplayClusterRootComponent::InitializeHierarchy()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterGame);

	// Now create all child components
	if (!(CreateCameras() && CreateScreens() && CreateNodes()))
	{
		UE_LOG(LogDisplayClusterGame, Error, TEXT("An error occurred during DisplayCluster root initialization"));
		return false;
	}

	// Let components initialize ourselves
	for (auto it = SceneNodeComponents.CreateIterator(); it; ++it)
	{
		if (it->Value->ApplySettings() == false)
		{
			UE_LOG(LogDisplayClusterGame, Warning, TEXT("Coulnd't initialize DisplayCluster node: ID=%s"), *it->Key);
		}
	}

	// Check if default camera was specified in command line arguments
	FString DefaultCamId;
	if (FParse::Value(FCommandLine::Get(), DisplayClusterStrings::args::Camera, DefaultCamId))
	{
		DisplayClusterHelpers::str::TrimStringValue(DefaultCamId);
		UE_LOG(LogDisplayClusterGame, Log, TEXT("Default camera from command line arguments: %s"), *DefaultCamId);
		if (CameraComponents.Contains(DefaultCamId))
		{
			SetDefaultCamera(DefaultCamId);
		}
	}

	return true;
}

bool UDisplayClusterRootComponent::CreateScreens()
{
	// Create screens
	const IPDisplayClusterConfigManager* const ConfigMgr = GDisplayCluster->GetPrivateConfigMgr();
	if (!ConfigMgr)
	{
		UE_LOG(LogDisplayClusterGame, Error, TEXT("Couldn't get config manager interface"));
		return false;
	}

	const TArray<FDisplayClusterConfigScreen> Screens = ConfigMgr->GetScreens();
	for (const FDisplayClusterConfigScreen& Screen : Screens)
	{
		// Create screen component
		UDisplayClusterScreenComponent* NewScreenComp = NewObject<UDisplayClusterScreenComponent>(GetOwner(), FName(*Screen.Id), RF_Transient);
		check(NewScreenComp);

		NewScreenComp->AttachToComponent(this, FAttachmentTransformRules(EAttachmentRule::KeepRelative, false));
		NewScreenComp->RegisterComponent();
		NewScreenComp->SetSettings(&Screen);

		// Store the screen
		ScreenComponents.Add(Screen.Id, NewScreenComp);
		SceneNodeComponents.Add(Screen.Id, NewScreenComp);
	}

	return true;
}

bool UDisplayClusterRootComponent::CreateNodes()
{
	// Create other nodes
	const TArray<FDisplayClusterConfigSceneNode> SceneNodes = GDisplayCluster->GetPrivateConfigMgr()->GetSceneNodes();
	for (const FDisplayClusterConfigSceneNode& Node : SceneNodes)
	{
		UDisplayClusterSceneComponent* NewSceneNode = NewObject<UDisplayClusterSceneComponent>(GetOwner(), FName(*Node.Id), RF_Transient);
		check(NewSceneNode);

		NewSceneNode->AttachToComponent(this, FAttachmentTransformRules(EAttachmentRule::KeepRelative, false));
		NewSceneNode->RegisterComponent();
		NewSceneNode->SetSettings(&Node);

		SceneNodeComponents.Add(Node.Id, NewSceneNode);
	}

	return true;
}

bool UDisplayClusterRootComponent::CreateCameras()
{
	// Get all cameras listed in the config file
	const TArray<FDisplayClusterConfigCamera> Cameras = GDisplayCluster->GetPrivateConfigMgr()->GetCameras();
	
	// Instantiate each camera within hierarchy of this root
	for (const FDisplayClusterConfigCamera& Camera : Cameras)
	{
		UDisplayClusterCameraComponent* NewCameraComponent = NewObject<UDisplayClusterCameraComponent>(GetOwner(), FName(*Camera.Id), RF_Transient);
		check(NewCameraComponent);

		NewCameraComponent->AttachToComponent(this, FAttachmentTransformRules(EAttachmentRule::KeepRelative, false));
		NewCameraComponent->RegisterComponent();
		NewCameraComponent->SetSettings(&Camera);

		CameraComponents.Add(Camera.Id, NewCameraComponent);
		SceneNodeComponents.Add(Camera.Id, NewCameraComponent);

		if (!DefaultCameraComponent)
		{
			DefaultCameraComponent = NewCameraComponent;
		}
	}

	// At least one camera must be set up
	if (CameraComponents.Num() == 0)
	{
		UE_LOG(LogDisplayClusterGame, Warning, TEXT("No cameras found"));
		return false;
	}

	return CameraComponents.Num() > 0;
}


TArray<UDisplayClusterScreenComponent*> UDisplayClusterRootComponent::GetAllScreens() const
{
	FScopeLock lock(&InternalsSyncScope);
	return GetMapValues<UDisplayClusterScreenComponent>(ScreenComponents);
}

UDisplayClusterScreenComponent* UDisplayClusterRootComponent::GetScreenById(const FString& id) const
{
	FScopeLock lock(&InternalsSyncScope);
	return GetItem<UDisplayClusterScreenComponent>(ScreenComponents, id, FString("GetScreenById"));
}

int32 UDisplayClusterRootComponent::GetScreensAmount() const
{
	FScopeLock lock(&InternalsSyncScope);
	return ScreenComponents.Num();
}

UDisplayClusterCameraComponent* UDisplayClusterRootComponent::GetCameraById(const FString& id) const
{
	FScopeLock lock(&InternalsSyncScope);
	return GetItem<UDisplayClusterCameraComponent>(CameraComponents, id, FString("GetCameraById"));
}

TArray<UDisplayClusterCameraComponent*> UDisplayClusterRootComponent::GetAllCameras() const
{
	FScopeLock lock(&InternalsSyncScope);
	return GetMapValues<UDisplayClusterCameraComponent>(CameraComponents);
}

int32 UDisplayClusterRootComponent::GetCamerasAmount() const
{
	FScopeLock lock(&InternalsSyncScope);
	return CameraComponents.Num();
}

UDisplayClusterCameraComponent* UDisplayClusterRootComponent::GetDefaultCamera() const
{
	FScopeLock lock(&InternalsSyncScope);
	return DefaultCameraComponent;
}

void UDisplayClusterRootComponent::SetDefaultCamera(const FString& id)
{
	FScopeLock lock(&InternalsSyncScope);

	UDisplayClusterCameraComponent* NewDefaultCamera = GetCameraById(id);
	if (NewDefaultCamera)
	{
		DefaultCameraComponent = NewDefaultCamera;
	}
}

UDisplayClusterSceneComponent* UDisplayClusterRootComponent::GetNodeById(const FString& id) const
{
	FScopeLock lock(&InternalsSyncScope);
	return GetItem<UDisplayClusterSceneComponent>(SceneNodeComponents, id, FString("GetNodeById"));
}

TArray<UDisplayClusterSceneComponent*> UDisplayClusterRootComponent::GetAllNodes() const
{
	FScopeLock lock(&InternalsSyncScope);
	return GetMapValues<UDisplayClusterSceneComponent>(SceneNodeComponents);
}

// Extracts array of values from a map
template <typename ObjType>
TArray<ObjType*> UDisplayClusterRootComponent::GetMapValues(const TMap<FString, ObjType*>& container) const
{
	TArray<ObjType*> items;
	container.GenerateValueArray(items);
	return items;
}

// Gets item by id. Performs checks and logging.
template <typename DataType>
DataType* UDisplayClusterRootComponent::GetItem(const TMap<FString, DataType*>& container, const FString& id, const FString& logHeader) const
{
	if (container.Contains(id))
	{
		return container[id];
	}

	UE_LOG(LogDisplayClusterGame, Warning, TEXT("%s: ID not found <%s>. Return nullptr."), *logHeader, *id);
	return nullptr;
}
