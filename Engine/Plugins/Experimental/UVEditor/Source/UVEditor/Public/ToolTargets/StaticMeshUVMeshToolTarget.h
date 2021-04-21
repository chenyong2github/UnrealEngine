// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "GeometryBase.h"
#include "ToolTargets/ToolTarget.h"
#include "TargetInterfaces/UVUnwrapDynamicMesh.h"

#include "StaticMeshUVMeshToolTarget.generated.h"

PREDECLARE_USE_GEOMETRY_CLASS(FDynamicMesh3);
PREDECLARE_USE_GEOMETRY_CLASS(FMeshDescriptionUVsToDynamicMesh);
struct FMeshDescription;
class UStaticMesh;

/**
 * Uses the UV's inside a mesh description provided by a static mesh to create a
 * flat FDynamicMesh that can be manipulated and saved back to the static mesh.
 * 
 * Does not do any caching of any produced meshes, so GetMesh() is not a trivial call.
 */
UCLASS(Transient)
class UVEDITOR_API UStaticMeshUVMeshToolTarget : public UToolTarget,
	public IUVUnwrapDynamicMesh
{
	GENERATED_BODY()

public:

	// IUVUnwrapDynamicMesh implementation
	virtual int32 GetNumUVLayers() override;
	virtual TSharedPtr<FDynamicMesh3> GetMesh(int32 LayerIndex) override;
	virtual void SaveBackToUVs(const FDynamicMesh3* MeshToSave, int32 LayerIndex) override;

protected:
	UPROPERTY()
	UStaticMesh* OriginalAsset = nullptr;

	friend class UStaticMeshUVMeshToolTargetFactory;
};

/** Factory for UStaticMeshUVMeshToolTarget to be used by the target manager. */
UCLASS(Transient)
class UVEDITOR_API UStaticMeshUVMeshToolTargetFactory : public UToolTargetFactory
{
	GENERATED_BODY()

public:

	virtual bool CanBuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& TargetTypeInfo) const override;

	virtual UToolTarget* BuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& TargetTypeInfo) override;
};