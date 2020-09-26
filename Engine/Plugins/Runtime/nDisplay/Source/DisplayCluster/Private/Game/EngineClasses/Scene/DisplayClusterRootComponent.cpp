// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DisplayClusterRootComponent.h"

#include "Config/IPDisplayClusterConfigManager.h"
#include "DisplayClusterConfigurationTypes.h"

#include "Components/DisplayClusterCameraComponent.h"
#include "Components/DisplayClusterMeshComponent.h"
#include "Components/DisplayClusterScreenComponent.h"
#include "Components/DisplayClusterXformComponent.h"

#include "Misc/DisplayClusterGlobals.h"
#include "Misc/DisplayClusterHelpers.h"
#include "Misc/DisplayClusterLog.h"
#include "Misc/DisplayClusterStrings.h"


UDisplayClusterRootComponent::UDisplayClusterRootComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// This component settings
	PrimaryComponentTick.bCanEverTick = true;
}

void UDisplayClusterRootComponent::BeginPlay()
{
	Super::BeginPlay();

	// Store current operation mode on BeginPlay (not in the constructor)
	const EDisplayClusterOperationMode OperationMode = GDisplayCluster->GetOperationMode();

	if (OperationMode == EDisplayClusterOperationMode::Cluster ||
		OperationMode == EDisplayClusterOperationMode::Editor)
	{
		// Build nDisplay hierarchy
		InitializeHierarchy();
	}
}

void UDisplayClusterRootComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	{
		FScopeLock Lock(&InternalsSyncScope);

		// Clean containers. We store only pointers so there is no need to do any additional
		// operations. All components will be destroyed by the engine.
		XformComponents.Reset();
		CameraComponents.Reset();
		ScreenComponents.Reset();
		MeshComponents.Reset();
		AllComponents.Reset();
	}

	Super::EndPlay(EndPlayReason);
}

bool UDisplayClusterRootComponent::InitializeHierarchy()
{
	const UDisplayClusterConfigurationData* ConfigData = GDisplayCluster->GetPrivateConfigMgr()->GetConfig();
	if (!ConfigData)
	{
		return false;
	}

	// Spawn all components
	SpawnComponents<UDisplayClusterXformComponent,  UDisplayClusterConfigurationSceneComponentXform> (ConfigData->Scene->Xforms,  XformComponents,  AllComponents);
	SpawnComponents<UDisplayClusterCameraComponent, UDisplayClusterConfigurationSceneComponentCamera>(ConfigData->Scene->Cameras, CameraComponents, AllComponents);
	SpawnComponents<UDisplayClusterScreenComponent, UDisplayClusterConfigurationSceneComponentScreen>(ConfigData->Scene->Screens, ScreenComponents, AllComponents);
	SpawnComponents<UDisplayClusterMeshComponent,   UDisplayClusterConfigurationSceneComponentMesh>  (ConfigData->Scene->Meshes,  MeshComponents,   AllComponents);

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

	// If no default camera set, try to set the first one
	if (DefaultCameraComponent == nullptr)
	{
		if (CameraComponents.Num() > 0)
		{
			// There is no guarantee that default camera is the first one listed in a config file
			SetDefaultCamera(CameraComponents.CreateConstIterator()->Key);
		}
		else
		{
			UE_LOG(LogDisplayClusterGame, Error, TEXT("No cameras found"));
			return false;
		}
	}

	return true;
}

int32 UDisplayClusterRootComponent::GetScreensAmount() const
{
	FScopeLock Lock(&InternalsSyncScope);
	return ScreenComponents.Num();
}

UDisplayClusterScreenComponent* UDisplayClusterRootComponent::GetScreenById(const FString& ScreenId) const
{
	FScopeLock Lock(&InternalsSyncScope);
	if (ScreenComponents.Contains(ScreenId))
	{
		return ScreenComponents[ScreenId];
	}

	return nullptr;
}

void UDisplayClusterRootComponent::GetAllScreens(TMap<FString, UDisplayClusterScreenComponent*>& OutScreens) const
{
	FScopeLock Lock(&InternalsSyncScope);
	OutScreens = ScreenComponents;
}

int32 UDisplayClusterRootComponent::GetCamerasAmount() const
{
	FScopeLock Lock(&InternalsSyncScope);
	return CameraComponents.Num();
}

