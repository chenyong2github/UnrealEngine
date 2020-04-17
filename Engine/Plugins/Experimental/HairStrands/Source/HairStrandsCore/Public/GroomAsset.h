// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "HairDescription.h"
#include "HairStrandsDatas.h"
#include "RenderResource.h"
#include "GroomSettings.h"
#include "Curves/CurveFloat.h"
#include "HairStrandsInterface.h"
#include "Interfaces/Interface_AssetUserData.h"
#include "Engine/SkeletalMesh.h"
#include "Interfaces/Interface_AssetUserData.h"

#include "GroomAsset.generated.h"


/** List of niagara solvers */
UENUM(BlueprintType)
enum class EGroomNiagaraSolvers : uint8
{
	None = 0 UMETA(Hidden),
	CosseratRods = 0x02 UMETA(DisplatName = "GroomRods"),
	AngularSprings = 0x04 UMETA(DisplatName = "GroomSprings"),
	CustomSolver = 0x08 UMETA(DisplatName = "CustomSolver")
};

/** Size of each strands*/
UENUM(BlueprintType)
enum class EGroomStrandsSize : uint8
{
	None = 0 UMETA(Hidden),
	Size2 = 0x02 UMETA(DisplatName = "2"),
	Size4 = 0x04 UMETA(DisplatName = "4"),
	Size8 = 0x08 UMETA(DisplatName = "8"),
	Size16 = 0x10 UMETA(DisplatName = "16"),
	Size32 = 0x20 UMETA(DisplatName = "32")
};

class UAssetUserData;
class UMaterialInterface;
class UNiagaraSystem;
struct FHairGroupData;

/* Source/CPU data for root resources (GPU resources are stored into FHairStrandsRootResources) */
struct FHairStrandsRootData
{
	/** Build the hair strands resource */
	FHairStrandsRootData();
	FHairStrandsRootData(const FHairStrandsDatas* HairStrandsDatas, uint32 LODCount, const TArray<uint32>& NumSamples);
	void Serialize(FArchive& Ar);
	void Reset();
	bool HasProjectionData() const;

	struct FMeshProjectionLOD
	{
		int32 LODIndex = -1;

		/* Triangle on which a root is attached */
		/* When the projection is done with source to target mesh transfer, the projection indices does not match.
		   In this case we need to separate index computation. The barycentric coords remain the same however. */
		TArray<FHairStrandsCurveTriangleIndexFormat::Type> RootTriangleIndexBuffer;
		TArray<FHairStrandsCurveTriangleBarycentricFormat::Type> RootTriangleBarycentricBuffer;

		/* Strand hair roots translation and rotation in rest position relative to the bound triangle. Positions are relative to the rest root center */
		TArray<FHairStrandsMeshTrianglePositionFormat::Type> RestRootTrianglePosition0Buffer;
		TArray<FHairStrandsMeshTrianglePositionFormat::Type> RestRootTrianglePosition1Buffer;
		TArray<FHairStrandsMeshTrianglePositionFormat::Type> RestRootTrianglePosition2Buffer;

		/* Number of samples used for the mesh interpolation */
		uint32 SampleCount = 0;

		/* Store the hair interpolation weights | Size = SamplesCount * SamplesCount */
		TArray<FHairStrandsWeightFormat::Type> MeshInterpolationWeightsBuffer;

		/* Store the samples vertex indices */
		TArray<FHairStrandsIndexFormat::Type> MeshSampleIndicesBuffer;

		/* Store the samples rest positions */
		TArray<FHairStrandsMeshTrianglePositionFormat::Type> RestSamplePositionsBuffer;
	};

	/* Number of roots */
	uint32 RootCount;

	/* Curve index for every vertices */
	TArray<FHairStrandsIndexFormat::Type> VertexToCurveIndexBuffer;

	/* Curve root's positions */
	TArray<FHairStrandsRootPositionFormat::Type> RootPositionBuffer;

	/* Curve root's normal orientation */
	TArray<FHairStrandsRootNormalFormat::Type> RootNormalBuffer;

