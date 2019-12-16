// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NiagaraCommon.h"
#include "NiagaraShared.h"
#include "NiagaraDataInterface.h"
#include "NiagaraParameterStore.h"
#include "Camera/PlayerCameraManager.h"
#include "NiagaraDataInterfaceCamera.generated.h"

struct CameraDataInterface_InstanceData
{
	TWeakObjectPtr<APlayerCameraManager> CameraObject = nullptr;

	/** A binding to the user ptr we're reading the camera from. */
	//FNiagaraParameterDirectBinding<UObject*> UserParamBinding;
};

UCLASS(EditInlineNew, Category = "Camera", meta = (DisplayName = "Camera Query"))
class NIAGARA_API UNiagaraDataInterfaceCamera : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()

public:
	/** Reference to a user parameter that should receive the particle data after the simulation tick. The supplied parameter object needs to implement the INiagaraParticleCallbackHandler interface. */
	//UPROPERTY(EditAnywhere, Category = "Camera")
	//FNiagaraUserParameterBinding CameraParameter;

	//UObject Interface
	virtual void PostInitProperties() override;
	//UObject Interface End

	//UNiagaraDataInterface Interface
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) override;
	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual int32 PerInstanceDataSize() const override { return sizeof(CameraDataInterface_InstanceData); }
	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	virtual bool GetFunctionHLSL(const FName&  DefinitionFunctionName, FString InstanceFunctionName, FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { return true; }
	virtual FNiagaraDataInterfaceParametersCS* ConstructComputeParameters() const override;
	//UNiagaraDataInterface Interface

	void QueryOcclusionFactorGPU(FVectorVMContext& Context);
	void GetCameraFOV(FVectorVMContext& Context);
	void GetCameraPosition(FVectorVMContext& Context);
	void GetViewPropertiesGPU(FVectorVMContext& Context);
	void GetClipSpaceTransformsGPU(FVectorVMContext& Context);
	void GetViewSpaceTransformsGPU(FVectorVMContext& Context);
	
private:
	static const FName GetCameraOcclusionName;
	static const FName GetViewPropertiesName;
	static const FName GetClipSpaceTransformsName;
	static const FName GetViewSpaceTransformsName;
	static const FName GetCameraPositionsName;
	static const FName GetFieldOfViewName;
};

struct FNiagaraDataIntefaceProxyCameraQuery : public FNiagaraDataInterfaceProxy
{
	// There's nothing in this proxy. It just reads from scene textures.

	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override
	{
		return 0;
	}
};