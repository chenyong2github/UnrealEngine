// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NiagaraMergeable.h"
#include "NiagaraDataInterfaceBase.generated.h"

class INiagaraCompiler;
class UCurveVector;
class UCurveLinearColor;
class UCurveFloat;
class FNiagaraSystemInstance;
class FNiagaraShader;
struct FNiagaraDataInterfaceParamRef;
class FRHICommandList;

struct FNiagaraDataInterfaceProxy;
class NiagaraEmitterInstanceBatcher;

struct FNiagaraDataInterfaceSetArgs
{
	FNiagaraShader* Shader;
	FNiagaraDataInterfaceProxy* DataInterface;
	FGuid SystemInstance;
	const NiagaraEmitterInstanceBatcher* Batcher;
	uint32 ShaderStageIndex;
	bool IsOutputStage;
	bool IsIterationStage;
};

/**
* An interface to the parameter bindings for the data interface used by a Niagara compute shader.
*/
struct FNiagaraDataInterfaceParametersCS
{
	virtual ~FNiagaraDataInterfaceParametersCS() {}
	virtual void Bind(const FNiagaraDataInterfaceParamRef& ParamRef, const class FShaderParameterMap& ParameterMap) {}
	virtual void Serialize(FArchive& Ar) { }
	virtual void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const {}
	virtual void Unset(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const {}

};

//////////////////////////////////////////////////////////////////////////

/** Base class for all Niagara data interfaces. */
UCLASS(abstract, EditInlineNew)
class NIAGARACORE_API UNiagaraDataInterfaceBase : public UNiagaraMergeable
{
	GENERATED_UCLASS_BODY()

public:
	
	/** Constructs the correct CS parameter type for this DI (if any). The object type returned by this can only vary by class and not per object data. */
	virtual FNiagaraDataInterfaceParametersCS* ConstructComputeParameters()const
	{
		return nullptr;
	}
};


