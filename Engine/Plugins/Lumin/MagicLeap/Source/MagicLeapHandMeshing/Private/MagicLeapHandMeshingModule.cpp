// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MagicLeapHandMeshingModule.h"
#include "GameFramework/Actor.h"
#include "EngineUtils.h"

DEFINE_LOG_CATEGORY(LogMagicLeapHandMeshing);

void FMagicLeapHandMeshingModule::StartupModule()
{
	IMagicLeapHandMeshingModule::StartupModule();
	TickDelegate = FTickerDelegate::CreateRaw(this, &FMagicLeapHandMeshingModule::Tick);
	TickDelegateHandle = FTicker::GetCoreTicker().AddTicker(TickDelegate);
}

void FMagicLeapHandMeshingModule::ShutdownModule()
{
	FTicker::GetCoreTicker().RemoveTicker(TickDelegateHandle);
	IModuleInterface::ShutdownModule();
}

bool FMagicLeapHandMeshingModule::Tick(float DeltaTime)
{
	MeshTracker.Update();
	return true;
}

bool FMagicLeapHandMeshingModule::CreateClient()
{
	return MeshTracker.Create();
}

bool FMagicLeapHandMeshingModule::DestroyClient()
{
	return MeshTracker.Destroy();
}

bool FMagicLeapHandMeshingModule::ConnectMRMesh(UMRMeshComponent* InMRMeshPtr)
{
	return MeshTracker.ConnectMRMesh(InMRMeshPtr);
}

bool FMagicLeapHandMeshingModule::DisconnectMRMesh(class UMRMeshComponent* InMRMeshPtr)
{
	return MeshTracker.DisconnectMRMesh(InMRMeshPtr);
}

bool FMagicLeapHandMeshingModule::HasMRMesh() const
{
	return MeshTracker.HasMRMesh();
}

bool FMagicLeapHandMeshingModule::HasClient() const
{
	return MeshTracker.HasClient();
}

IMPLEMENT_MODULE(FMagicLeapHandMeshingModule, MagicLeapHandMeshing);
