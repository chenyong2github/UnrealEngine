// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IOutputRemap.h"

class FOutputRemapMesh;

class FOutputRemapModule : public IOutputRemap
{
public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IModuleInterface
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// OutputRemap
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool Load(const FString& RemapMeshFile, uint32& OutMeshRef) override;
	virtual void ReloadAll() override;
	virtual bool ApplyOutputRemap_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* ShaderResourceTexture, FRHITexture2D* TargetableTexture, uint32 MeshRef) override;

private:
	bool IsLoaded(const FString& RemapMeshFile, uint32& OutMeshRef);
	void ReleaseData();

private:
	FCriticalSection DataGuard;
	TArray<TSharedPtr<FOutputRemapMesh>> MeshAssets;
};