	/* Store the hair projection information for each mesh LOD */
	TArray<FMeshProjectionLOD> MeshProjectionLODs;
};

/* Render buffers for root deformation for dynamic meshes */
struct FHairStrandsRestRootResource : public FRenderResource
{
	/** Build the hair strands resource */
	FHairStrandsRestRootResource();
	FHairStrandsRestRootResource(const FHairStrandsRootData& RootData);
	FHairStrandsRestRootResource(const FHairStrandsDatas* HairStrandsDatas, uint32 LODCount, const TArray<uint32>& NumSamples);

	/* Init the buffer */
	virtual void InitRHI() override;

	/* Release the buffer */
	virtual void ReleaseRHI() override;

	/* Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FHairStrandsRestRootResource"); }

	/* Populate GPU LOD data from RootData (this function doesn't initialize resources) */
	void PopulateFromRootData();

	FRWBuffer RootPositionBuffer;
	FRWBuffer RootNormalBuffer;
	FRWBuffer VertexToCurveIndexBuffer;

	struct FMeshProjectionLOD
	{
		const bool IsValid() const { return Status == FHairStrandsProjectionHairData::EStatus::Completed; }
		FHairStrandsProjectionHairData::EStatus Status = FHairStrandsProjectionHairData::EStatus::Invalid;
		int32 LODIndex = -1;

		/* Triangle on which a root is attached */
		/* When the projection is done with source to target mesh transfer, the projection indices does not match.
		   In this case we need to separate index computation. The barycentric coords remain the same however. */
		FRWBuffer RootTriangleIndexBuffer;
		FRWBuffer RootTriangleBarycentricBuffer;

		/* Strand hair roots translation and rotation in rest position relative to the bound triangle. Positions are relative to the rest root center */
		FRWBuffer RestRootTrianglePosition0Buffer;
		FRWBuffer RestRootTrianglePosition1Buffer;
		FRWBuffer RestRootTrianglePosition2Buffer;

		/* Strand hair mesh interpolation matrix and sample indices */
		uint32 SampleCount = 0;
		FRWBuffer MeshInterpolationWeightsBuffer;
		FRWBuffer MeshSampleIndicesBuffer;
		FRWBuffer RestSamplePositionsBuffer;
	};

	/* Store the hair projection information for each mesh LOD */
	TArray<FMeshProjectionLOD> MeshProjectionLODs;

	/* Store CPU data for root info & root binding */
	FHairStrandsRootData RootData;
};


/* Render buffers for root deformation for dynamic meshes */
struct FHairStrandsDeformedRootResource : public FRenderResource
{
	/** Build the hair strands resource */
	FHairStrandsDeformedRootResource();
	FHairStrandsDeformedRootResource(const FHairStrandsRestRootResource* InRestResources);

	/* Init the buffer */
	virtual void InitRHI() override;

	/* Release the buffer */
	virtual void ReleaseRHI() override;

	/* Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FHairStrandsDeformedRootResource"); }

	struct FMeshProjectionLOD
	{
		const bool IsValid() const { return Status == FHairStrandsProjectionHairData::EStatus::Completed; }
		FHairStrandsProjectionHairData::EStatus Status = FHairStrandsProjectionHairData::EStatus::Invalid;
		int32 LODIndex = -1;

		/* Strand hair roots translation and rotation in triangle-deformed position relative to the bound triangle. Positions are relative the deformed root center*/
		FRWBuffer DeformedRootTrianglePosition0Buffer;
		FRWBuffer DeformedRootTrianglePosition1Buffer;
		FRWBuffer DeformedRootTrianglePosition2Buffer;

		/* Strand hair mesh interpolation matrix and sample indices */
		uint32 SampleCount = 0;
		FRWBuffer DeformedSamplePositionsBuffer;
		FRWBuffer MeshSampleWeightsBuffer;
	};

	/* Store the hair projection information for each mesh LOD */
	uint32 RootCount = 0;
	TArray<FMeshProjectionLOD> MeshProjectionLODs;
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
	const bool bInitializedData = false;

	/* Whether the GPU data should be initialized with the asset data or not */
	uint32 CurrentIndex = 0;
};

