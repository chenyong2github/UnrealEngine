// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NiagaraDataInterface.h"
#include "NiagaraCommon.h"
#include "Components/SplineComponent.h"
#include "NiagaraDataInterfaceWater.generated.h"

class AWaterBody;

UCLASS(EditInlineNew, Category = "Water", meta = (DisplayName = "Water"))
class WATER_API UNiagaraDataInterfaceWater : public UNiagaraDataInterface
{
	GENERATED_BODY()

public:
	virtual void PostInitProperties() override;

	/** UNiagaraDataInterface interface */
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) override;
	virtual bool Equals(const UNiagaraDataInterface* Other) const;
	virtual bool CopyTo(UNiagaraDataInterface* Destination) const;

	virtual int32 PerInstanceDataSize() const override;
	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance);
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance);
	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	virtual bool HasPreSimulateTick() const override { return true; }

	void GetWaterDataAtPoint(FVectorVMContext& Context);

	void GetWaveParamLookupTableOffset(FVectorVMContext& Context);

	/** Sets the current water body to be used by this data interface */
	void SetWaterBody(AWaterBody* InWaterBody) { SourceBody = InWaterBody; }
private:
	UPROPERTY(EditAnywhere, Category = "Water") 
	AWaterBody* SourceBody;
};
