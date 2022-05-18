// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraShared.h"
#include "ShaderParameterMetadataBuilder.h"

class NIAGARASHADER_API FNiagaraShaderParametersBuilder
{
public:
	explicit FNiagaraShaderParametersBuilder(const FNiagaraDataInterfaceGPUParamInfo& InGPUParamInfo, TArray<FString>& InLooseNames, FShaderParametersMetadataBuilder& InMetadataBuilder)
		: GPUParamInfo(InGPUParamInfo)
		, LooseNames(InLooseNames)
		, MetadataBuilder(InMetadataBuilder)
	{
	}

	template<typename T> void AddLooseParam(const TCHAR* Name)
	{
		LooseNames.Emplace(FString::Printf(TEXT("%s_%s"), *GPUParamInfo.DataInterfaceHLSLSymbol, Name));
		MetadataBuilder.AddParam<T>(*LooseNames.Last());
	}
	template<typename T> void AddLooseParamArray(const TCHAR* Name, int32 NumElements)
	{
		LooseNames.Emplace(FString::Printf(TEXT("%s_%s"), *GPUParamInfo.DataInterfaceHLSLSymbol, Name));
		MetadataBuilder.AddParamArray<T>(*LooseNames.Last(), NumElements);
	}
	template<typename T> void AddNestedStruct()
	{
		MetadataBuilder.AddNestedStruct<T>(*GPUParamInfo.DataInterfaceHLSLSymbol);
	}

	TConstArrayView<FNiagaraDataInterfaceGeneratedFunction> GetGeneratedFunctions() const { return GPUParamInfo.GeneratedFunctions; }

private:
	const FNiagaraDataInterfaceGPUParamInfo& GPUParamInfo;
	TArray<FString>& LooseNames;
	FShaderParametersMetadataBuilder& MetadataBuilder;
};