struct FHairStrandsClusterCullingResource : public FRenderResource
{
	FHairStrandsClusterCullingResource(const FHairStrandsDatas& RenStrandsData);

	/* Init the buffer */
	virtual void InitRHI() override;

	/* Release the buffer */
	virtual void ReleaseRHI() override;

	/* Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FHairStrandsClusterResource"); }


	struct FClusterInfo
	{
		uint32 FirstVertexId;
		uint32 VertexIdCountLodHigh;
		uint32 VertexIdCountLodLow;
		uint32 UnusedUint;
	};
	TArray<FClusterInfo> ClusterInfoArray;
	TArray<uint32> VertexToClusterIdArray;
	TArray<uint32> ClusterVertexIdArray;
	TArray<float> ClusterIndexRadiusScaleInfoArray;

	/* Cluster info buffer */
	FReadBuffer ClusterInfoBuffer;		// Contains: Start VertexId, VertexId Count (for VertexToClusterIdBuffer)

	/* VertexId => ClusterId to know which AABB to contribute to*/
	FReadBuffer VertexToClusterIdBuffer;

	/* Concatenated data for each cluster: list of VertexId pointed to by ClusterInfoBuffer */
	FReadBuffer ClusterVertexIdBuffer;

	/* Contains information to recove the radius scale to apply per cluster when decimating vertex count */
	FReadBuffer ClusterIndexRadiusScaleInfoBuffer;

	/* Number of cluster  */
	uint32 ClusterCount;
	/* Number of vertex  */
	uint32 VertexCount;
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
struct HAIRSTRANDSCORE_API FHairSolverSettings
{
	GENERATED_BODY()

	FHairSolverSettings();

	/** Enable the simulation for the group */
	UPROPERTY(EditAnywhere, Category = "SolverSettings", meta = (ToolTip = "Enable the simulation on that group"))
	bool EnableSimulation;

	/** Niagara solver to be used for simulation */
	UPROPERTY(EditAnywhere, Category = "SolverSettings", meta = (ToolTip = "Niagara solver to be used for simulation"))
	EGroomNiagaraSolvers NiagaraSolver;

	/** Custom system to be used for simulation */
	UPROPERTY(EditAnywhere, Category = "SolverSettings", meta = (EditCondition = "NiagaraSolver == EGroomNiagaraSolvers::CustomSolver", ToolTip = "Custom niagara system to be used if custom solver is picked"))
	TSoftObjectPtr<UNiagaraSystem> CustomSystem;

	/** Number of substeps to be used */
	UPROPERTY(EditAnywhere, Category = "SolverSettings", meta = (ToolTip = "Number of sub steps to be done per frame. The actual solver calls are done at 24 fps"))
	int32 SubSteps;

	/** Number of iterations for the constraint solver  */
	UPROPERTY(EditAnywhere, Category = "SolverSettings", meta = (ToolTip = "Number of iterations to solve the constraints with the xpbd solver"))
	int32 IterationCount;
};

USTRUCT(BlueprintType)
struct HAIRSTRANDSCORE_API FHairExternalForces
{
	GENERATED_BODY()

	FHairExternalForces();

	/** Acceleration vector in cm/s2 to be used for the gravity*/
	UPROPERTY(EditAnywhere, Category = "ExternalForces", meta = (ToolTip = "Acceleration vector in cm/s2 to be used for the gravity"))
	FVector GravityVector;

	/** Coefficient between 0 and 1 to be used for the air drag */
	UPROPERTY(EditAnywhere, Category = "ExternalForces", meta = (ToolTip = "Coefficient between 0 and 1 to be used for the air drag"))
	float AirDrag;

	/** Velocity of the surrounding air in cm/s */
	UPROPERTY(EditAnywhere, Category = "ExternalForces", meta = (ToolTip = "Velocity of the surrounding air in cm/s"))
	FVector AirVelocity;
};

USTRUCT(BlueprintType)
struct HAIRSTRANDSCORE_API FHairBendConstraint
{
	GENERATED_BODY()

