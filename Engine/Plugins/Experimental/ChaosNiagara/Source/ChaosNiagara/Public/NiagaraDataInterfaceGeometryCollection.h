// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraDataInterface.h"
#include "NiagaraCommon.h"
#include "VectorVM.h"
#include "GeometryCollection/GeometryCollectionActor.h"

#include "NiagaraDataInterfaceGeometryCollection.generated.h"


/** Arrays in which the cpu datas will be str */
struct FNDIGeometryCollectionArrays
{
	TArray<FVector4f> WorldTransformBuffer;
	TArray<FVector4f> PrevWorldTransformBuffer;
	TArray<FVector4f> WorldInverseTransformBuffer;
	TArray<FVector4f> PrevWorldInverseTransformBuffer;
	TArray<FVector4f> BoundsBuffer;

	FNDIGeometryCollectionArrays()
	{
		Resize(100);
	}

	FNDIGeometryCollectionArrays(uint32 Num)
	{
		Resize(Num);
	}

	void CopyFrom(const FNDIGeometryCollectionArrays* Other)
	{
		Resize(Other->NumPieces);
		
		WorldTransformBuffer = Other->WorldTransformBuffer;
		PrevWorldTransformBuffer = Other->PrevWorldTransformBuffer;
		WorldInverseTransformBuffer = Other->WorldInverseTransformBuffer;
		PrevWorldInverseTransformBuffer = Other->PrevWorldInverseTransformBuffer;
		BoundsBuffer = Other->BoundsBuffer;
	}

	void Resize(uint32 Num)
	{		
		NumPieces = Num;

		WorldTransformBuffer.Init(FVector4f(0, 0, 0, 0), 3 * NumPieces);
		PrevWorldTransformBuffer.Init(FVector4f(0, 0, 0, 0), 3 * NumPieces);
		WorldInverseTransformBuffer.Init(FVector4f(0, 0, 0, 0), 3 * NumPieces);
		PrevWorldInverseTransformBuffer.Init(FVector4f(0, 0, 0, 0), 3 * NumPieces);
		BoundsBuffer.Init(FVector4f(0, 0, 0, 0), NumPieces);
	}

	uint32 NumPieces = 100;
};

/** Render buffers that will be used in hlsl functions */
struct FNDIGeometryCollectionBuffer : public FRenderResource
{
	/** Init the buffer */
	virtual void InitRHI() override;

	/** Release the buffer */
	virtual void ReleaseRHI() override;

	/** Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FNDIGeometryCollectionBuffer"); }

	/** World transform buffer */
	FRWBuffer WorldTransformBuffer;

	/** Inverse transform buffer*/
	FRWBuffer PrevWorldTransformBuffer;

	/** World transform buffer */
	FRWBuffer WorldInverseTransformBuffer;

	/** Inverse transform buffer*/
	FRWBuffer PrevWorldInverseTransformBuffer;

	/** Element extent buffer */
	FRWBuffer BoundsBuffer;

	/** number of transforms */
	uint32 NumPieces;

	void SetNumPieces(uint32 Num)
	{
		NumPieces = Num;
	}
};

/** Data stored per physics asset instance*/
struct FNDIGeometryCollectionData
{
	/** Initialize the cpu datas */
	void Init(class UNiagaraDataInterfaceGeometryCollection* Interface, FNiagaraSystemInstance* SystemInstance);

	/** Update the gpu datas */
	void Update(class UNiagaraDataInterfaceGeometryCollection* Interface, FNiagaraSystemInstance* SystemInstance);

	/** Release the buffers */
	void Release();

	ETickingGroup ComputeTickingGroup();

	/** The instance ticking group */
	ETickingGroup TickingGroup;

	/** Geometry Collection Bounds */
	FVector3f BoundsOrigin;

	FVector3f BoundsExtent;

	/** Physics asset Gpu buffer */
	FNDIGeometryCollectionBuffer* AssetBuffer = nullptr;

	/** Physics asset Cpu arrays */
	FNDIGeometryCollectionArrays *AssetArrays = nullptr;	
};

/** Data Interface for the Collisions */
UCLASS(EditInlineNew, Category = "Collision", meta = (DisplayName = "Geometry Collection"))
class UNiagaraDataInterfaceGeometryCollection : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()

public:

	DECLARE_NIAGARA_DI_PARAMETER();

	UPROPERTY(EditAnywhere, Category = "Geometry Collection")
	TObjectPtr<AGeometryCollectionActor> GeometryCollectionActor;

	/** UObject Interface */
	virtual void PostInitProperties() override;

	/** UNiagaraDataInterface Interface */
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction& OutFunc) override;
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { return Target == ENiagaraSimTarget::GPUComputeSim; }
	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)override;
	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	virtual int32 PerInstanceDataSize()const override { return sizeof(FNDIGeometryCollectionData); }
	virtual bool Equals(const UNiagaraDataInterface* Other) const override;
	virtual bool HasPreSimulateTick() const override { return true; }
	virtual bool HasTickGroupPrereqs() const override { return true; }
	virtual ETickingGroup CalculateTickGroup(const void* PerInstanceData) const override;


	/** GPU simulation  functionality */
#if WITH_EDITORONLY_DATA
	virtual void GetCommonHLSL(FString& OutHLSL) override;
	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;

	virtual void ValidateFunction(const FNiagaraFunctionSignature& Function, TArray<FText>& OutValidationErrors) override;
#endif
	virtual void ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance) override;

	static const FString BoundsMinName;
	static const FString BoundsMaxName;
	static const FString NumPiecesName;
	static const FString WorldTransformBufferName;
	static const FString PrevWorldTransformBufferName;
	static const FString WorldInverseTransformBufferName;
	static const FString PrevWorldInverseTransformBufferName;
	static const FString BoundsBufferName;

protected:
	/** Copy one niagara DI to this */
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
};

/** Proxy to send data to gpu */
struct FNDIGeometryCollectionProxy : public FNiagaraDataInterfaceProxy
{
	/** Get the size of the data that will be passed to render*/
	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override { return sizeof(FNDIGeometryCollectionData); }

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
	TMap<FNiagaraSystemInstanceID, FNDIGeometryCollectionData> SystemInstancesToProxyData;
};