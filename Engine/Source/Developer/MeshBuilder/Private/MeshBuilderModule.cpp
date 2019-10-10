// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "IMeshBuilderModule.h"
#include "Modules/ModuleManager.h"

#include "StaticMeshBuilder.h"
#include "Engine/StaticMesh.h"
#include "SkeletalMeshBuilder.h"
#include "Engine/SkeletalMesh.h"

class FMeshBuilderModule : public IMeshBuilderModule
{
public:

	FMeshBuilderModule()
	{
	}

	virtual void StartupModule() override
	{
		// Register any modular features here
	}

	virtual void ShutdownModule() override
	{
		// Unregister any modular features here
	}

	virtual bool BuildMesh(FStaticMeshRenderData& OutRenderData, UObject* Mesh, const FStaticMeshLODGroup& LODGroup) override;

	virtual bool BuildSkeletalMesh(USkeletalMesh* SkeletalMesh, const int32 LODIndex, const bool bRegenDepLODs) override;

private:

};

IMPLEMENT_MODULE(FMeshBuilderModule, MeshBuilder );

bool FMeshBuilderModule::BuildMesh(FStaticMeshRenderData& OutRenderData, class UObject* Mesh, const FStaticMeshLODGroup& LODGroup)
{
	UStaticMesh* StaticMesh = Cast<UStaticMesh>(Mesh);
	if (StaticMesh != nullptr)
	{
		//Call the static mesh builder
		return FStaticMeshBuilder().Build(OutRenderData, StaticMesh, LODGroup);
	}
	return false;
}

bool FMeshBuilderModule::BuildSkeletalMesh(USkeletalMesh* SkeletalMesh, const int32 LODIndex, const bool bRegenDepLODs)
{
	//Call the skeletal mesh builder
	return FSkeletalMeshBuilder().Build(SkeletalMesh, LODIndex, bRegenDepLODs);
}