	FHairBendConstraint();

	/** Enable the solve of the bend constraint during the xpbd loop */
	UPROPERTY(EditAnywhere, Category = "BendConstraint", meta = (ToolTip = "Enable the solve of the bend constraint during the xpbd loop"))
	bool SolveBend;

	/** Enable the projection of the bend constraint after the xpbd loop */
	UPROPERTY(EditAnywhere, Category = "BendConstraint", meta = (ToolTip = "Enable ther projection of the bend constraint after the xpbd loop"))
	bool ProjectBend;

	/** Damping for the bend constraint between 0 and 1 */
	UPROPERTY(EditAnywhere, Category = "BendConstraint", meta = (ToolTip = "Damping for the bend constraint between 0 and 1"))
	float BendDamping;

	/** Stiffness for the bend constraint in GPa */
	UPROPERTY(EditAnywhere, Category = "BendConstraint", meta = (ToolTip = "Stiffness for the bend constraint in GPa"))
	float BendStiffness;

	/** Stiffness scale along the strand */
	UPROPERTY(EditAnywhere, Category = "BendConstraint", meta = (DisplayName = "Stiffness Scale", ViewMinInput = "0.0", ViewMaxInput = "1.0", ViewMinOutput = "0.0", ViewMaxOutput = "1.0", TimeLineLength = "1.0", XAxisName = "Strand Coordinate (0,1)", YAxisName = "Bend Scale", ToolTip = "This curve determines how much the bend stiffness will be scaled along each strand. \n The X axis range is [0,1], 0 mapping the root and 1 the tip"))
	FRuntimeFloatCurve BendScale;
};


USTRUCT(BlueprintType)
struct HAIRSTRANDSCORE_API FHairStretchConstraint
{
	GENERATED_BODY()

	FHairStretchConstraint();

	/** Enable the solve of the stretch constraint during the xpbd loop */
	UPROPERTY(EditAnywhere, Category = "StretchConstraint", meta = (ToolTip = "Enable the solve of the stretch constraint during the xpbd loop"))
	bool SolveStretch;

	/** Enable the projection of the stretch constraint after the xpbd loop */
	UPROPERTY(EditAnywhere, Category = "StretchConstraint", meta = (ToolTip = "Enable ther projection of the stretch constraint after the xpbd loop"))
	bool ProjectStretch;

	/** Damping for the stretch constraint between 0 and 1 */
	UPROPERTY(EditAnywhere, Category = "StretchConstraint", meta = (ToolTip = "Damping for the stretch constraint between 0 and 1"))
	float StretchDamping;

	/** Stiffness for the stretch constraint in GPa */
	UPROPERTY(EditAnywhere, Category = "StretchConstraint", meta = (ToolTip = "Stiffness for the stretch constraint in GPa"))
	float StretchStiffness;

	/** Stretch scale along the strand  */
	UPROPERTY(EditAnywhere, Category = "StretchConstraint", meta = (DisplayName = "Stiffness Scale", ViewMinInput = "0.0", ViewMaxInput = "1.0", ViewMinOutput = "0.0", ViewMaxOutput = "1.0", TimeLineLength = "1.0", XAxisName = "Strand Coordinate (0,1)", YAxisName = "Stretch Scale", ToolTip = "This curve determines how much the stretch stiffness will be scaled along each strand. \n The X axis range is [0,1], 0 mapping the root and 1 the tip"))
	FRuntimeFloatCurve StretchScale;
};

USTRUCT(BlueprintType)
struct HAIRSTRANDSCORE_API FHairCollisionConstraint
{
	GENERATED_BODY()

	FHairCollisionConstraint();

	/** Enable the solve of the collision constraint during the xpbd loop  */
	UPROPERTY(EditAnywhere, Category = "CollisionConstraint", meta = (ToolTip = "Enable the solve of the collision constraint during the xpbd loop"))
	bool SolveCollision;

	/** Enable ther projection of the collision constraint after the xpbd loop */
	UPROPERTY(EditAnywhere, Category = "CollisionConstraint", meta = (ToolTip = "Enable ther projection of the collision constraint after the xpbd loop"))
	bool ProjectCollision;

