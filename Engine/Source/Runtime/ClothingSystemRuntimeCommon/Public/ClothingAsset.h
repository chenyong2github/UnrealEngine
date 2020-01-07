// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ClothingAssetBase.h"
#include "ClothConfigBase.h"
#include "ClothLODData.h"
#include "ClothConfig_Legacy.h"
#include "ClothLODData_Legacy.h"
#include "ClothingSimulationInteractor.h"

#include "ClothingAsset.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogClothingAsset, Log, All);

class UPhysicsAsset;
struct FSkelMeshSection;

namespace ClothingAssetUtils
{
	/**
	 * Helper struct to hold binding information on a clothing asset, used to 
	 * enumerate all of the bindings on a skeletal mesh with 
	 * \c GetMeshClothingAssetBindings() below.
	 */
	struct FClothingAssetMeshBinding
	{
		UClothingAssetCommon* Asset;
		int32 LODIndex;
		int32 SectionIndex;
		int32 AssetInternalLodIndex;
	};

	/**
	 * Given a skeletal mesh, find all of the currently bound clothing assets and their binding information
	 * @param InSkelMesh - The skeletal mesh to search
	 * @param OutBindings - The list of bindings to write to
	 */
	void CLOTHINGSYSTEMRUNTIMECOMMON_API 
	GetMeshClothingAssetBindings(
		USkeletalMesh* InSkelMesh, 
		TArray<FClothingAssetMeshBinding>& OutBindings);
	
	/**
	 * Similar to above, but only inspects the specified LOD.
	 */
	void CLOTHINGSYSTEMRUNTIMECOMMON_API 
	GetMeshClothingAssetBindings(
		USkeletalMesh* InSkelMesh, 
		TArray<FClothingAssetMeshBinding>& OutBindings, 
		int32 InLodIndex);

#if WITH_EDITOR
	/**
	 * Clears the clothing tracking struct of a section.
	 */
	CLOTHINGSYSTEMRUNTIMECOMMON_API void 
	ClearSectionClothingData(FSkelMeshSection& InSection);
#endif
}

/**
 * Custom data wrapper for clothing assets.
 * If writing a new clothing asset importer, creating a new derived custom data 
 * is how to store importer (and possibly simulation) data that importer will 
 * create. This needs to be set to the \c CustomData member on the asset your 
 * factory creates.
 *
 * Testing whether a UClothingAssetCommon was made from a custom plugin can be 
 * achieved with:
 * \code if(AssetPtr->CustomData->IsA(UMyCustomData::StaticClass())) \endcode
 */
UCLASS(abstract, MinimalAPI)
class UClothingAssetCustomData : public UObject
{
	GENERATED_BODY()
public:
	virtual void BindToSkeletalMesh(USkeletalMesh* InSkelMesh, int32 InMeshLodIndex, int32 InSectionIndex, int32 InAssetLodIndex)
	{}
};

/**
 * Implementation of non-solver specific, but common Engine related functionality.
 *
 * Solver specific implementations may wish to override this class to construct
 * their own default instances of child classes, such as \c ClothSimConfig and 
 * \c CustomData, as well as override the \c AddNewLod() factory to build their 
 * own implementation of \c UClothLODDataBase.
 */
UCLASS(hidecategories = Object, BlueprintType)
class CLOTHINGSYSTEMRUNTIMECOMMON_API UClothingAssetCommon : public UClothingAssetBase
{
	GENERATED_BODY()
public:

	UClothingAssetCommon(const FObjectInitializer& ObjectInitializer);

#if WITH_EDITOR

	/**
	 * Create weights for skinning the render mesh to our simulation mesh, and 
	 * weights to drive our sim mesh from the skeleton.
	 */
	virtual bool BindToSkeletalMesh(USkeletalMesh* InSkelMesh, const int32 InMeshLodIndex, const int32 InSectionIndex, const int32 InAssetLodIndex) override;

	/**
	 * Helper that invokes \c UnbindFromSkeletalMesh() for each avilable entry in 
	 * \p InSkelMesh->GetImportedModel()'s LODModel.
	 */
	virtual void UnbindFromSkeletalMesh(USkeletalMesh* InSkelMesh) override;
	virtual void UnbindFromSkeletalMesh(USkeletalMesh* InSkelMesh, const int32 InMeshLodIndex) override;

	/** 
	 * Callback envoked after weights have been edited.
	 * Calls \c PushWeightsToMesh() on each \c ClothLodData, and invalidates cached data. 
	 */
	virtual void ApplyParameterMasks();

	/**
	 *	Builds the LOD transition data.
	 *	When we transition between LODs we skin the incoming mesh to the outgoing mesh
	 *	in exactly the same way the render mesh is skinned to create a smooth swap
	 */
	void BuildLodTransitionData();

#endif // WITH_EDITOR

	/**
	 * Rebuilds the \c UsedBoneIndices array from looking up the entries in the
	 * \c UsedBoneNames array, in the \p InSkelMesh's reference skeleton.
	 */
	virtual void RefreshBoneMapping(USkeletalMesh* InSkelMesh) override;

	/** Calculates the preferred root bone for the simulation. */
	void CalculateReferenceBoneIndex();

	/** Returns \c true if \p InLodIndex is a valid LOD id (index into \c ClothLodData). */
	virtual bool IsValidLod(int32 InLodIndex) override;

	/** Returns the number of valid LOD's (length of the \c ClothLodData array). */
	virtual int32 GetNumLods() override;

#if WITH_EDITOR
	/** * Add a new LOD class instance. */
	virtual int32 AddNewLod() override;

