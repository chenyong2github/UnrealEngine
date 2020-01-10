// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IMagicLeapPlugin.h"
#include "IMagicLeapHandMeshingModule.h"
#include "MagicLeapMeshTracker.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMagicLeapHandMeshing, Verbose, All);

class FMagicLeapHandMeshingModule : public IMagicLeapHandMeshingModule
{
public:
	void StartupModule() override;
	void ShutdownModule() override;
	bool Tick(float DeltaTime) override;
	bool CreateClient();
	bool DestroyClient();
	bool ConnectMRMesh(UMRMeshComponent* InMRMeshPtr);
	bool DisconnectMRMesh(class UMRMeshComponent* InMRMeshPtr);
	bool HasMRMesh() const;
	bool HasClient() const;

private:
	FTickerDelegate TickDelegate;
	FDelegateHandle TickDelegateHandle;
	FMagicLeapMeshTracker MeshTracker;
};

inline FMagicLeapHandMeshingModule& GetMagicLeapHandMeshingModule()
{
	return FModuleManager::Get().GetModuleChecked<FMagicLeapHandMeshingModule>("MagicLeapHandMeshing");
}
