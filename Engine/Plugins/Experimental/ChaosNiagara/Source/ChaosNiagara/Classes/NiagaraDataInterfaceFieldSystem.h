// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraDataInterface.h"
#include "NiagaraCommon.h"
#include "VectorVM.h"
#include "Engine/Blueprint.h"
#include "Field/FieldSystem.h"
#include "Field/FieldSystemAsset.h"
#include "Field/FieldSystemComponent.h"
#include "NiagaraDataInterfaceFieldSystem.generated.h"

/** Arrays in which the cpu datas will be str */
struct FNDIFieldSystemArrays
{
	static const uint32 NumFields = FFieldNodeBase::ESerializationType::FieldNode_FReturnResultsTerminal + 1;
	static const uint32 NumCommands = EFieldPhysicsType::Field_PhysicsType_Max;

	TStaticArray<int32, NumCommands + 1>	FieldCommandsNodes;
	TArray<int32>						FieldNodesOffsets;
	TArray<float>						FieldNodesParams;

	TArray<float>		ArrayFieldDatas;
	TArray<FVector>		VectorFieldDatas;
	TArray<float>		ScalarFieldDatas;
	TArray<int32>		IntegerFieldDatas;

	FIntVector	FieldDimensions;
	FVector		MinBounds;
	FVector		MaxBounds;
};

/** Render buffers that will be used in hlsl functions */
struct FNDIFieldSystemBuffer : public FRenderResource
{
	/** Check if all the assets are valid */
	bool IsValid() const;

	/** Set the assets that will be used to affect the buffer */
	void Initialize(const TArray<TWeakObjectPtr<class UFieldSystem>>& FieldSystems, const TArray<TWeakObjectPtr<class UFieldSystemComponent>>& FieldComponents,
		const FIntVector& FieldDimensions, const FVector& MinBounds, const FVector& MaxBounds);

	/** Update the buffers */
	void Update();

	/** Init the buffer */
	virtual void InitRHI() override;

	/** Release the buffer */
	virtual void ReleaseRHI() override;

	/** Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FNDIFieldSystemBuffer"); }

	/** Field nodes params buffer */
	FRWBuffer FieldNodesParamsBuffer;

	/** Field nodes offsets buffer */
	FRWBuffer FieldNodesOffsetsBuffer;

	/** Field commands nodes buffer */
	FRWBuffer FieldCommandsNodesBuffer;

	/** Vector Field Texture */
	FTextureRWBuffer3D VectorFieldTexture;

	/** Scalar Field Texture */
	FTextureRWBuffer3D ScalarFieldTexture;

	/** Integer Field Texture */
	FTextureRWBuffer3D IntegerFieldTexture;

	/** The field systems to be used*/
	TArray<TWeakObjectPtr<class UFieldSystem>> FieldSystems;

	/** The field component from which the system will be constructed*/
	TArray<TWeakObjectPtr<class UFieldSystemComponent>> FieldComponents;

	/** Physics Asset arrays */
	TUniquePtr<FNDIFieldSystemArrays> AssetArrays;
};

/** Data stored per physics asset instance*/
struct FNDIFieldSystemData
{
	/** Initialize the buffers */
	bool Init(class UNiagaraDataInterfaceFieldSystem* Interface, FNiagaraSystemInstance* SystemInstance);

	/** Release the buffers */
	void Release();

	/** Physics asset Gpu buffer */
	FNDIFieldSystemBuffer* FieldSystemBuffer;
};

/** Data Interface for the strand base */
UCLASS(EditInlineNew, Category = "Chaos", meta = (DisplayName = "Field System"))
class CHAOSNIAGARA_API UNiagaraDataInterfaceFieldSystem : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()

