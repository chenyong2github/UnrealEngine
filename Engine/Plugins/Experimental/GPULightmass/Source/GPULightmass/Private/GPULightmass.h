// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ScenePrivate.h"
#include "VolumetricLightmap.h"
#include "GPULightmassModule.h"
#include "GPULightmassSettings.h"
#include "Scene/Scene.h"

class FGPULightmass : public IStaticLightingSystem
{
public:
	FGPULightmass(UWorld* InWorld, FGPULightmassModule* GPULightmassModule, UGPULightmassSettings* InSettings = nullptr);
	virtual ~FGPULightmass();
	void GameThreadDestroy();

	virtual const FMeshMapBuildData* GetPrimitiveMeshMapBuildData(const UPrimitiveComponent* Component, int32 LODIndex) override;
	virtual const FLightComponentMapBuildData* GetLightComponentMapBuildData(const ULightComponent* Component) override;
	virtual const FPrecomputedVolumetricLightmap* GetPrecomputedVolumetricLightmap() override;

	UWorld* World;
	FGPULightmassModule* GPULightmassModule;
	UGPULightmassSettings* Settings;
	TUniquePtr<FGCObjectScopeGuard> SettingsGuard;

	GPULightmass::FScene Scene;

	void InstallGameThreadEventHooks();
	void RemoveGameThreadEventHooks();

	void StartRecordingVisibleTiles();
	void EndRecordingVisibleTiles();

	TSharedPtr<SNotificationItem> LightBuildNotification;
	int32 LightBuildPercentage;
	
	double StartTime = 0;

	void EditorTick();

private:
	// Game thread event hooks
	void OnPreWorldFinishDestroy(UWorld* World);

	void OnPrimitiveComponentRegistered(UPrimitiveComponent* InComponent);
	void OnPrimitiveComponentUnregistered(UPrimitiveComponent* InComponent);
	void OnLightComponentRegistered(ULightComponentBase* InComponent);
	void OnLightComponentUnregistered(ULightComponentBase* InComponent);
	void OnStationaryLightChannelReassigned(ULightComponentBase* InComponent, int32 NewShadowMapChannel);
	void OnLightmassImportanceVolumeModified();
	void OnMaterialInvalidated(FMaterialRenderProxy* Material);
};
