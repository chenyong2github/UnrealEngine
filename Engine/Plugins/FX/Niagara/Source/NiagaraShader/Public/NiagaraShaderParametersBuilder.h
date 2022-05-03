// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraShared.h"
#include "ShaderParameterMetadataBuilder.h"

class NIAGARASHADER_API FNiagaraShaderParametersBuilder
{
public:
	explicit FNiagaraShaderParametersBuilder(FNiagaraDataInterfaceGPUParamInfo& InParamInfo, FShaderParametersMetadataBuilder& InMetadataBuilder)
		: ParamInfo(InParamInfo)
		, MetadataBuilder(InMetadataBuilder)
	{
	}

	template<typename T> void AddLooseParam(const TCHAR* Name)
	{
		ParamInfo.LooseMetadataNames.Emplace(FString::Printf(TEXT("%s_%s"), *ParamInfo.DataInterfaceHLSLSymbol, Name));
		MetadataBuilder.AddParam<T>(*ParamInfo.LooseMetadataNames.Last());
	}
	template<typename T> void AddLooseParamArray(const TCHAR* Name, int32 NumElements)
	{
		ParamInfo.LooseMetadataNames.Emplace(FString::Printf(TEXT("%s_%s"), *ParamInfo.DataInterfaceHLSLSymbol, Name));
		MetadataBuilder.AddParamArray<T>(*ParamInfo.LooseMetadataNames.Last(), NumElements);
	}
	template<typename T> void AddNestedStruct()
	{
		MetadataBuilder.AddNestedStruct<T>(*ParamInfo.DataInterfaceHLSLSymbol);
	}

	TArrayView<FNiagaraDataInterfaceGeneratedFunction> GetGeneratedFunctions() const { return ParamInfo.GeneratedFunctions; }

private:
	FNiagaraDataInterfaceGPUParamInfo& ParamInfo;
	FShaderParametersMetadataBuilder& MetadataBuilder;
};
