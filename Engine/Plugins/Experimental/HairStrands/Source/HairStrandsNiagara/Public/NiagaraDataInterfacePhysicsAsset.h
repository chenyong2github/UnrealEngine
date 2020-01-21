// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraDataInterface.h"
#include "NiagaraCommon.h"
#include "VectorVM.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "NiagaraDataInterfacePhysicsAsset.generated.h"

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
struct FNDIPhysicsAssetArrays 
{
	FElementOffset ElementOffsets;
	TArray<FVector4> CurrentTransform;
	TArray<FVector4> InverseTransform;
	TArray<FVector4> PreviousTransform;
	TArray<FVector4> PreviousInverse;
	TArray<FVector4> RestTransform;
	TArray<FVector4> RestInverse;
	TArray<FVector4> ElementExtent;
};

/** Render buffers that will be used in hlsl functions */
struct FNDIPhysicsAssetBuffer : public FRenderResource
{
	/** Check if all the assets are valid */
	bool IsValid() const;

	/** Set the assets that will be used to affect the buffer */
	void Initialize(const TWeakObjectPtr<class UPhysicsAsset>  PhysicsAsset, const TWeakObjectPtr<class USkeletalMeshComponent>  SkeletalMesh, const FTransform& InWorldTransform);

	/** Update the buffers */
	void Update();

	/** Init the buffer */
	virtual void InitRHI() override;

	/** Release the buffer */
	virtual void ReleaseRHI() override;

	/** Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FNDIPhysicsAssetBuffer"); }

	/** Current transform buffer */
	FRWBuffer CurrentTransformBuffer;

	/** Previous transform buffer */
	FRWBuffer PreviousTransformBuffer;

	/** Previous inverse buffer */
	FRWBuffer PreviousInverseBuffer;

	/** Inverse transform buffer*/
	FRWBuffer InverseTransformBuffer;

	/** Rest transform buffer */
	FRWBuffer RestTransformBuffer;

	/** Rest transform buffer */
	FRWBuffer RestInverseBuffer;

	/** Element extent buffer */
	FRWBuffer ElementExtentBuffer;

	/** The physics asset datas from which the buffers will be constructed */
	TWeakObjectPtr<class UPhysicsAsset> PhysicsAsset;

	/** The skeletal mesh from which the transform will be extracted*/
	TWeakObjectPtr<class USkeletalMeshComponent> SkeletalMesh;

	/** Physics Asset arrays */
	TUniquePtr<FNDIPhysicsAssetArrays> AssetArrays;

	/** World transform */
	FTransform WorldTransform;
};

/** Data stored per physics asset instance*/
struct FNDIPhysicsAssetData
{
	/** Initialize the buffers */
	bool Init(class UNiagaraDataInterfacePhysicsAsset* Interface, FNiagaraSystemInstance* SystemInstance);

	/** Release the buffers */
	void Release();

	/** Physics asset Gpu buffer */
	FNDIPhysicsAssetBuffer* PhysicsAssetBuffer;

	/** Bounding box center */
	FVector BoxOrigin;

	/** Bounding box extent */
	FVector BoxExtent;
};

/** Data Interface for the strand base */
UCLASS(EditInlineNew, Category = "Strands", meta = (DisplayName = "Physics Asset"))
class HAIRSTRANDSNIAGARA_API UNiagaraDataInterfacePhysicsAsset : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()

public:

	/** Skeletal Mesh from which the Physics Asset will be found. */
	UPROPERTY(EditAnywhere, Category = "Source")
	UPhysicsAsset* DefaultSource;

	/** The source actor from which to sample */
	UPROPERTY(EditAnywhere, Category = "Source")
	AActor* SourceActor;

	/** The source component from which to sample */
	TWeakObjectPtr<class USkeletalMeshComponent> SourceComponent;

	/** The source asset from which to sample */
	TWeakObjectPtr<class UPhysicsAsset> PhysicsAsset;

	/** UObject Interface */
	virtual void PostInitProperties() override;

	/** UNiagaraDataInterface Interface */
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) override;
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { return Target == ENiagaraSimTarget::GPUComputeSim; }
	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)override;
	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	virtual int32 PerInstanceDataSize()const override { return sizeof(FNDIPhysicsAssetData); }
	virtual bool Equals(const UNiagaraDataInterface* Other) const override;

	/** GPU simulation  functionality */
	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;
	virtual FNiagaraDataInterfaceParametersCS* ConstructComputeParameters() const override;
	virtual void ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance) override;
	virtual void GetCommonHLSL(FString& OutHLSL) override;

	/** Extract the source component */
	void ExtractSourceComponent(FNiagaraSystemInstance* SystemInstance);

	/** Get the number of boxes*/
	void GetNumBoxes(FVectorVMContext& Context);

	/** Get the number of spheres  */
	void GetNumSpheres(FVectorVMContext& Context);

	/** Get the number of capsules */
	void GetNumCapsules(FVectorVMContext& Context);

	/** Get the closest point */
	void GetClosestPoint(FVectorVMContext& Context);

	/** Get the closest texture point */
	void GetTexturePoint(FVectorVMContext& Context);

	/** Get the projection point */
	void GetProjectionPoint(FVectorVMContext& Context);

	/** Name of element offsets */
	static const FString ElementOffsetsName;

	/** Name of the current transform buffer */
	static const FString CurrentTransformBufferName;

	/** Name of the previous transform buffer */
	static const FString PreviousTransformBufferName;

	/** Name of the previous inverse buffer */
	static const FString PreviousInverseBufferName;

	/** Name of the inverse transform buffer */
	static const FString InverseTransformBufferName;

	/** Name of the rest transform buffer */
	static const FString RestTransformBufferName;

	/** Name of the rest inverse transform buffer */
	static const FString RestInverseBufferName;

	/** Name of the element extent buffer */
	static const FString ElementExtentBufferName;

	/** Init Box Origin */
	static const FString BoxOriginName;

	/** Init Box extent */
	static const FString BoxExtentName;

protected:
	/** Copy one niagara DI to this */
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
};

/** Proxy to send data to gpu */
struct FNDIPhysicsAssetProxy : public FNiagaraDataInterfaceProxy
{
	/** Destroy internal data */
	virtual void DeferredDestroy() override;

	/** Get the size of the data that will be passed to render*/
	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override { return sizeof(FNDIPhysicsAssetData); }

	/** Get the data that will be passed to render*/
	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance) override;

	/** Initialize the Proxy data strands buffer */
	void InitializePerInstanceData(const FNiagaraSystemInstanceID& SystemInstance);

	/** Destroy the proxy data if necessary */
	void DestroyPerInstanceData(NiagaraEmitterInstanceBatcher* Batcher, const FNiagaraSystemInstanceID& SystemInstance);

	/** List of proxy data for each system instances*/
	TMap<FNiagaraSystemInstanceID, FNDIPhysicsAssetData> SystemInstancesToProxyData;

	/** List of proxy data to destroy later */
	TSet<FNiagaraSystemInstanceID> DeferredDestroyList;
};

