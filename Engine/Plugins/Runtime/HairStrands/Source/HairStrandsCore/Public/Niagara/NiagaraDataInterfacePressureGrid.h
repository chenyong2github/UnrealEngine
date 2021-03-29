// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceRW.h"
#include "NiagaraCommon.h"
#include "VectorVM.h"
#include "NiagaraDataInterfaceVelocityGrid.h"
#include "NiagaraDataInterfacePressureGrid.generated.h"

/** Data Interface for the strand base */
UCLASS(EditInlineNew, Category = "Grid", meta = (DisplayName = "Pressure Grid"))
class HAIRSTRANDSCORE_API UNiagaraDataInterfacePressureGrid : public UNiagaraDataInterfaceVelocityGrid
{
	GENERATED_UCLASS_BODY()

public:

	DECLARE_NIAGARA_DI_PARAMETER();

	/** UNiagaraDataInterface Interface */
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) override;
	virtual bool HasPreSimulateTick() const override { return true; }

	/** GPU simulation  functionality */
#if WITH_EDITORONLY_DATA
	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;
	virtual void GetCommonHLSL(FString& OutHLSL) override;
	virtual bool AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const override;
#endif

	/** Build the velocity field */
	void BuildDistanceField(FVectorVMContext& Context);

	/** Project the velocity field to be divergence free */
	void SolveGridPressure(FVectorVMContext& Context);

	/** Scale Cell Fields */
	void ScaleCellFields(FVectorVMContext& Context);

	/** Set the solid boundary */
	void SetSolidBoundary(FVectorVMContext& Context);

	/** Compute the solid weights */
	void ComputeBoundaryWeights(FVectorVMContext& Context);

	/** Get Node Position */
	void GetNodePosition(FVectorVMContext& Context);

	/** Get Density Field */
	void GetDensityField(FVectorVMContext& Context);

	/** Build the Density Field */
	void BuildDensityField(FVectorVMContext& Context);

	/** Update the deformation gradient */
	void UpdateDeformationGradient(FVectorVMContext& Context);

};

/** Proxy to send data to gpu */
struct FNDIPressureGridProxy : public FNDIVelocityGridProxy
{
	/** Launch all pre stage functions */
	virtual void PreStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceStageArgs& Context) override;
};

