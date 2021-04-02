// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceRW.h"
#include "NiagaraCommon.h"
#include "VectorVM.h"
#include "GroomAsset.h"
#include "GroomActor.h"
#include "NiagaraDataInterfaceHairStrands.generated.h"

static const int32 MaxDelay = 2;
static const int32 NumScales = 4;
static const int32 StretchOffset = 0;
static const int32 BendOffset = 1;
static const int32 RadiusOffset = 2;
static const int32 ThicknessOffset = 3;

struct FNDIHairStrandsData;

/** Render buffers that will be used in hlsl functions */
struct FNDIHairStrandsBuffer : public FRenderResource
{
	/** Set the asset that will be used to affect the buffer */
	void Initialize(const FHairStrandsDatas*  HairStrandsDatas, 
		const FHairStrandsRestResource*  HairStrandsRestResource, 
		const FHairStrandsDeformedResource*  HairStrandsDeformedResource, 
		const FHairStrandsRestRootResource* HairStrandsRestRootResource, 
		const FHairStrandsDeformedRootResource* HairStrandsDeformedRootResource,
		const TStaticArray<float, 32 * NumScales>& InParamsScale);  

	/** Set the asset that will be used to affect the buffer */
	void Update(const FHairStrandsDatas* HairStrandsDatas,
		const FHairStrandsRestResource* HairStrandsRestResource,
		const FHairStrandsDeformedResource* HairStrandsDeformedResource,
		const FHairStrandsRestRootResource* HairStrandsRestRootResource,
		const FHairStrandsDeformedRootResource* HairStrandsDeformedRootResource);

	/** Transfer CPU datas to GPU */
	void Transfer(const TStaticArray<float, 32 * NumScales>& InParamsScale);

	/** Init the buffer */
	virtual void InitRHI() override;

	/** Release the buffer */
	virtual void ReleaseRHI() override;

	/** Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FNDIHairStrandsBuffer"); }

	/** Strand curves point offset buffer */
	FRWBuffer CurvesOffsetsBuffer;

	/** Deformed position buffer in case no ressource are there */
	FRWBuffer DeformedPositionBuffer;

	/** Bounding Box Buffer*/
	FRWBuffer BoundingBoxBuffer;

	/** Params scale buffer */
	FRWBuffer ParamsScaleBuffer;

	/** The strand asset datas from which to sample */
	const FHairStrandsDatas* SourceDatas;

	/** The strand asset resource from which to sample */
	const FHairStrandsRestResource* SourceRestResources;

	/** The strand deformed resource to write into */
	const FHairStrandsDeformedResource* SourceDeformedResources;

	/** The strand root resource to write into */
	const FHairStrandsRestRootResource* SourceRestRootResources;
	
	/** The strand root resource to write into */
	const FHairStrandsDeformedRootResource* SourceDeformedRootResources;

	/** Scales along the strand */
	TStaticArray<float, 32 * NumScales> ParamsScale;

	/** Bounding box offsets */
	FIntVector4 BoundingBoxOffsets;
};

/** Data stored per strand base instance*/
struct FNDIHairStrandsData
{
	FNDIHairStrandsData()
	{
		ResetDatas();
	}
	/** Initialize the buffers */
	bool Init(class UNiagaraDataInterfaceHairStrands* Interface, FNiagaraSystemInstance* SystemInstance);

	/** Release the buffers */
	void Release();

	/** Update the buffers */
	void Update(UNiagaraDataInterfaceHairStrands* Interface, FNiagaraSystemInstance* SystemInstance, const FHairStrandsDatas* HairStrandsDatas, UGroomAsset* GroomAsset, const int32 GroupIndex, const FTransform& LocalToWorld);

	inline void ResetDatas()
	{
		WorldTransform.SetIdentity();
		GlobalInterpolation = false;

		TickCount = 0;
		ForceReset = true;

		NumStrands = 0;
		StrandsSize = 0;

		SubSteps = 5;
		IterationCount = 20;

		GravityVector = FVector(0.0, 0.0, -981.0);
		AirDrag = 0.1;
		AirVelocity = FVector(0, 0, 0);

		SolveBend = true;
		ProjectBend = false;
		BendDamping = 0.01;
		BendStiffness = 0.01;

		SolveStretch = true;
		ProjectStretch = false;
		StretchDamping = 0.01;
		StretchStiffness = 1.0;

		SolveCollision = true;
		ProjectCollision = true;
		KineticFriction = 0.1;
		StaticFriction = 0.1;
		StrandsViscosity = 1.0;
		GridDimension = FIntVector(30,30,30);
		CollisionRadius = 1.0;

		StrandsDensity = 1.0;
		StrandsSmoothing = 0.1;
		StrandsThickness = 0.01;

		TickingGroup = NiagaraFirstTickGroup;

		for (int32 i = 0; i < 32 * NumScales; ++i)
		{
			ParamsScale[i] = 1.0;
		}
		SkeletalMeshes = 0;
	}

