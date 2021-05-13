// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "TargetInterfaces/DynamicMeshCommitter.h"
#include "TargetInterfaces/DynamicMeshProvider.h"
#include "TargetInterfaces/MaterialProvider.h"
#include "TargetInterfaces/MeshDescriptionCommitter.h"
#include "TargetInterfaces/MeshDescriptionProvider.h"
#include "TargetInterfaces/SkeletalMeshBackedTarget.h"
#include "ToolTargets/PrimitiveComponentToolTarget.h"

#include "SkeletalMeshComponentToolTarget.generated.h"


class USkeletalMesh;

/**
 * A tool target backed by a skeletal mesh component that can provide and take a mesh
 * description.
 */
UCLASS(Transient)
class MESHMODELINGTOOLSEDITORONLY_API USkeletalMeshComponentToolTarget :
	public UPrimitiveComponentToolTarget,
	public IMeshDescriptionCommitter,
	public IMeshDescriptionProvider,
	public IDynamicMeshProvider, 
	public IDynamicMeshCommitter,
	public IMaterialProvider,
	public ISkeletalMeshBackedTarget
{
	GENERATED_BODY()

public:
	// IMeshDescriptionProvider implementation
	FMeshDescription* GetMeshDescription() override;

	// IMeshDescritpionCommitter implementation
	void CommitMeshDescription(const FCommitter& Committer) override;
	using IMeshDescriptionCommitter::CommitMeshDescription; // unhide the other overload

	// IMaterialProvider implementation
	int32 GetNumMaterials() const override;
	UMaterialInterface* GetMaterial(int32 MaterialIndex) const override;
	void GetMaterialSet(FComponentMaterialSet& MaterialSetOut, bool bPreferAssetMaterials) const override;
	bool CommitMaterialSetUpdate(const FComponentMaterialSet& MaterialSet, bool bApplyToAsset) override;	

	// IDynamicMeshProvider
	virtual TSharedPtr<UE::Geometry::FDynamicMesh3> GetDynamicMesh() override;

	// IDynamicMeshCommitter
	virtual void CommitDynamicMesh(const UE::Geometry::FDynamicMesh3& Mesh, const FDynamicMeshCommitInfo& CommitInfo) override;
	using IDynamicMeshCommitter::CommitDynamicMesh; // unhide the other overload

	// ISkeletalMeshBackedTarget implementation
	USkeletalMesh* GetSkeletalMesh() const override;

protected:
	// So that the tool target factory can poke into Component.
	friend class USkeletalMeshComponentToolTargetFactory;

private:
	// Until USkeletalMesh stores its internal representation as FMeshDescription, we need to
	// retain the storage here to cover the lifetime of the pointer returned by GetMeshDescription(). 
	TUniquePtr<FMeshDescription> CachedMeshDescription;	
};

/** Factory for USkeletalMeshComponentToolTarget to be used by the target manager. */
UCLASS(Transient)
class MESHMODELINGTOOLSEDITORONLY_API USkeletalMeshComponentToolTargetFactory : public UToolTargetFactory
{
	GENERATED_BODY()

public:

	bool CanBuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& TargetTypeInfo) const override;

	UToolTarget* BuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& TargetTypeInfo) override;
};
