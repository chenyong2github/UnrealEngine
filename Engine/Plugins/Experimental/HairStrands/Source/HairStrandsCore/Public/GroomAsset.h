// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#pragma once

#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "HairDescription.h"
#include "HairStrandsDatas.h"
#include "RenderResource.h"
#include "Interfaces/Interface_AssetUserData.h"

#include "GroomAsset.generated.h"

class UMaterialInterface;
class UNiagaraSystem;

/* Render buffers for root deformation for dynamic meshes */
struct FHairStrandsRootResource : public FRenderResource
{
	/** Build the hair strands resource */
	FHairStrandsRootResource(const FHairStrandsDatas* HairStrandsDatas, uint32 LODCount);

	/* Init the buffer */
	virtual void InitRHI() override;

	/* Release the buffer */
	virtual void ReleaseRHI() override;

	/* Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FHairStrandsRootResource"); }
	
	FRWBuffer RootPositionBuffer;
	FRWBuffer RootNormalBuffer;

	struct FMeshProjectionLOD
	{
		enum class EStatus { Invalid, Initialized, Completed };
		EStatus Status = EStatus::Invalid;
		int32 LODIndex = -1;

		/* Triangle on which a root is attached */
		FRWBuffer RootTriangleIndexBuffer;
		FRWBuffer RootTriangleBarycentricBuffer;
	
		/* Strand hair roots translation and rotation in rest position relative to the bound triangle. Positions are relative to the rest root center */
		FVector	  RestRootOffset = FVector::ZeroVector;
		FRWBuffer RestRootTrianglePosition0Buffer;
		FRWBuffer RestRootTrianglePosition1Buffer;
		FRWBuffer RestRootTrianglePosition2Buffer;

		/* Strand hair roots translation and rotation in triangle-deformed position relative to the bound triangle. Positions are relative the deformed root center*/
		FVector   DeformedRootOffset = FVector::ZeroVector;
		FRWBuffer DeformedRootTrianglePosition0Buffer;
		FRWBuffer DeformedRootTrianglePosition1Buffer;
		FRWBuffer DeformedRootTrianglePosition2Buffer;
	};

	/* Store the hair projection information for each mesh LOD */
	TArray<FMeshProjectionLOD> MeshProjectionLODs;

	/* Strand hair vertex to curve index */
	FRWBuffer VertexToCurveIndexBuffer;

	const uint32 RootCount;
	/* Curve index for every vertices */
	TArray<FHairStrandsIndexFormat::Type> CurveIndices;

	/* Curve root's positions */
	TArray<FHairStrandsRootPositionFormat::Type> RootPositions;

	/* Curve root's normal orientation */
	TArray<FHairStrandsRootNormalFormat::Type> RootNormals;
};

/* Render buffers that will be used for rendering */
struct FHairStrandsRestResource : public FRenderResource
{
	/** Build the hair strands resource */
	FHairStrandsRestResource(const FHairStrandsDatas::FRenderData& HairStrandRenderData, const FVector& PositionOffset);

	/* Init the buffer */
	virtual void InitRHI() override;

	/* Release the buffer */
	virtual void ReleaseRHI() override;

	/* Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FHairStrandsResource"); }

	/* Strand hair rest position buffer */
	FRWBuffer RestPositionBuffer;

	/* Strand hair attribute buffer */
	FRWBuffer AttributeBuffer;

	/* Strand hair material buffer */
	FRWBuffer MaterialBuffer;
	
	/* Position offset as the rest positions are expressed in relative coordinate (16bits) */
	FVector PositionOffset = FVector::ZeroVector;

	/* Reference to the hair strands render data */
	const FHairStrandsDatas::FRenderData& RenderData;
};

struct FHairStrandsDeformedResource : public FRenderResource
{
	/** Build the hair strands resource */
	FHairStrandsDeformedResource(const FHairStrandsDatas::FRenderData& HairStrandRenderData, bool bInitializeData);

	/* Init the buffer */
	virtual void InitRHI() override;

	/* Release the buffer */
	virtual void ReleaseRHI() override;

	/* Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FHairStrandsDeformedResource"); }

	/* Strand hair deformed position buffer (previous and current) */
	FRWBuffer DeformedPositionBuffer[2];

	/* Strand hair tangent buffer */
	FRWBuffer TangentBuffer;

	/* Position offset as the deformed positions are expressed in relative coordinate (16bits) */
	FVector PositionOffset = FVector::ZeroVector;

	/* Reference to the hair strands render data */
	const FHairStrandsDatas::FRenderData& RenderData;

	/* Whether the GPU data should be initialized with the asset data or not */
	const bool bInitializedData;
};

struct FHairStrandsInterpolationResource : public FRenderResource
{
	/** Build the hair strands resource */
	FHairStrandsInterpolationResource(const FHairStrandsInterpolationDatas::FRenderData& InterpolationRenderData, const FHairStrandsDatas& SimDatas);

