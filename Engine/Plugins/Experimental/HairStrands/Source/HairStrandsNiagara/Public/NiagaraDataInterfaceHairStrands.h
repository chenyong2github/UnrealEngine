// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceRW.h"
#include "NiagaraCommon.h"
#include "VectorVM.h"
#include "GroomAsset.h"
#include "GroomActor.h"
#include "NiagaraDataInterfaceHairStrands.generated.h"

/** Size of each strands*/
UENUM(BlueprintType)
enum class EHairStrandsSize : uint8
{
	None = 0 UMETA(Hidden),
	Size2 = 0x02 UMETA(DisplatName = "2"),
	Size4 = 0x04 UMETA(DisplatName = "4"),
	Size8 = 0x08 UMETA(DisplatName = "8"),
	Size16 = 0x10 UMETA(DisplatName = "16"),
	Size32 = 0x20 UMETA(DisplatName = "32")
};

/** Render buffers that will be used in hlsl functions */
struct FNDIHairStrandsBuffer : public FRenderResource
{
	/** Set the asset that will be used to affect the buffer */
	void SetHairAsset(const FHairStrandsDatas*  HairStrandsDatas, const FHairStrandsRestResource*  HairStrandsRestResource, 
		const FHairStrandsDeformedResource*  HairStrandsDeformedResource, const FHairStrandsRootResource* HairStrandsRootResource );

	/** Init the buffer */
	virtual void InitRHI() override;

	/** Release the buffer */
	virtual void ReleaseRHI() override;

	/** Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FNDIHairStrandsBuffer"); }

	/** Clear the bounding box buffer */
	void ClearBuffer(FRHICommandList& RHICmdList);

	/** Strand curves point offset buffer */
	FRWBuffer CurvesOffsetsBuffer;

	/** Deformed position buffer in case no ressource are there */
	FRWBuffer DeformedPositionBuffer;

	/**Rest triangle position of vertex A*/
	FRWBuffer RestTrianglePositionABuffer;

	/**Rest triangle position of vertex B*/
	FRWBuffer RestTrianglePositionBBuffer;

	/**Rest triangle position of vertex C*/
	FRWBuffer RestTrianglePositionCBuffer;

	/**Deformed triangle position of vertex A*/
	FRWBuffer DeformedTrianglePositionABuffer;

	/**Deformed triangle position of vertex B*/
	FRWBuffer DeformedTrianglePositionBBuffer;

	/**Deformed triangle position of vertex C*/
	FRWBuffer DeformedTrianglePositionCBuffer;

	/**Root barycentric coordinates */
	FRWBuffer RootBarycentricCoordinatesBuffer;

	/** Bounding Box Buffer*/
	FRWBuffer BoundingBoxBuffer;

	/** Node Bound Buffer*/
	FRWBuffer NodeBoundBuffer;

	/** The strand asset datas from which to sample */
	const FHairStrandsDatas* SourceDatas;

	/** The strand asset resource from which to sample */
	const FHairStrandsRestResource* SourceRestResources;

	/** The strand deformed resource to write into */
	const FHairStrandsDeformedResource* SourceDeformedResources;

	/** The strand root resource to write into */
	const FHairStrandsRootResource* SourceRootResources;
};

/** Data stored per strand base instance*/
struct FNDIHairStrandsData
{
	/** Cached World transform. */
	FTransform WorldTransform;

	/** Number of strands*/
	int32 NumStrands;

	/** Strand size */
	int32 StrandSize;

	/** Bounding box center */
	FVector BoxCenter;

	/** Bounding box extent */
	FVector BoxExtent;

	/** Tick Count*/
	int32 TickCount;

	/** Force reset simulation */
	bool ForceReset;

	/** Reset Tick*/
	int32 ResetTick;

	/** Strands Gpu buffer */
	FNDIHairStrandsBuffer* HairStrandsBuffer;
};

/** Data Interface for the strand base */
UCLASS(EditInlineNew, Category = "Strands", meta = (DisplayName = "Hair Strands"))
class HAIRSTRANDSNIAGARA_API UNiagaraDataInterfaceHairStrands : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()

