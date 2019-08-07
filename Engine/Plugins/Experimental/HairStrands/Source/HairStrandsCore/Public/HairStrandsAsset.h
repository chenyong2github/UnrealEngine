// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#pragma once

#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "HairStrandsDatas.h"
#include "RenderResource.h"

#include "HairStrandsAsset.generated.h"


/**
 * An instance of a hair strands.
 */
class FHairStrandsInstance
{
public:

	/** The hair strands resource. Kept an active ref count to manage release with UHairStrands. */
	struct FHairStrandsResource* RenderResource;

	/** World Transform of the hair strands */
	FMatrix LocalToGlobal;

	/** Instance destructor */
	~FHairStrandsInstance();

	/**
	 * Initializes the instance for the given resource.
	 * @param InResource - The resource to be used by this instance.
	 * @param bInstanced - true if the resource is instanced and ownership is being transferred.
	 */
	void InitResource(struct FHairStrandsResource* InResource, bool bInstanced);

	/**
	 * Update the transforms for this hair strands instance.
	 * @param LocalToWorld - Transform from local space to world space.
	 */
	void UpdateTransforms(const FMatrix& LocalToWorld);

private:

	/** true if the resource is instanced and owned by this instance. */
	bool bInstancedResource;
};

/* Render buffers that will be used for rendering */
struct FHairStrandsResource : public FRenderResource
{
	/** Build the hair strands resource */
	FHairStrandsResource(FHairStrandsDatas* HairStrandsDatas);

	/* Init the buffer */
	virtual void InitRHI() override;

	/* Release the buffer */
	virtual void ReleaseRHI() override;

	/* Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FHairStrandsResource"); }

	/* Strand hair position buffer */
	FRWBuffer PositionBuffer;

	/* Strand hair offset buffer */
	FRWBuffer TangentBuffer;

	/* Pointer to the hair strands datas */
	FHairStrandsDatas* StrandsDatas;
};

/**
 * Implements an asset that can be used to store hair strands
 */
UCLASS(BlueprintType, hidecategories = (Object))
class HAIRSTRANDSCORE_API UHairStrandsAsset : public UObject
{
	GENERATED_BODY()

public:

	/** Holds the File Path. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "HairStrands")
	FString FilePath;

	/** Hair strands datas */
	FHairStrandsDatas StrandsDatas;

	//~ Begin UObject Interface.
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;
	virtual void Serialize(FArchive& Ar) override;

#if WITH_EDITOR

	/**  Part of Uobject interface  */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA

	/** Asset data to be used when re-importing */
	UPROPERTY(VisibleAnywhere, Instanced, Category = ImportSettings)
	class UAssetImportData* AssetImportData;

	/** Retrievde the asset tags*/
	virtual void GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const override;

	/** Part of Uobject interface */
	virtual void PostInitProperties() override;

#endif // WITH_EDITORONLY_DATA

	/** Initialize resources. */
	void InitResource();

	/** Update resources. */
	void UpdateResource();

	/** Release the hair strands resource. */
	void ReleaseResource();

	/**
	 * Initializes an instance for use with this vector field.
	 */
	void InitInstance(class FVectorFieldInstance* Instance, bool bPreviewInstance);

//private : 

	/** Render resource to be allocated*/
	FHairStrandsResource* HairStrandsResource;
};
