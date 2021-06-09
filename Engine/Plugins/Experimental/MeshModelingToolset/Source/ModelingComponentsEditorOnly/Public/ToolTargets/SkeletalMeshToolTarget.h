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

#include "SkeletalMeshToolTarget.generated.h"


class USkeletalMesh;

/**
 * A tool target backed by a skeletal mesh.
 */
UCLASS(Transient)
class MODELINGCOMPONENTSEDITORONLY_API USkeletalMeshToolTarget :
	public UToolTarget,
	public IMeshDescriptionCommitter,
	public IMeshDescriptionProvider,
	public IDynamicMeshProvider, 
	public IDynamicMeshCommitter,
	public IMaterialProvider,
	public ISkeletalMeshBackedTarget
{
	GENERATED_BODY()

public:
	// UToolTarget
	virtual bool IsValid() const override;

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
	virtual TSharedPtr<UE::Geometry::FDynamicMesh3, ESPMode::ThreadSafe> GetDynamicMesh() override;

	// IDynamicMeshCommitter
	virtual void CommitDynamicMesh(const UE::Geometry::FDynamicMesh3& Mesh, 
		const FDynamicMeshCommitInfo& CommitInfo) override;
	using IDynamicMeshCommitter::CommitDynamicMesh; // unhide the other overload

	// ISkeletalMeshBackedTarget implementation
	USkeletalMesh* GetSkeletalMesh() const override;

protected:
	USkeletalMesh* SkeletalMesh = nullptr;

	// So that the tool target factory can poke into Component.
	friend class USkeletalMeshToolTargetFactory;

	
	friend class USkeletalMeshComponentToolTarget;

	static void GetMeshDescription(const USkeletalMesh* SkeletalMesh, FMeshDescription& MeshDescriptionOut);
	static void CommitMeshDescription(USkeletalMesh* SkeletalMesh, 
		FMeshDescription* MeshDescription, const FCommitter& Committer);
	static void GetMaterialSet(const USkeletalMesh* SkeletalMesh, FComponentMaterialSet& MaterialSetOut,
		bool bPreferAssetMaterials);
	static bool CommitMaterialSetUpdate(USkeletalMesh* SkeletalMesh,
		const FComponentMaterialSet& MaterialSet, bool bApplyToAsset);
	
private:
	// Until USkeletalMesh stores its internal representation as FMeshDescription, we need to
	// retain the storage here to cover the lifetime of the pointer returned by GetMeshDescription(). 
	TUniquePtr<FMeshDescription> CachedMeshDescription;	
};

/** Factory for USkeletalMeshToolTarget to be used by the target manager. */
UCLASS(Transient)
class MODELINGCOMPONENTSEDITORONLY_API USkeletalMeshToolTargetFactory : public UToolTargetFactory
{
	GENERATED_BODY()

public:

	bool CanBuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& TargetTypeInfo) const override;

	UToolTarget* BuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& TargetTypeInfo) override;
};