	/** Static friction used for collision against the physics asset */
	UPROPERTY(EditAnywhere, Category = "Collision Constraint", meta = (ToolTip = "Static friction used for collision against the physics asset"))
	float StaticFriction;

	/** Kinetic friction used for collision against the physics asset*/
	UPROPERTY(EditAnywhere, Category = "CollisionConstraint", meta = (ToolTip = "Kinetic friction used for collision against the physics asset"))
	float KineticFriction;

	/** Viscosity parameter between 0 and 1 that will be used for self collision */
	UPROPERTY(EditAnywhere, Category = "CollisionConstraint", meta = (ToolTip = "Viscosity parameter between 0 and 1 that will be used for self collision"))
	float StrandsViscosity;

	/** Dimension of the grid used to compute the viscosity force */
	UPROPERTY(EditAnywhere, Category = "CollisionConstraint", meta = (ToolTip = "Dimension of the grid used to compute the viscosity force"))
	FIntVector GridDimension;

	/** Radius that will be used for the collision detection against the physics asset */
	UPROPERTY(EditAnywhere, Category = "CollisionConstraint", meta = (ToolTip = "Radius that will be used for the collision detection against the physics asset"))
	float CollisionRadius;

	/** Radius scale along the strand */
	UPROPERTY(EditAnywhere, Category = "CollisionConstraint", meta = (ViewMinInput = "0.0", ViewMaxInput = "1.0", ViewMinOutput = "0.0", ViewMaxOutput = "1.0", TimeLineLength = "1.0", XAxisName = "Strand Coordinate (0,1)", YAxisName = "Collision Radius", ToolTip = "This curve determines how much the collision radius will be scaled along each strand. \n The X axis range is [0,1], 0 mapping the root and 1 the tip"))
	FRuntimeFloatCurve RadiusScale;
};


USTRUCT(BlueprintType)
struct HAIRSTRANDSCORE_API FHairMaterialConstraints
{
	GENERATED_BODY()

	FHairMaterialConstraints();

	/** Radius scale along the strand */
	UPROPERTY(EditAnywhere, Category = "MaterialConstraints", meta = (ToolTip = "Bend constraint settings to be applied to the hair strands"))
	FHairBendConstraint BendConstraint;

	/** Radius scale along the strand */
	UPROPERTY(EditAnywhere, Category = "MaterialConstraints", meta = (ToolTip = "Stretch constraint settings to be applied to the hair strands"))
	FHairStretchConstraint StretchConstraint;

	/** Radius scale along the strand */
	UPROPERTY(EditAnywhere, Category = "MaterialConstraints", meta = (ToolTip = "Collision constraint settings to be applied to the hair strands"))
	FHairCollisionConstraint CollisionConstraint;
};

USTRUCT(BlueprintType)
struct HAIRSTRANDSCORE_API FHairStrandsParameters
{
	GENERATED_BODY()

	FHairStrandsParameters();

	/** Number of particles per guide that will be used for simulation*/
	UPROPERTY(EditAnywhere, Category = "StrandsParameters", meta = (ToolTip = "Number of particles per guide that will be used for simulation"))
	EGroomStrandsSize StrandsSize;

	/** Density of the strands in g/cm3 */
	UPROPERTY(EditAnywhere, Category = "StrandsParameters", meta = (ToolTip = "Density of the strands in g/cm3"))
	float StrandsDensity;

	/** Smoothing between 0 and 1 of the incoming guides curves for better stability */
	UPROPERTY(EditAnywhere, Category = "StrandsParameters", meta = (ToolTip = "Smoothing between 0 and 1 of the incoming guides curves for better stability"))
	float StrandsSmoothing;

	/** Strands thickness in cm that will be used for mass and inertia computation */
	UPROPERTY(EditAnywhere, Category = "StrandsParameters", meta = (ToolTip = "Strands thickness in cm that will be used for mass and inertia computation"))
	float StrandsThickness;

