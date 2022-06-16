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

	// Adds a loose parameter that is scoped to the data interface
	// i.e. if the parameter was called "MyFloat" the shader variable would be "UniqueDataInterfaceName_MyFloat"
	template<typename T> void AddLooseParam(const TCHAR* Name)
	{
		LooseNames.Emplace(FString::Printf(TEXT("%s_%s"), *GPUParamInfo.DataInterfaceHLSLSymbol, Name));
		MetadataBuilder.AddParam<T>(*LooseNames.Last());
	}

	// Adds a loose array parameter that is scoped to the data interface
	// i.e. if the parameter was called "MyFloat" the shader variable would be "UniqueDataInterfaceName_MyFloat"
	template<typename T> void AddLooseParamArray(const TCHAR* Name, int32 NumElements)
	{
		LooseNames.Emplace(FString::Printf(TEXT("%s_%s"), *GPUParamInfo.DataInterfaceHLSLSymbol, Name));
		MetadataBuilder.AddParamArray<T>(*LooseNames.Last(), NumElements);
	}

	// Adds a shader parameters structure that is scoped to the data interface
	// i.e. if the structured contained "MyFloat" the shader variable would be "UniqueDataInterfaceName_MyFloat"
	template<typename T> void AddNestedStruct()
	{
		MetadataBuilder.AddNestedStruct<T>(*GPUParamInfo.DataInterfaceHLSLSymbol);
	}

	// Adds a shadera parameters structure that is global in scope
	// i.e. if the structure contained "MyFloat" the shader variable would be named "MyFloat"
	template<typename T> void AddIncludedStruct()
	{
		MetadataBuilder.AddIncludedStruct<T>();
	}

	TConstArrayView<FNiagaraDataInterfaceGeneratedFunction> GetGeneratedFunctions() const { return GPUParamInfo.GeneratedFunctions; }

private:
	const FNiagaraDataInterfaceGPUParamInfo& GPUParamInfo;
	TArray<FString>& LooseNames;
	FShaderParametersMetadataBuilder& MetadataBuilder;
};