	/* Init the buffer */
	virtual void InitRHI() override;

	/* Release the buffer */
	virtual void ReleaseRHI() override;

	/* Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FHairStrandsInterplationResource"); }

	FRWBuffer Interpolation0Buffer;
	FRWBuffer Interpolation1Buffer;

	// For debug purpose only (should be remove once all hair simulation is correctly handled)
	FRWBuffer SimRootPointIndexBuffer;
	TArray<FHairStrandsRootIndexFormat::Type> SimRootPointIndex;

	/* Reference to the hair strands interpolation render data */
	const FHairStrandsInterpolationDatas::FRenderData& RenderData;
};

#if RHI_RAYTRACING
struct FHairStrandsRaytracingResource : public FRenderResource
{
	/** Build the hair strands resource */
	FHairStrandsRaytracingResource(const FHairStrandsDatas& HairStrandsDatas);

	/* Init the buffer */
	virtual void InitRHI() override;

	/* Release the buffer */
	virtual void ReleaseRHI() override;

	/* Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FHairStrandsRaytracingResource"); }

	FRWBuffer PositionBuffer;
	FRayTracingGeometry RayTracingGeometry;
	uint32 VertexCount;
};
#endif

USTRUCT(BlueprintType)
struct HAIRSTRANDSCORE_API FHairGroupInfo
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Info")
	int32 GroupID = 0;

	UPROPERTY(VisibleAnywhere, Category = "Info", meta = (DisplayName = "Curve Count"))
	int32 NumCurves = 0;

	UPROPERTY(VisibleAnywhere, Category = "Info", meta = (DisplayName = "Guide Count"))
	int32 NumGuides = 0;

	UPROPERTY(VisibleAnywhere, Category = "Info", meta = (DisplayName = "Auto-generated Guides", ToolTip = "Checked when guides were auto-generated"))
	bool bIsAutoGenerated = false;

	UPROPERTY(EditAnywhere, Category="Rendering")
	UMaterialInterface* Material = nullptr;

	// Currently hide the Nigara simulation slot as it is not used, and could confuse users
	//UPROPERTY(EditAnywhere, Category="Simulation", meta = (DisplayName = "Niagara System Asset"))
	//UNiagaraSystem* NiagaraAsset = nullptr;
};

struct HAIRSTRANDSCORE_API FHairGroupData
{
	FHairStrandsDatas HairRenderData;
	FHairStrandsDatas HairSimulationData;
	FHairStrandsInterpolationDatas HairInterpolationData;

	/** Interpolated hair render resource to be allocated */
	FHairStrandsRestResource* HairStrandsRestResource = nullptr;

	/** Guide render resource to be allocated */
	FHairStrandsRestResource* HairSimulationRestResource = nullptr;

	friend FArchive& operator<<(FArchive& Ar, FHairGroupData& GroupData);
};

/**
 * Implements an asset that can be used to store hair strands
 */
UCLASS(BlueprintType, hidecategories = (Object))
class HAIRSTRANDSCORE_API UGroomAsset : public UObject, public IInterface_AssetUserData
{
	GENERATED_BODY()

#if WITH_EDITOR
	/** Notification when anything changed */
	DECLARE_MULTICAST_DELEGATE(FOnGroomAssetChanged);
#endif

public:

	UPROPERTY(EditAnywhere, EditFixedSize, BlueprintReadWrite, Category = "HairGroups", meta = (DisplayName = "Group"))
	TArray<FHairGroupInfo> HairGroupsInfo;

	TArray<FHairGroupData> HairGroupsData;

	//~ Begin UObject Interface.
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;
	virtual void Serialize(FArchive& Ar) override;

	/** Density factor for converting hair into guide curve if no guides are provided. */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "BuildSettings", meta = (ClampMin = "0.01", UIMin = "0.01", UIMax = "1.0"))
	float HairToGuideDensity = 0.1f;

#if WITH_EDITOR
	FOnGroomAssetChanged& GetOnGroomAssetChanged() { return OnGroomAssetChanged;  }

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

	void Reset();

	int32 GetNumHairGroups() const;

#if WITH_EDITOR
	FOnGroomAssetChanged OnGroomAssetChanged;
#endif

	//~ Begin IInterface_AssetUserData Interface
	virtual void AddAssetUserData(UAssetUserData* InUserData) override;
	virtual void RemoveUserDataOfClass(TSubclassOf<UAssetUserData> InUserDataClass) override;
	virtual UAssetUserData* GetAssetUserDataOfClass(TSubclassOf<UAssetUserData> InUserDataClass) override;
	virtual const TArray<UAssetUserData*>* GetAssetUserDataArray() const override;
	//~ End IInterface_AssetUserData Interface

//private : 


	TUniquePtr<FHairDescription> HairDescription;

	/** Array of user data stored with the asset */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Instanced, Category = HairLab)
	TArray<UAssetUserData*> AssetUserData;
};