	/** Thickness scale along the curve */
	UPROPERTY(EditAnywhere, Category = "StrandsParameters", meta = (ViewMinInput = "0.0", ViewMaxInput = "1.0", ViewMinOutput = "0.0", ViewMaxOutput = "1.0", TimeLineLength = "1.0", XAxisName = "Strand Coordinate (0,1)", YAxisName = "Strands Thickness", ToolTip = "This curve determines how much the strands thickness will be scaled along each strand. \n The X axis range is [0,1], 0 mapping the root and 1 the tip"))
	FRuntimeFloatCurve ThicknessScale;
};

USTRUCT(BlueprintType)
struct HAIRSTRANDSCORE_API FHairGroupsPhysics
{
	GENERATED_BODY()

	FHairGroupsPhysics();

	/** Enable the simulation for the group */
	UPROPERTY(EditAnywhere, Category = "GroupsPhysics", meta = (ToolTip = "Solver Settings for the hair physics"))
	FHairSolverSettings SolverSettings;

	/** Enable the simulation for the group */
	UPROPERTY(EditAnywhere, Category = "GroupsPhysics", meta = (ToolTip = "External Forces for the hair physics"))
	FHairExternalForces ExternalForces;

	/** Enable the simulation for the group */
	UPROPERTY(EditAnywhere, Category = "GroupsPhysics", meta = (ToolTip = "Material Constraints for the hair physics"))
	FHairMaterialConstraints MaterialConstraints;

	/** Enable the simulation for the group */
	UPROPERTY(EditAnywhere, Category = "GroupsPhysics", meta = (ToolTip = "Strands Parameters for the hair physics"))
	FHairStrandsParameters StrandsParameters;
};

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

	UPROPERTY(VisibleAnywhere, Category = "Info", meta = (DisplayName = "Import options", ToolTip = "Show the options used at import time"))
	FGroomBuildSettings ImportSettings;

	UPROPERTY(EditAnywhere, Category = "Rendering")
	UMaterialInterface* Material = nullptr;

	friend FArchive& operator<<(FArchive& Ar, FHairGroupInfo& GroupInfo);
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

	/** Interpolation resource to be allocated */
	FHairStrandsInterpolationResource* HairInterpolationResource = nullptr;

	/** Cluster culling resource to be allocated */
	FHairStrandsClusterCullingResource* ClusterCullingResource = nullptr;

	friend FArchive& operator<<(FArchive& Ar, FHairGroupData& GroupData);
};

/**
 * Implements an asset that can be used to store hair strands
 */
UCLASS(BlueprintType, hidecategories = (Object))
class HAIRSTRANDSCORE_API UGroomAsset : public UObject, public IInterface_AssetUserData
{
	GENERATED_UCLASS_BODY()

#if WITH_EDITOR
	/** Notification when anything changed */
	DECLARE_MULTICAST_DELEGATE(FOnGroomAssetChanged);
#endif

public:

	UPROPERTY(EditAnywhere, EditFixedSize, BlueprintReadWrite, Category = "HairGroups", meta = (DisplayName = "Group"))
	TArray<FHairGroupInfo> HairGroupsInfo;

	UPROPERTY(EditAnywhere, EditFixedSize, BlueprintReadWrite, Category = "HairPhysics", meta = (DisplayName = "Group"))
	TArray<FHairGroupsPhysics> HairGroupsPhysics;

	/** Enable radial basis function interpolation to be used instead of the local skin rigid transform */
	UPROPERTY(EditAnywhere, EditFixedSize, BlueprintReadWrite, Category = "HairInterpolation", meta = (ToolTip = "Enable radial basis function interpolation to be used instead of the local skin rigid transform (WIP)"))
	bool EnableGlobalInterpolation = false;

	TArray<FHairGroupData> HairGroupsData;

	//~ Begin UObject Interface.
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;
	virtual void Serialize(FArchive& Ar) override;

	/** Density factor for converting hair into guide curve if no guides are provided. */
	UPROPERTY(BlueprintReadWrite, Category = "BuildSettings", meta = (ClampMin = "0.01", UIMin = "0.01", UIMax = "1.0"))
	float HairToGuideDensity = 0.1f; // To remove, as this is now stored into the import settings

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

