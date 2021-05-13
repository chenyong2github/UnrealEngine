// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "ConversionUtils/VolumeToDynamicMesh.h" // FVolumeToMeshOptions
#include "TargetInterfaces/DynamicMeshCommitter.h"
#include "TargetInterfaces/DynamicMeshProvider.h"
#include "TargetInterfaces/MaterialProvider.h"
#include "ToolTargets/PrimitiveComponentToolTarget.h"

#include "VolumeDynamicMeshToolTarget.generated.h"

/**
 * A tool target backed by AVolume
 */
UCLASS(Transient)
class MESHMODELINGTOOLSEDITORONLY_API UVolumeDynamicMeshToolTarget : public UPrimitiveComponentToolTarget,
	public IDynamicMeshCommitter, public IDynamicMeshProvider, public IMaterialProvider
{
	GENERATED_BODY()

public:
	UVolumeDynamicMeshToolTarget();

	const UE::Conversion::FVolumeToMeshOptions& GetVolumeToMeshOptions() { return VolumeToMeshOptions; }

	// IDynamicMeshProvider implementation
	virtual TSharedPtr<UE::Geometry::FDynamicMesh3> GetDynamicMesh() override;

	// IDynamicMeshCommitter implementation
	virtual void CommitDynamicMesh(const UE::Geometry::FDynamicMesh3& Mesh, const FDynamicMeshCommitInfo&) override;
	using IDynamicMeshCommitter::CommitDynamicMesh; // unhide the other overload

	// IMaterialProvider implementation
	virtual int32 GetNumMaterials() const override;
	virtual UMaterialInterface* GetMaterial(int32 MaterialIndex) const override;
	// Ignores bPreferAssetMaterials
	virtual void GetMaterialSet(FComponentMaterialSet& MaterialSetOut, bool bPreferAssetMaterials) const override;

	// Doesn't actually do anything for a volume
	virtual bool CommitMaterialSetUpdate(const FComponentMaterialSet& MaterialSet, bool bApplyToAsset) override { return false; }

	// Rest provided by parent class

protected:
	TSharedPtr<FMeshDescription> ConvertedMeshDescription;
	UE::Conversion::FVolumeToMeshOptions VolumeToMeshOptions;

	friend class UVolumeDynamicMeshToolTargetFactory;
};

/** Factory for UVolumeDynamicMeshToolTarget to be used by the target manager. */
UCLASS(Transient)
class MESHMODELINGTOOLSEDITORONLY_API UVolumeDynamicMeshToolTargetFactory : public UToolTargetFactory
{
	GENERATED_BODY()

public:

	virtual bool CanBuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& TargetTypeInfo) const override;

	virtual UToolTarget* BuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& TargetTypeInfo) override;
};