	inline void CopyDatas(const FNDIHairStrandsData* OtherDatas)
	{
		if (OtherDatas != nullptr)
		{
			HairStrandsBuffer = OtherDatas->HairStrandsBuffer;

			WorldTransform = OtherDatas->WorldTransform;

			GlobalInterpolation = OtherDatas->GlobalInterpolation;

			TickCount = OtherDatas->TickCount;
			ForceReset = OtherDatas->ForceReset;

			NumStrands = OtherDatas->NumStrands;
			StrandsSize = OtherDatas->StrandsSize;

			SubSteps = OtherDatas->SubSteps;
			IterationCount = OtherDatas->IterationCount;

			GravityVector = OtherDatas->GravityVector;
			AirDrag = OtherDatas->AirDrag;
			AirVelocity = OtherDatas->AirVelocity;

			SolveBend = OtherDatas->SolveBend;
			ProjectBend = OtherDatas->ProjectBend;
			BendDamping = OtherDatas->BendDamping;
			BendStiffness = OtherDatas->BendStiffness;

			SolveStretch = OtherDatas->SolveStretch;
			ProjectStretch = OtherDatas->ProjectStretch;
			StretchDamping = OtherDatas->StretchDamping;
			StretchStiffness = OtherDatas->StretchStiffness;

			SolveCollision = OtherDatas->SolveCollision;
			ProjectCollision = OtherDatas->ProjectCollision;
			StaticFriction = OtherDatas->StaticFriction;
			KineticFriction = OtherDatas->KineticFriction;
			StrandsViscosity = OtherDatas->StrandsViscosity;
			GridDimension = OtherDatas->GridDimension;
			CollisionRadius = OtherDatas->CollisionRadius;

			StrandsDensity = OtherDatas->StrandsDensity;
			StrandsSmoothing = OtherDatas->StrandsSmoothing;
			StrandsThickness = OtherDatas->StrandsThickness;

			ParamsScale = OtherDatas->ParamsScale;

			SkeletalMeshes = OtherDatas->SkeletalMeshes;

			TickingGroup = OtherDatas->TickingGroup;
		}
	}

	/** Cached World transform. */
	FTransform WorldTransform;

	/** Global Interpolation */
	bool GlobalInterpolation;

	/** Number of strands*/
	int32 NumStrands;

	/** Strand size */
	int32 StrandsSize;

	/** Tick Count*/
	int32 TickCount;

	/** Force reset simulation */
	bool ForceReset;

	/** Strands Gpu buffer */
	FNDIHairStrandsBuffer* HairStrandsBuffer;
	
	/** Number of substeps to be used */
	int32 SubSteps;

	/** Number of iterations for the constraint solver  */
	int32 IterationCount;

	/** Acceleration vector in cm/s2 to be used for the gravity*/
	FVector GravityVector;

	/** Coefficient between 0 and 1 to be used for the air drag */
	float AirDrag;

	/** Velocity of the surrounding air in cm/s  */
	FVector AirVelocity;

	/** Velocity of the surrounding air in cm/s */
	bool SolveBend;

	/** Enable the solve of the bend constraint during the xpbd loop */
	bool ProjectBend;

	/** Damping for the bend constraint between 0 and 1 */
	float BendDamping;

	/** Stiffness for the bend constraint in GPa */
	float BendStiffness;

	/** Enable the solve of the stretch constraint during the xpbd loop */
	bool SolveStretch;

	/** Enable the projection of the stretch constraint after the xpbd loop */
	bool ProjectStretch;

	/** Damping for the stretch constraint between 0 and 1 */
	float StretchDamping;

	/** Stiffness for the stretch constraint in GPa */
	float StretchStiffness;

	/** Enable the solve of the collision constraint during the xpbd loop  */
	bool SolveCollision;

	/** Enable ther projection of the collision constraint after the xpbd loop */
	bool ProjectCollision;

	/** Static friction used for collision against the physics asset */
	float StaticFriction;

	/** Kinetic friction used for collision against the physics asset*/
	float KineticFriction;

	/** Radius that will be used for the collision detection against the physics asset */
	float StrandsViscosity;

	/** Grid Dimension used to compute the viscosity forces */
	FIntVector GridDimension;

