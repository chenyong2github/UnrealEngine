// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraDataInterface.h"
#include "NiagaraCommon.h"
#include "VectorVM.h"
#include "NiagaraDataInterfaceRigidMeshCollisionQuery.generated.h"

// Forward declaration
class AActor;

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
	TArray<FVector4f> WorldTransform;
	TArray<FVector4f> InverseTransform;
	TArray<FVector4f> CurrentTransform;
	TArray<FVector4f> CurrentInverse;
	TArray<FVector4f> PreviousTransform;
	TArray<FVector4f> PreviousInverse;
	TArray<FVector4f> ElementExtent;
	TArray<uint32> PhysicsType;
	TArray<uint32> DFIndex;
	TArray<FPrimitiveSceneProxy*> SourceSceneProxy;

	FNDIRigidMeshCollisionArrays()
	{
		Resize(100);
	}

	FNDIRigidMeshCollisionArrays(uint32 Num)
	{
		Resize(Num);
	}

	void CopyFrom(const FNDIRigidMeshCollisionArrays* Other)
	{
		Resize(Other->MaxPrimitives);

		ElementOffsets = Other->ElementOffsets;
		WorldTransform = Other->WorldTransform;
		InverseTransform = Other->InverseTransform;
		CurrentTransform = Other->CurrentTransform;
		CurrentInverse = Other->CurrentInverse;
		PreviousTransform = Other->PreviousTransform;
		PreviousInverse = Other->PreviousInverse;
		ElementExtent = Other->ElementExtent;
		PhysicsType = Other->PhysicsType;
		DFIndex = Other->DFIndex;
		SourceSceneProxy = Other->SourceSceneProxy;
	}

	void Resize(uint32 Num)
	{
		MaxPrimitives = Num;
		MaxTransforms = MaxPrimitives * 2;				

		WorldTransform.Init(FVector4f(0, 0, 0, 0), 3 * MaxTransforms);
		InverseTransform.Init(FVector4f(0, 0, 0, 0), 3 * MaxTransforms);
		CurrentTransform.Init(FVector4f(0, 0, 0, 0), 3 * MaxPrimitives);
		CurrentInverse.Init(FVector4f(0, 0, 0, 0), 3 * MaxPrimitives);
		PreviousTransform.Init(FVector4f(0, 0, 0, 0), 3 * MaxPrimitives);
		PreviousInverse.Init(FVector4f(0, 0, 0, 0), 3 * MaxPrimitives);
		ElementExtent.Init(FVector4f(0, 0, 0, 0), MaxPrimitives);
		PhysicsType.Init(0, MaxPrimitives);
		DFIndex.Init(0, MaxPrimitives);
		SourceSceneProxy.Init(nullptr, MaxPrimitives);
	}

	uint32 MaxPrimitives = 100;
	uint32 MaxTransforms = MaxPrimitives * 2;
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

	/** Distance field index buffer */
	FRWBuffer DFIndexBuffer;

	/** Max number of primitives */
	uint32 MaxNumPrimitives;

	/** Max number of transforms (prev and next needed) */
	uint32 MaxNumTransforms;

	void SetMaxNumPrimitives(uint32 Num)
	{
		MaxNumPrimitives = Num;
		MaxNumTransforms = 2 * Num;
	}
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
	FNDIRigidMeshCollisionBuffer* AssetBuffer = nullptr;

	/** Physics asset Cpu arrays */
	FNDIRigidMeshCollisionArrays *AssetArrays = nullptr;

	/** Static Mesh Components **/
	TArray<AActor*> Actors;
};

/** Data Interface used to collide against static meshes - whether it is the mesh distance field or a physics asset's collision primitive */
UCLASS(EditInlineNew, Category = "Collision", meta = (DisplayName = "Rigid Mesh Collision Query"))
class UNiagaraDataInterfaceRigidMeshCollisionQuery : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()

public:

	DECLARE_NIAGARA_DI_PARAMETER();

	UPROPERTY(EditAnywhere, Category = "Static Mesh")
	FString Tag = TEXT("");

	UPROPERTY(EditAnywhere, Category = "Static Mesh")
	bool OnlyUseMoveable = true;

	UPROPERTY(EditAnywhere, Category = "General")
	int MaxNumPrimitives = 100;

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

	virtual bool RequiresDistanceFieldData() const override { return true; }

	/** GPU simulation  functionality */
#if WITH_EDITORONLY_DATA
	virtual void GetCommonHLSL(FString& OutHLSL) override;
	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;
	virtual bool UpgradeFunctionCall(FNiagaraFunctionSignature& FunctionSignature) override;

	virtual void ValidateFunction(const FNiagaraFunctionSignature& Function, TArray<FText>& OutValidationErrors) override;
#endif
	virtual void ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance) override;

	/** Name of element offsets */
	static const FString MaxTransformsName;

	/** Name of element offsets */
	static const FString CurrentOffsetName;

	/** Name of element offsets */
	static const FString PreviousOffsetName;

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

	/** Name of the DF Index type buffer */
	static const FString DFIndexBufferName;

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