public:

	/** Size of each strand. */
	UPROPERTY(EditAnywhere, Category = "Spawn")
	EHairStrandsSize StrandSize;

	/** Hair Strands Asset used to sample from when not overridden by a source actor from the scene. Also useful for previewing in the editor. */
	UPROPERTY(EditAnywhere, Category = "Source")
	UGroomAsset* DefaultSource;

	/** The source actor from which to sample */
	UPROPERTY(EditAnywhere, Category = "Source")
	AActor* SourceActor;

	/** The source component from which to sample */
	TWeakObjectPtr<class UGroomComponent> SourceComponent;

	/** Group Index to be used */
	UPROPERTY(EditAnywhere, Category = "Source")
	int32 GroupIndex;

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

	/** GPU simulation  functionality */
	virtual bool GetFunctionHLSL(const FName&  DefinitionFunctionName, FString InstanceFunctionName, FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual void GetParameterDefinitionHLSL(FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual FNiagaraDataInterfaceParametersCS* ConstructComputeParameters() const override;
	virtual void ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance) override;
	virtual void GetCommonHLSL(FString& OutHLSL) override;

	/** Update the source component */
	void UpdateSourceComponent(FNiagaraSystemInstance* SystemInstance);

	/** Check if the component is Valid */
	bool IsComponentValid() const;

	/** Extract datas and resources */
	void ExtractDatasAndResources(FNiagaraSystemInstance* SystemInstance, FHairStrandsDatas*& OutStrandsDatas,
		FHairStrandsRestResource*& OutStrandsRestResource, FHairStrandsDeformedResource*& OutStrandsDeformedResource, FHairStrandsRootResource*& OutStrandsRootResource);

	/** Get the number of strands */
	void GetNumStrands(FVectorVMContext& Context);

	/** Get the strand size  */
	void GetStrandSize(FVectorVMContext& Context);

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
	void GetBoxCenter(FVectorVMContext& Context);

	/** Get the bounding box extent */
	void GetBoxExtent(FVectorVMContext& Context);

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
	void SolveStaticCollisionConstraint(FVectorVMContext& Context);

	/** Project the static collision constraint */
	void ProjectStaticCollisionConstraint(FVectorVMContext& Context);

	/** Compute the rest direction*/
	void ComputeRestDirection(FVectorVMContext& Context);

	/** Update the node orientation to match the bishop frame*/
	void UpdateNodeOrientation(FVectorVMContext& Context);

	/** Compute the air drag force */
	void ComputeAirDragForce(FVectorVMContext& Context);

	/** Get the rest position and orientation relative to the transform or to the skin cache */
	void ComputeLocalState(FVectorVMContext& Context);

	/** Attach the node position and orientation to the transform or to the skin cache */
	void AttachNodeState(FVectorVMContext& Context);

	/** Check if we need or not a simulation reset*/
	void NeedSimulationReset(FVectorVMContext& Context);

	/** Name of the world transform */
	static const FString WorldTransformName;

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

	/** Name of node bound buffer */
	static const FString NodeBoundBufferName;

	/** Name of the nodes positions buffer */
	static const FString RestPositionBufferName;

	/** Name of the box center  */
	static const FString BoxCenterName;

	/** Name of the box extent  */
	static const FString BoxExtentName;

	/** Param to check if the roots have been attached to the skin */
	static const FString HasRootAttachedName;

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

protected:
	/** Copy one niagara DI to this */
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
};

/** Proxy to send data to gpu */
struct FNDIHairStrandsProxy : public FNiagaraDataInterfaceProxy
{
	/** Destroy internal data */
	virtual void DeferredDestroy() override;

	/** Get the size of the data that will be passed to render*/
	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override { return sizeof(FNDIHairStrandsData); }

	/** Get the data that will be passed to render*/
	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance) override;

	/** Initialize the Proxy data strands buffer */
	void InitializePerInstanceData(const FNiagaraSystemInstanceID& SystemInstance, FNDIHairStrandsBuffer* StrandsBuffer, const uint32 NumStrands, const uint8 StrandSize, const FVector& BoxCenter, const FVector& BoxExtent, const FTransform& WorldTransform);

	/** Destroy the proxy data if necessary */
	void DestroyPerInstanceData(NiagaraEmitterInstanceBatcher* Batcher, const FNiagaraSystemInstanceID& SystemInstance);

	/** Launch all pre stage functions */
	virtual void PreStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) override;

	/** Launch all post stage functions */
	virtual void PostStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) override;

	/** Reset the buffers  */
	virtual void ResetData(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) override;

	/** List of proxy data for each system instances*/
	TMap<FNiagaraSystemInstanceID, FNDIHairStrandsData> SystemInstancesToProxyData;

	/** List of proxy data to destroy later */
	TSet<FNiagaraSystemInstanceID> DeferredDestroyList;
};