	/** Radius scale along the strand */
	float CollisionRadius;

	/** Density of the strands in g/cm3 */
	float StrandsDensity;

	/** Smoothing between 0 and 1 of the incoming guides curves for better stability */
	float StrandsSmoothing;

	/** Strands thickness in cm that will be used for mass and inertia computation */
	float StrandsThickness;

	/** Scales along the strand */
	TStaticArray<float, 32 * NumScales> ParamsScale;

	/** List of all the skel meshes in the hierarchy*/
	uint32 SkeletalMeshes;

	/** The instance ticking group */
	ETickingGroup TickingGroup;
};

/** Data Interface for the strand base */
UCLASS(EditInlineNew, Category = "Strands", meta = (DisplayName = "Hair Strands"))
class HAIRSTRANDSCORE_API UNiagaraDataInterfaceHairStrands : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()

public:

	DECLARE_NIAGARA_DI_PARAMETER();

	/** Hair Strands Asset used to sample from when not overridden by a source actor from the scene. Also useful for previewing in the editor. */
	UPROPERTY(EditAnywhere, Category = "Source")
	UGroomAsset* DefaultSource;

	/** The source actor from which to sample */
	UPROPERTY(EditAnywhere, Category = "Source")
	AActor* SourceActor;

	/** The source component from which to sample */
	TWeakObjectPtr<class UGroomComponent> SourceComponent;

	/** UObject Interface */
	virtual void PostInitProperties() override;

	/** UNiagaraDataInterface Interface */
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) override;
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { return Target == ENiagaraSimTarget::GPUComputeSim; }
	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)override;
	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	virtual int32 PerInstanceDataSize()const override { return sizeof(FNDIHairStrandsData); }
	virtual bool Equals(const UNiagaraDataInterface* Other) const override;
	virtual bool HasPreSimulateTick() const override { return true; }
	virtual bool HasTickGroupPrereqs() const override { return true; }
	virtual ETickingGroup CalculateTickGroup(const void* PerInstanceData) const override;

	/** GPU simulation  functionality */
#if WITH_EDITORONLY_DATA
	virtual void GetCommonHLSL(FString& OutHLSL) override;
	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;
	virtual bool AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const override;
