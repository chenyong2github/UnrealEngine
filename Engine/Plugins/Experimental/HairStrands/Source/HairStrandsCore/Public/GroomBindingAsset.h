// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "HairDescription.h"
#include "HairStrandsDatas.h"
#include "RenderResource.h"
#include "GroomResources.h"
#include "HairStrandsInterface.h"
#include "Engine/SkeletalMesh.h"
#include "GroomBindingAsset.generated.h"


class UAssetUserData;
class UMaterialInterface;
class UNiagaraSystem;
class UGroomAsset;
struct FHairGroupData;

USTRUCT(BlueprintType)
struct HAIRSTRANDSCORE_API FGoomBindingGroupInfo
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Info", meta = (DisplayName = "Curve Count"))
	int32 RenRootCount = 0;

	UPROPERTY(VisibleAnywhere, Category = "Info", meta = (DisplayName = "Curve LOD"))
	int32 RenLODCount = 0;

	UPROPERTY(VisibleAnywhere, Category = "Info", meta = (DisplayName = "Guide Count"))
	int32 SimRootCount = 0;

	UPROPERTY(VisibleAnywhere, Category = "Info", meta = (DisplayName = "Guide LOD"))
	int32 SimLODCount = 0;
};

/**
 * Implements an asset that can be used to store binding information between a groom and a skeletal mesh
 */
UCLASS(BlueprintType, hidecategories = (Object))
class HAIRSTRANDSCORE_API UGroomBindingAsset : public UObject
{
	GENERATED_BODY()

#if WITH_EDITOR
	/** Notification when anything changed */
	DECLARE_MULTICAST_DELEGATE(FOnGroomBindingAssetChanged);
#endif

public:

	/** Groom to bind. */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "BuildSettings", meta = (ClampMin = "0.01", UIMin = "0.01", UIMax = "1.0"))
	UGroomAsset* Groom;

	/** Skeletal mesh on which the groom has been authored. This is optional, and used only if the hair
		binding is done a different mesh than the one which it has been authored */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "BuildSettings", meta = (ClampMin = "0.01", UIMin = "0.01", UIMax = "1.0"))
	USkeletalMesh* SourceSkeletalMesh;

	/** Skeletal mesh on which the groom is attached to. */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "BuildSettings", meta = (ClampMin = "0.01", UIMin = "0.01", UIMax = "1.0"))
	USkeletalMesh* TargetSkeletalMesh;

	/** Number of points used for the rbf interpolation */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "BuildSettings", meta = (ClampMin = "0.01", UIMin = "0.01", UIMax = "1.0"))
	int32 NumInterpolationPoints = 100;

	UPROPERTY(EditAnywhere, EditFixedSize, BlueprintReadWrite, Category = "HairGroups", meta = (DisplayName = "Group"))
	TArray<FGoomBindingGroupInfo> GroupInfos;

	/** GPU and CPU binding data for both simulation and rendering. */
	struct FHairGroupResource
	{
		FHairStrandsRestRootResource* SimRootResources = nullptr;
		FHairStrandsRestRootResource* RenRootResources = nullptr;
		TArray<FHairStrandsRestRootResource*> CardsRootResources;
	};
	typedef TArray<FHairGroupResource> FHairGroupResources;
	FHairGroupResources HairGroupResources;

	/** Queue of resources which needs to be deleted. This queue is needed for keeping valid pointer on the group resources 
	   when the binding asset is recomputed */
	TQueue<FHairGroupResource> HairGroupResourcesToDelete;

	struct FHairGroupData
	{
		FHairStrandsRootData SimRootData;
		FHairStrandsRootData RenRootData;
		TArray<FHairStrandsRootData> CardsRootData;
	};
	typedef TArray<FHairGroupData> FHairGroupDatas;
	FHairGroupDatas HairGroupDatas;

	//~ Begin UObject Interface.
	virtual void PostLoad() override;
	virtual void PreSave(const class ITargetPlatform* TargetPlatform) override;
	virtual void PostSaveRoot(bool bCleanupIsRequired) override;
	virtual void BeginDestroy() override;
	virtual void Serialize(FArchive& Ar) override;

	static bool IsCompatible(const USkeletalMesh* InSkeletalMesh, const UGroomBindingAsset* InBinding, bool bIssueWarning);
	static bool IsCompatible(const UGroomAsset* InGroom, const UGroomBindingAsset* InBinding, bool bIssueWarning);
	static bool IsBindingAssetValid(const UGroomBindingAsset* InBinding, bool bIsBindingReloading, bool bIssueWarning);

#if WITH_EDITOR
	FOnGroomBindingAssetChanged& GetOnGroomBindingAssetChanged() { return OnGroomBindingAssetChanged; }

	/**  Part of UObject interface  */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	
#endif // WITH_EDITOR

	enum class EQueryStatus
	{
		None,
		Submitted,
		Completed
	};
	volatile EQueryStatus QueryStatus = EQueryStatus::None;

	/** Initialize resources. */
	void InitResource();

	/** Update resources. */
	void UpdateResource();

	/** Release the hair strands resource. */
	void ReleaseResource();

	void Reset();

	/** Return true if the binding asset is valid, i.e., correctly built and loaded. */
	bool IsValid() const { return bIsValid;  }

	//private :
#if WITH_EDITOR
	FOnGroomBindingAssetChanged OnGroomBindingAssetChanged;
#endif

#if WITH_EDITORONLY_DATA
	/** Build/rebuild a binding asset */
	void Build();

	FString BuildDerivedDataKeySuffix(USkeletalMesh* Source, USkeletalMesh* Target, UGroomAsset* Groom, uint32 NumInterpolationPoints);
	void CacheDerivedDatas();
	void InvalidateBinding();
	void InvalidateBinding(class USkeletalMesh*);
	bool bRegisterSourceMeshCallback = false;
	bool bRegisterTargetMeshCallback = false;
	bool bRegisterGroomAssetCallback = false;
	FString CachedDerivedDataKey;
#endif
	bool bIsValid = false;
};
