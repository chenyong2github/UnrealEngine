// Copyright Epic Games, Inc. All Rights Reserved.

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

	virtual bool BuildMesh(
		UObject* StaticMesh,
		TArray< FStaticMeshBuildVertex >& Verts,
		TArray< uint32 >& Indexes,
		FStaticMeshSectionArray& Sections,
		bool bBuildOnlyPosition,
		uint32& NumTexCoords,
		bool& bHasColors ) override;

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

bool FMeshBuilderModule::BuildMesh(
	UObject* Mesh,
	TArray< FStaticMeshBuildVertex >& Verts,
	TArray< uint32 >& Indexes,
	FStaticMeshSectionArray& Sections,
	bool bBuildOnlyPosition,
	uint32& NumTexCoords,
	bool& bHasColors )
{
	UStaticMesh* StaticMesh = Cast< UStaticMesh >( Mesh );
	if( StaticMesh )
	{
		//Call the static mesh builder
		return FStaticMeshBuilder().Build( StaticMesh, Verts, Indexes, Sections, bBuildOnlyPosition, NumTexCoords, bHasColors );
	}
	return false;
}

bool FMeshBuilderModule::BuildSkeletalMesh(USkeletalMesh* SkeletalMesh, const int32 LODIndex, const bool bRegenDepLODs)
{
	//Call the skeletal mesh builder
	return FSkeletalMeshBuilder().Build(SkeletalMesh, LODIndex, bRegenDepLODs);
}