#endif
	virtual void ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance) override;

	/** Update the source component */
	void ExtractSourceComponent(FNiagaraSystemInstance* SystemInstance);

	/** Check if the component is Valid */
	bool IsComponentValid() const;

	/** Extract datas and resources */
	void ExtractDatasAndResources(
		FNiagaraSystemInstance* SystemInstance, 
		FHairStrandsDatas*& OutStrandsDatas,
		FHairStrandsRestResource*& OutStrandsRestResource, 
		FHairStrandsDeformedResource*& OutStrandsDeformedResource, 
		FHairStrandsRestRootResource*& OutStrandsRestRootResource, 
		FHairStrandsDeformedRootResource*& OutStrandsDeformedRootResource,
		UGroomAsset*& OutGroomAsset,
		int32& OutGroupIndex,
		FTransform& OutLocalToWorld);

	/** Get the number of strands */
	void GetNumStrands(FVectorVMContext& Context);

	/** Get the groom asset datas  */
	void GetStrandSize(FVectorVMContext& Context);

	void GetSubSteps(FVectorVMContext& Context);

	void GetIterationCount(FVectorVMContext& Context);

	void GetGravityVector(FVectorVMContext& Context);

	void GetAirDrag(FVectorVMContext& Context);

	void GetAirVelocity(FVectorVMContext& Context);

	void GetSolveBend(FVectorVMContext& Context);

	void GetProjectBend(FVectorVMContext& Context);

	void GetBendDamping(FVectorVMContext& Context);

	void GetBendStiffness(FVectorVMContext& Context);

	void GetBendScale(FVectorVMContext& Context);

	void GetSolveStretch(FVectorVMContext& Context);

	void GetProjectStretch(FVectorVMContext& Context);

	void GetStretchDamping(FVectorVMContext& Context);

	void GetStretchStiffness(FVectorVMContext& Context);

	void GetStretchScale(FVectorVMContext& Context);

	void GetSolveCollision(FVectorVMContext& Context);

	void GetProjectCollision(FVectorVMContext& Context);

	void GetStaticFriction(FVectorVMContext& Context);

	void GetKineticFriction(FVectorVMContext& Context);

	void GetStrandsViscosity(FVectorVMContext& Context);

	void GetGridDimension(FVectorVMContext& Context);

	void GetCollisionRadius(FVectorVMContext& Context);

	void GetRadiusScale(FVectorVMContext& Context);

	void GetStrandsSmoothing(FVectorVMContext& Context);

	void GetStrandsDensity(FVectorVMContext& Context);

	void GetStrandsThickness(FVectorVMContext& Context);

	void GetThicknessScale(FVectorVMContext& Context);

	/** Get the world transform */
	void GetWorldTransform(FVectorVMContext& Context);

	/** Get the world inverse */
	void GetWorldInverse(FVectorVMContext& Context);

	/** Get the strand vertex position in world space*/
	void GetPointPosition(FVectorVMContext& Context);

	/** Get the strand node position in world space*/
	void ComputeNodePosition(FVectorVMContext& Context);

	/** Get the strand node orientation in world space*/
	void ComputeNodeOrientation(FVectorVMContext& Context);

	/** Get the strand node mass */
	void ComputeNodeMass(FVectorVMContext& Context);

	/** Get the strand node inertia */
	void ComputeNodeInertia(FVectorVMContext& Context);

	/** Compute the edge length (diff between 2 nodes positions)*/
	void ComputeEdgeLength(FVectorVMContext& Context);

	/** Compute the edge orientation (diff between 2 nodes orientations) */
	void ComputeEdgeRotation(FVectorVMContext& Context);

	/** Compute the rest local position */
	void ComputeRestPosition(FVectorVMContext& Context);

	/** Compute the rest local orientation */
	void ComputeRestOrientation(FVectorVMContext& Context);

	/** Update the root node orientation based on the current transform */
	void AttachNodePosition(FVectorVMContext& Context);

	/** Update the root node position based on the current transform */
	void AttachNodeOrientation(FVectorVMContext& Context);

	/** Report the node displacement onto the points position*/
	void UpdatePointPosition(FVectorVMContext& Context);

	/** Reset the point position to be the rest one */
	void ResetPointPosition(FVectorVMContext& Context);

	/** Add external force to the linear velocity and advect node position */
	void AdvectNodePosition(FVectorVMContext& Context);

	/** Add external torque to the angular velocity and advect node orientation*/
	void AdvectNodeOrientation(FVectorVMContext& Context);

	/** Update the node linear velocity based on the node position difference */
	void UpdateLinearVelocity(FVectorVMContext& Context);

	/** Update the node angular velocity based on the node orientation difference */
	void UpdateAngularVelocity(FVectorVMContext& Context);

	/** Get the bounding box center */
	void GetBoundingBox(FVectorVMContext& Context);

	/** Reset the bounding box extent */
	void ResetBoundingBox(FVectorVMContext& Context);

	/** Build the groom bounding box */
	void BuildBoundingBox(FVectorVMContext& Context);

	/** Setup the distance spring material */
	void SetupDistanceSpringMaterial(FVectorVMContext& Context);

	/** Solve the distance spring material */
	void SolveDistanceSpringMaterial(FVectorVMContext& Context);

	/** Project the distance spring material */
	void ProjectDistanceSpringMaterial(FVectorVMContext& Context);

	/** Setup the angular spring material */
	void SetupAngularSpringMaterial(FVectorVMContext& Context);

	/** Solve the angular spring material */
	void SolveAngularSpringMaterial(FVectorVMContext& Context);

	/** Project the angular spring material */
	void ProjectAngularSpringMaterial(FVectorVMContext& Context);

	/** Setup the stretch rod material */
	void SetupStretchRodMaterial(FVectorVMContext& Context);

	/** Solve the stretch rod material */
	void SolveStretchRodMaterial(FVectorVMContext& Context);

	/** Project the stretch rod material */
	void ProjectStretchRodMaterial(FVectorVMContext& Context);

	/** Setup the bend rod material */
	void SetupBendRodMaterial(FVectorVMContext& Context);

	/** Solve the bend rod material */
	void SolveBendRodMaterial(FVectorVMContext& Context);

	/** Project the bend rod material */
	void ProjectBendRodMaterial(FVectorVMContext& Context);

	/** Solve the static collision constraint */
	void SolveHardCollisionConstraint(FVectorVMContext& Context);

	/** Project the static collision constraint */
	void ProjectHardCollisionConstraint(FVectorVMContext& Context);

	/** Solve the soft collision constraint */
	void SolveSoftCollisionConstraint(FVectorVMContext& Context);

	/** Project the soft collision constraint */
	void ProjectSoftCollisionConstraint(FVectorVMContext& Context);

	/** Setup the soft collision constraint */
	void SetupSoftCollisionConstraint(FVectorVMContext& Context);

	/** Compute the rest direction*/
	void ComputeEdgeDirection(FVectorVMContext& Context);

	/** Update the strands material frame */
	void UpdateMaterialFrame(FVectorVMContext& Context);

	/** Compute the strands material frame */
	void ComputeMaterialFrame(FVectorVMContext& Context);

	/** Compute the air drag force */
	void ComputeAirDragForce(FVectorVMContext& Context);

	/** Get the rest position and orientation relative to the transform or to the skin cache */
	void ComputeLocalState(FVectorVMContext& Context);

	/** Attach the node position and orientation to the transform or to the skin cache */
	void AttachNodeState(FVectorVMContext& Context);

	/** Update the node position and orientation based on rbf transfer */
	void UpdateNodeState(FVectorVMContext& Context);

	/** Check if we need or not a simulation reset*/
	void NeedSimulationReset(FVectorVMContext& Context);

	/** Check if we have a global interpolation */
	void HasGlobalInterpolation(FVectorVMContext& Context);

	/** Check if we need a rest pose update */
	void NeedRestUpdate(FVectorVMContext& Context);

	/** Eval the skinned position given a rest position*/
	void EvalSkinnedPosition(FVectorVMContext& Context);

	/** Init the samples along the strands that will be used to transfer informations to the grid */
	void InitGridSamples(FVectorVMContext& Context);

	/** Get the sample state given an index */
	void GetSampleState(FVectorVMContext& Context);

	/** Name of the world transform */
	static const FString WorldTransformName;

	/** Name of the bounding box offsets*/
	static const FString BoundingBoxOffsetsName;

	/** Name of the world transform */
	static const FString WorldInverseName;

	/** Name of the world rotation */
	static const FString WorldRotationName;

	/** Name of the number of strands */
	static const FString NumStrandsName;

	/** Name of the strand size */
	static const FString StrandSizeName;

	/** Name of the points positions buffer */
	static const FString DeformedPositionBufferName;

	/** Name of the curves offsets buffer */
	static const FString CurvesOffsetsBufferName;

	/** Name of bounding box buffer */
	static const FString BoundingBoxBufferName;

	/** Name of the nodes positions buffer */
	static const FString RestPositionBufferName;

	/** Param to check if the roots have been attached to the skin */
	static const FString InterpolationModeName;

	/** Param to check if we need to update the rest pose */
	static const FString RestUpdateName;

	/** boolean to check if we need to rest the simulation*/
	static const FString ResetSimulationName;

	/** Rest center of all the roots */
	static const FString RestRootOffsetName;

	/** Rest position of the triangle vertex A */
	static const FString RestTrianglePositionAName;

	/** Rest position of the triangle vertex B */
	static const FString RestTrianglePositionBName;

	/** Rest position of the triangle vertex C */
	static const FString RestTrianglePositionCName;

	/** Deformed center of all the roots */
	static const FString DeformedRootOffsetName;

	/** Deformed position of the triangle vertex A */
	static const FString DeformedTrianglePositionAName;

	/** Deformed position of the triangle vertex A */
	static const FString DeformedTrianglePositionBName;

	/** Deformed position of the triangle vertex A */
	static const FString DeformedTrianglePositionCName;

	/** Root barycentric coordinates */
	static const FString RootBarycentricCoordinatesName;

	/** Rest center of all the position */
	static const FString RestPositionOffsetName;

	/** Deformed center of all the position */
	static const FString DeformedPositionOffsetName;

	/** Number of samples for rbf interpolation */
	static const FString SampleCountName;

	/** Rbf sample weights */
	static const FString MeshSampleWeightsName;

	/** Rbf Sample rest positions */
	static const FString RestSamplePositionsName;

	/** Params scale buffer */
	static const FString ParamsScaleBufferName;

protected:
	/** Copy one niagara DI to this */
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
};

/** Proxy to send data to gpu */
struct FNDIHairStrandsProxy : public FNiagaraDataInterfaceProxy
{
	/** Get the size of the data that will be passed to render*/
	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override { return sizeof(FNDIHairStrandsData); }

	/** Get the data that will be passed to render*/
	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance) override;

	/** Initialize the Proxy data strands buffer */
	void InitializePerInstanceData(const FNiagaraSystemInstanceID& SystemInstance);

	/** Destroy the proxy data if necessary */
	void DestroyPerInstanceData(NiagaraEmitterInstanceBatcher* Batcher, const FNiagaraSystemInstanceID& SystemInstance);

	/** Launch all pre stage functions */
	virtual void PreStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceStageArgs& Context) override;

	/** List of proxy data for each system instances*/
	TMap<FNiagaraSystemInstanceID, FNDIHairStrandsData> SystemInstancesToProxyData;
};