public:

	DECLARE_NIAGARA_DI_PARAMETER();

	/** Blue print. */
	UPROPERTY(EditAnywhere, Category = "Source")
	UBlueprint* BlueprintSource;

	/** The source actor from which to sample */
	UPROPERTY(EditAnywhere, Category = "Source")
	AActor* SourceActor;

	/** The source actor from which to sample */
	UPROPERTY(EditAnywhere, Category = "Field")
		FIntVector FieldDimensions;

	/** The source actor from which to sample */
	UPROPERTY(EditAnywhere, Category = "Field")
	FVector MinBounds;

	/** The source actor from which to sample */
	UPROPERTY(EditAnywhere, Category = "Field")
	FVector MaxBounds;

	/** The source component from which to sample */
	TArray<TWeakObjectPtr<class UFieldSystemComponent>> SourceComponents;

	/** The source asset from which to sample */
	TArray<TWeakObjectPtr<class UFieldSystem>> FieldSystems;

	/** UObject Interface */
	virtual void PostInitProperties() override;

	/** UNiagaraDataInterface Interface */
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction& OutFunc) override;
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { return Target == ENiagaraSimTarget::GPUComputeSim; }
	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)override;
	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	virtual int32 PerInstanceDataSize()const override { return sizeof(FNDIFieldSystemData); }
	virtual bool Equals(const UNiagaraDataInterface* Other) const override;

	/** GPU simulation  functionality */
	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;
	virtual void ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance) override;
	virtual void GetCommonHLSL(FString& OutHLSL) override;

	/** Extract the source component */
	void ExtractSourceComponent(FNiagaraSystemInstance* SystemInstance);

	/** Sample the linear force */
	void SampleLinearForce(FVectorVMContext& Context);

	/** Sample the linear velocity */
	void SampleLinearVelocity(FVectorVMContext& Context);

	/** Sample the field linear force */
	void SampleAngularVelocity(FVectorVMContext& Context);

	/** Sample the field angular torque */
	void SampleAngularTorque(FVectorVMContext& Context);

	/** Sample the field dynamic state */
	void SampleDynamicState(FVectorVMContext& Context);

	/** Sample the field dynamic constraint */
	void SampleDynamicConstraint(FVectorVMContext& Context);

	/** Sample the field collision group */
	void SampleCollisionGroup(FVectorVMContext& Context);

	/** Sample the field activate disabled */
	void SampleActivateDisabled(FVectorVMContext& Context);

	/** Sample the field kill */
	void SampleFieldKill(FVectorVMContext& Context);

	/** Sample the field external cluster strain */
	void SampleExternalClusterStrain(FVectorVMContext& Context);

	/** Sample the field internal cluster strain */
	void SampleInternalClusterStrain(FVectorVMContext& Context);

	/** Sample the field distance threshold */
	void SampleDisableThreshold(FVectorVMContext& Context);

	/** Sample the field sleeping threshold */
	void SampleSleepingThreshold(FVectorVMContext& Context);

	/** Sample the field static position */
	void SamplePositionStatic(FVectorVMContext& Context);

	/** Sample the field animated position */
	void SamplePositionAnimated(FVectorVMContext& Context);

	/** Sample the field target position */
	void SamplePositionTarget(FVectorVMContext& Context);

	/** Get the field dimensions */
	void GetFieldDimensions(FVectorVMContext& Context);

	/** Get the field bounds */
	void GetFieldBounds(FVectorVMContext& Context);

	/** Name of field commands nodes buffer */
	static const FString FieldCommandsNodesBufferName;

	/** Name of field nodes params buffer */
	static const FString FieldNodesParamsBufferName;

	/** Name of field nodes params buffer */
	static const FString FieldNodesOffsetsBufferName;

	/** Name of the vector field texture */
	static const FString VectorFieldTextureName;

	/** Name of the vector field sampler */
	static const FString VectorFieldSamplerName;

	/** Name of the scalar field texture*/
	static const FString ScalarFieldTextureName;

	/** Name of the scalar field sampler */
	static const FString ScalarFieldSamplerName;

	/** Name of the integer field texture*/
	static const FString IntegerFieldTextureName;

	/** Name of the integer field sampler */
	static const FString IntegerFieldSamplerName;

	/** Name of the field dimension property */
	static const FString FieldDimensionsName;

	/** Name of the min bounds property */
	static const FString MinBoundsName;

	/** Name of the max bounds property */
	static const FString MaxBoundsName;

protected:
	/** Copy one niagara DI to this */
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
};

/** Proxy to send data to gpu */
struct FNDIFieldSystemProxy : public FNiagaraDataInterfaceProxy
{
	/** Get the size of the data that will be passed to render*/
	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override { return sizeof(FNDIFieldSystemData); }

	/** Get the data that will be passed to render*/
	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance) override;

	/** Initialize the Proxy data Chaos buffer */
	void InitializePerInstanceData(const FNiagaraSystemInstanceID& SystemInstance);

	/** Destroy the proxy data if necessary */
	void DestroyPerInstanceData(NiagaraEmitterInstanceBatcher* Batcher, const FNiagaraSystemInstanceID& SystemInstance);

	/** List of proxy data for each system instances*/
	TMap<FNiagaraSystemInstanceID, FNDIFieldSystemData> SystemInstancesToProxyData;
};