	bool IsValid() const { return bIsInitialized; }

	/** Initialize resources. */
	void InitResource();

	/** Update resources. */
	void UpdateResource();

	/** Release the hair strands resource. */
	void ReleaseResource();

	void Reset();

	int32 GetNumHairGroups() const;

	/** Returns true if the asset has the HairDescription needed to recompute its groom data */
	bool CanRebuildFromDescription() const;

	//~ Begin IInterface_AssetUserData Interface
	virtual void AddAssetUserData(UAssetUserData* InUserData) override;
	virtual void RemoveUserDataOfClass(TSubclassOf<UAssetUserData> InUserDataClass) override;
	virtual UAssetUserData* GetAssetUserDataOfClass(TSubclassOf<UAssetUserData> InUserDataClass) override;
	virtual const TArray<UAssetUserData*>* GetAssetUserDataArray() const override;
	//~ End IInterface_AssetUserData Interface

//private :
#if WITH_EDITOR
	FOnGroomAssetChanged OnGroomAssetChanged;
#endif

	/** Array of user data stored with the asset */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Instanced, Category = HairLab)
	TArray<UAssetUserData*> AssetUserData;

#if WITH_EDITORONLY_DATA
public:
	/** Commits a HairDescription to buffer for serialization */
	void CommitHairDescription(FHairDescription&& HairDescription);

	/** Caches the computed groom data with the given build settings from/to the Derived Data Cache, building it if needed */
	bool CacheDerivedData(const struct FGroomBuildSettings* BuildSettings = nullptr);

private:
	FString BuildDerivedDataKeySuffix(const struct FGroomBuildSettings& BuildSettings);

	TUniquePtr<FHairDescription> HairDescription;
	TUniquePtr<FHairDescriptionBulkData> HairDescriptionBulkData;

	UPROPERTY()
	bool bIsCacheable = true;
#endif
	bool bIsInitialized;
};

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

	/** Number of points to be used for radial basis function interpolation  */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "HairInterpolation", meta = (ClampMax = "100", ClampMin = "0", ToolTip = "Number of points to be used for radial basis function interpolation (WIP)"))
	int32 NumInterpolationPoints = 100;

	UPROPERTY(EditAnywhere, EditFixedSize, BlueprintReadWrite, Category = "HairGroups", meta = (DisplayName = "Group"))
	TArray<FGoomBindingGroupInfo> GroupInfos;

	/** GPU and CPU binding data for both simulation and rendering. */
	struct FHairGroupResource
	{
		FHairStrandsRestRootResource* SimRootResources = nullptr;
		FHairStrandsRestRootResource* RenRootResources = nullptr;
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
	};
	typedef TArray<FHairGroupData> FHairGroupDatas;
	FHairGroupDatas HairGroupDatas;

	//~ Begin UObject Interface.
	virtual void PostLoad() override;
	virtual void PreSave(const class ITargetPlatform* TargetPlatform) override;
	virtual void PostSaveRoot(bool bCleanupIsRequired) override;
	virtual void BeginDestroy() override;
	virtual void Serialize(FArchive& Ar) override;

	static bool IsCompatible(const USkeletalMesh* InSkeletalMesh, const UGroomBindingAsset* InBinding);
	static bool IsCompatible(const UGroomAsset* InGroom, const UGroomBindingAsset* InBinding);
	static bool IsBindingAssetValid(const UGroomBindingAsset* InBinding, bool bIsBindingReloading=false);

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

	//private :
#if WITH_EDITOR
	FOnGroomBindingAssetChanged OnGroomBindingAssetChanged;
#endif
};

struct FProcessedHairDescription
{
	typedef TPair<FHairGroupInfo, FHairGroupData> FHairGroup;
	typedef TMap<int32, FHairGroup> FHairGroups;
	FHairGroups HairGroups;
	bool bCanUseClosestGuidesAndWeights = false;
	bool bHasUVData = false;
	bool IsValid() const;
};