	/**
	 * Called on the clothing asset when the base data (physical mesh etc.) has
	 * changed, so any intermediate generated data can be regenerated.
	 */
	virtual void InvalidateCachedData() override;

	/* Called after changes in any of the asset properties. */
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& ChainEvent) override;
#endif // WITH_EDITOR

	/**
	 * Builds self collision data.
	 * The default behavior is if the \c ClothSimConfig reports that self collisions
	 * are enabled, it envokes \c BuildSelfCollisionData() on each \c ClothLodData's
	 * \c ClothPhysicalMeshData member.
	 */
	virtual void BuildSelfCollisionData() override;

	/** Return a const cloth config pointer of the desired cloth config type, or nullptr if there isn't any suitable. */
	template<typename ClothConfigType, typename = typename TEnableIf<TIsDerivedFrom<ClothConfigType, UClothConfigBase>::IsDerived>::Type>
	const ClothConfigType* GetClothConfig() const
	{
		const UClothConfigBase* const* const ClothConfig = ClothConfigs.Find(ClothConfigType::StaticClass()->GetFName());
		return ClothConfig ? ExactCast<ClothConfigType>(*ClothConfig) : nullptr;
	}

	/** Return a cloth config pointer of the desired cloth config type, or nullptr if there isn't any suitable. */
	template<typename ClothConfigType, typename = typename TEnableIf<TIsDerivedFrom<ClothConfigType, UClothConfigBase>::IsDerived>::Type>
	ClothConfigType* GetClothConfig()
	{
		UClothConfigBase* const* const ClothConfig = ClothConfigs.Find(ClothConfigType::StaticClass()->GetFName());
		return ClothConfig ? ExactCast<ClothConfigType>(*ClothConfig) : nullptr;
	}

	/** Migrate deprecated objects. */
	virtual void PostLoad() override;

	/** Serialize deprecated objects. */
	virtual void Serialize(FArchive& Ar) override;

	/** Propagate the shared simulation configs between assets. Called after all cloth assets sharing the same simulation are loaded. */
	virtual void PostUpdateAllAssets() override;

	// The physics asset to extract collisions from when building a simulation.
	UPROPERTY(EditAnywhere, Category = Config)
	UPhysicsAsset* PhysicsAsset;

	// Simulation specific cloth parameters. 
	// Use GetClothConfig() to retrieve the correct parameters/config type for the desired cloth simulation system.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, EditFixedSize, Instanced, Category = Config)
	TMap<FName, UClothConfigBase*> ClothConfigs;

	// Shared by all cloth instances in a skeletal mesh
	// Only supported with Chaos Cloth for now
	// This may not be editable on unused cloth assets
	UPROPERTY()
	UClothConfigBase* ClothSharedSimConfig_DEPRECATED;

	// Parameters for how the NVcloth behaves.
	// These will have no effect on Chaos cloth
	UPROPERTY()
	UClothConfigBase* ClothSimConfig_DEPRECATED;

	// Parameters for how Chaos cloth behaves 
	// These will not affect NVcloth
	// For now, we have two configuration parameters so that we can switch between chaos and
	// non chaos at will without losing the original NVcloth data
	UPROPERTY()
	UClothConfigBase* ChaosClothSimConfig_DEPRECATED;

	// The actual asset data, listed by LOD.
	UPROPERTY()
	TArray<UClothLODDataCommon*> ClothLodData;

	// Tracks which clothing LOD each skel mesh LOD corresponds to (LodMap[SkelLod]=ClothingLod).
	UPROPERTY()
	TArray<int32> LodMap;

	// List of bones this asset uses inside its parent mesh.
	UPROPERTY()
	TArray<FName> UsedBoneNames;

	// List of the indices for the bones in UsedBoneNames, used for remapping.
	UPROPERTY()
	TArray<int32> UsedBoneIndices;

	// Bone to treat as the root of the simulation space.
	UPROPERTY()
	int32 ReferenceBoneIndex;

	// Custom data applied by the importer depending on where the asset was imported from.
	UPROPERTY()
	UClothingAssetCustomData* CustomData;

	/** 
	 * Deprecated property for transitioning the \c FClothConfig struct to the 
	 * \c UClothConfigBase array, in a new property called \c ClothConfigs.
	 */
	UPROPERTY()
	FClothConfig_Legacy ClothConfig_DEPRECATED;

	/** 
	 * Deprecated property for transitioning \c FClothLODData struct to the 
	 * \c UClothLODDataCommon class, in a new property called \c ClothLodData.
	 */
	UPROPERTY()
	TArray<FClothLODData_Legacy> LodData_DEPRECATED;

private:

	// Add or replace a new cloth config of the specified type.
	template<typename ClothConfigType, typename = typename TEnableIf<TIsDerivedFrom<ClothConfigType, UClothConfigBase>::IsDerived>::Type>
	void SetClothConfig(ClothConfigType* ClothConfig)
	{
		check(ClothConfig);
		ClothConfigs.Add(ClothConfig->GetClass()->GetFName(), static_cast<UClothConfigBase*>(ClothConfig));
	}

	// Create and add any missing cloth configs.
	// If a config from a different factory exists already, the newly
	// created config will attempt to initialize its parameters from it.
	void AddClothConfigs();

	// Propagate the shared simulation configs between assets.
	// Called after a cloth asset is created or loaded.
	void PropagateSharedConfigs();

#if WITH_EDITOR
	// Helper functions used in PostPropertyChangeCb
	void ReregisterComponentsUsingClothing();
	void ForEachInteractorUsingClothing(TFunction<void (UClothingSimulationInteractor*)> Func);
#endif // WITH_EDITOR
};
