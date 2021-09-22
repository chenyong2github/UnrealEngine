// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraDataInterface.h"
#include "NiagaraCommon.h"
#include "VectorVM.h"
#include "NiagaraDataInterfaceRigidMeshCollisionQuery.generated.h"

#define RIGID_MESH_COLLISION_QUERY_MAX_PRIMITIVES 100
#define RIGID_MESH_COLLISION_QUERY_MAX_TRANSFORMS RIGID_MESH_COLLISION_QUERY_MAX_PRIMITIVES * 2

// Forward declaration
class AStaticMeshActor;

/** Element offsets in the array list */
struct FElementOffset
{
	FElementOffset(const uint32 InBoxOffset, const uint32 InSphereOffset, const uint32 InCapsuleOffset, const uint32 InNumElements) :
		BoxOffset(InBoxOffset), SphereOffset(InSphereOffset), CapsuleOffset(InCapsuleOffset), NumElements(InNumElements)
	{}

	FElementOffset() :
		BoxOffset(0), SphereOffset(0), CapsuleOffset(0), NumElements(0)
	{}
	uint32 BoxOffset;
	uint32 SphereOffset;
	uint32 CapsuleOffset;
	uint32 NumElements;
};

/** Arrays in which the cpu datas will be str */
struct FNDIRigidMeshCollisionArrays
{
	FElementOffset ElementOffsets;
	TStaticArray<FVector4, 2 * RIGID_MESH_COLLISION_QUERY_MAX_TRANSFORMS> WorldTransform;
	TStaticArray<FVector4, 2 * RIGID_MESH_COLLISION_QUERY_MAX_TRANSFORMS> InverseTransform;
	TStaticArray<FVector4, RIGID_MESH_COLLISION_QUERY_MAX_TRANSFORMS> CurrentTransform;
	TStaticArray<FVector4, RIGID_MESH_COLLISION_QUERY_MAX_TRANSFORMS> CurrentInverse;
	TStaticArray<FVector4, RIGID_MESH_COLLISION_QUERY_MAX_TRANSFORMS> PreviousTransform;
	TStaticArray<FVector4, RIGID_MESH_COLLISION_QUERY_MAX_TRANSFORMS> PreviousInverse;
	TStaticArray<FVector4, RIGID_MESH_COLLISION_QUERY_MAX_PRIMITIVES> ElementExtent;
	TStaticArray<uint32, RIGID_MESH_COLLISION_QUERY_MAX_PRIMITIVES> PhysicsType;
};

/** Render buffers that will be used in hlsl functions */
struct FNDIRigidMeshCollisionBuffer : public FRenderResource
{
	/** Init the buffer */
	virtual void InitRHI() override;

	/** Release the buffer */
	virtual void ReleaseRHI() override;

	/** Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FNDIRigidMeshCollisionBuffer"); }

	/** World transform buffer */
	FRWBuffer WorldTransformBuffer;

	/** Inverse transform buffer*/
	FRWBuffer InverseTransformBuffer;

	/** Element extent buffer */
	FRWBuffer ElementExtentBuffer;

	/** Physics type buffer */
	FRWBuffer PhysicsTypeBuffer;
};

/** Data stored per physics asset instance*/
struct FNDIRigidMeshCollisionData
{
	/** Initialize the cpu datas */
	void Init(class UNiagaraDataInterfaceRigidMeshCollisionQuery* Interface, FNiagaraSystemInstance* SystemInstance);

	/** Update the gpu datas */
	void Update(class UNiagaraDataInterfaceRigidMeshCollisionQuery* Interface, FNiagaraSystemInstance* SystemInstance);

	/** Release the buffers */
	void Release();

	ETickingGroup ComputeTickingGroup();

	/** The instance ticking group */
	ETickingGroup TickingGroup;

	/** Physics asset Gpu buffer */
	FNDIRigidMeshCollisionBuffer* AssetBuffer;

	/** Physics asset Cpu arrays */
	FNDIRigidMeshCollisionArrays AssetArrays;  

	/** Static Mesh Components **/
	TArray<AStaticMeshActor*> StaticMeshActors;
};

/** Data Interface for the Collisions */
UCLASS(EditInlineNew, Category = "Collision", meta = (DisplayName = "Rigid Mesh Collision Query"))
class UNiagaraDataInterfaceRigidMeshCollisionQuery : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()

public:

	DECLARE_NIAGARA_DI_PARAMETER();

	UPROPERTY(EditAnywhere, Category = "Static Mesh")
	bool StaticMesh = true;

	UPROPERTY(EditAnywhere, Category = "Static Mesh", meta = (EditCondition = "UseStaticMeshes"))
	FString Tag = TEXT("");

	UPROPERTY(EditAnywhere, Category = "Static Mesh", meta = (EditCondition = "UseStaticMeshes"))
	bool OnlyUseMoveable = true;

	/** UObject Interface */
	virtual void PostInitProperties() override;

	/** UNiagaraDataInterface Interface */
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction& OutFunc) override;
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { return Target == ENiagaraSimTarget::GPUComputeSim; }
	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)override;
	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	virtual int32 PerInstanceDataSize()const override { return sizeof(FNDIRigidMeshCollisionData); }
	virtual bool Equals(const UNiagaraDataInterface* Other) const override;
	virtual bool HasPreSimulateTick() const override { return true; }
	virtual bool HasTickGroupPrereqs() const override { return true; }
	virtual ETickingGroup CalculateTickGroup(const void* PerInstanceData) const override;

	/** GPU simulation  functionality */
#if WITH_EDITORONLY_DATA
	virtual void GetCommonHLSL(FString& OutHLSL) override;
	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;
#endif
	virtual void ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance) override;

	/** Name of element offsets */
	static const FString ElementOffsetsName;

	/** Name of the world transform buffer */
	static const FString WorldTransformBufferName;

	/** Name of the inverse transform buffer */
	static const FString InverseTransformBufferName;

	/** Name of the element extent buffer */
	static const FString ElementExtentBufferName;

	/** Name of the physics type buffer */
	static const FString PhysicsTypeBufferName;

protected:
	/** Copy one niagara DI to this */
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
};

/** Proxy to send data to gpu */
struct FNDIRigidMeshCollisionProxy : public FNiagaraDataInterfaceProxy
{
	/** Get the size of the data that will be passed to render*/
	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override { return sizeof(FNDIRigidMeshCollisionData); }

	/** Get the data that will be passed to render*/
	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance) override;

	/** Initialize the Proxy data buffer */
	void InitializePerInstanceData(const FNiagaraSystemInstanceID& SystemInstance);

	/** Destroy the proxy data if necessary */
	void DestroyPerInstanceData(const FNiagaraSystemInstanceID& SystemInstance);

	/** Launch all pre stage functions */
	virtual void PreStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceStageArgs& Context) override;

	/** Reset the buffers  */
	virtual void ResetData(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceArgs& Context) override;

	/** List of proxy data for each system instances*/
	TMap<FNiagaraSystemInstanceID, FNDIRigidMeshCollisionData> SystemInstancesToProxyData;
};