UDisplayClusterCameraComponent* UDisplayClusterRootComponent::GetCameraById(const FString& CameraId) const
{
	FScopeLock Lock(&InternalsSyncScope);
	if (CameraComponents.Contains(CameraId))
	{
		return CameraComponents[CameraId];
	}

	return nullptr;
}

void UDisplayClusterRootComponent::GetAllCameras(TMap<FString, UDisplayClusterCameraComponent*>& OutCameras) const
{
	FScopeLock Lock(&InternalsSyncScope);
	OutCameras = CameraComponents;
}

UDisplayClusterCameraComponent* UDisplayClusterRootComponent::GetDefaultCamera() const
{
	FScopeLock Lock(&InternalsSyncScope);
	return DefaultCameraComponent;
}

void UDisplayClusterRootComponent::SetDefaultCamera(const FString& CameraId)
{
	FScopeLock Lock(&InternalsSyncScope);

	UDisplayClusterCameraComponent* NewDefaultCamera = GetCameraById(CameraId);
	if (NewDefaultCamera)
	{
		DefaultCameraComponent = NewDefaultCamera;
	}
}

int32 UDisplayClusterRootComponent::GetMeshesAmount() const
{
	FScopeLock Lock(&InternalsSyncScope);
	return MeshComponents.Num();
}

UDisplayClusterMeshComponent* UDisplayClusterRootComponent::GetMeshById(const FString& MeshId) const
{
	FScopeLock Lock(&InternalsSyncScope);
	if (MeshComponents.Contains(MeshId))
	{
		return MeshComponents[MeshId];
	}

	return nullptr;
}

void UDisplayClusterRootComponent::GetAllMeshes(TMap<FString, UDisplayClusterMeshComponent*>& OutMeshes) const
{
	FScopeLock Lock(&InternalsSyncScope);
	OutMeshes = MeshComponents;
}

int32 UDisplayClusterRootComponent::GetXformsAmount() const
{
	FScopeLock Lock(&InternalsSyncScope);
	return XformComponents.Num();
}

UDisplayClusterXformComponent* UDisplayClusterRootComponent::GetXformById(const FString& XformId) const
{
	FScopeLock Lock(&InternalsSyncScope);
	if (XformComponents.Contains(XformId))
	{
		return XformComponents[XformId];
	}

	return nullptr;
}

void UDisplayClusterRootComponent::GetAllXforms(TMap<FString, UDisplayClusterXformComponent*>& OutXforms) const
{
	FScopeLock Lock(&InternalsSyncScope);
	OutXforms = XformComponents;
}

int32 UDisplayClusterRootComponent::GetComponentsAmount() const
{
	FScopeLock Lock(&InternalsSyncScope);
	return AllComponents.Num();
}

UDisplayClusterSceneComponent* UDisplayClusterRootComponent::GetComponentById(const FString& ComponentId) const
{
	FScopeLock Lock(&InternalsSyncScope);
	if (AllComponents.Contains(ComponentId))
	{
		return AllComponents[ComponentId];
	}

	return nullptr;
}

void UDisplayClusterRootComponent::GetAllComponents(TMap<FString, UDisplayClusterSceneComponent*>& OutComponents) const
{
	FScopeLock Lock(&InternalsSyncScope);
	OutComponents = AllComponents;
}

template <typename TComp, typename TCfgData>
void UDisplayClusterRootComponent::SpawnComponents(const TMap<FString, TCfgData*>& InConfigData, TMap<FString, TComp*>& OutTypedMap, TMap<FString, UDisplayClusterSceneComponent*>& OutAllMap)
{
	for (const auto& it : InConfigData)
	{
		if (!OutAllMap.Contains(it.Key))
		{
			TComp* NewComponent = NewObject<TComp>(this, FName(*it.Key));
			if (NewComponent)
			{
				NewComponent->SetId(it.Key);
				NewComponent->SetConfigParameters(it.Value);
				NewComponent->AttachToComponent(this, FAttachmentTransformRules(EAttachmentRule::KeepRelative, false));
				NewComponent->RegisterComponent();

				// Save references
				OutAllMap.Emplace(it.Key, NewComponent);
				OutTypedMap.Emplace(it.Key, NewComponent);
			}
		}
	}
}
