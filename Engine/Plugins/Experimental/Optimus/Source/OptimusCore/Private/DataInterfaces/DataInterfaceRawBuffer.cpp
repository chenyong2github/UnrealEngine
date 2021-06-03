// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataInterfaces/DataInterfaceRawBuffer.h"

#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ShaderCore.h"
#include "ShaderParameterMetadataBuilder.h"

bool UTransientBufferDataInterface::SupportsAtomics() const
{
	return Type.FundamentalType == EShaderFundamentalType::Int;
}

void UTransientBufferDataInterface::GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const
{
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("ReadValue");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = Type;
		Fn.ParamTypes.Add(ReturnParam);
		FShaderParamTypeDefinition Param0 = {};
		Param0.FundamentalType = EShaderFundamentalType::Uint;
		Param0.DimType = EShaderFundamentalDimensionType::Scalar;
		Fn.ParamTypes.Add(Param0);
		OutFunctions.Add(Fn);
	}
	if (SupportsAtomics())
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("ReadAtomicAdd");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = Type;
		Fn.ParamTypes.Add(ReturnParam);
		FShaderParamTypeDefinition Param0 = {};
		Param0.FundamentalType = EShaderFundamentalType::Uint;
		Param0.DimType = EShaderFundamentalDimensionType::Scalar;
		Fn.ParamTypes.Add(Param0);
		FShaderParamTypeDefinition Param1 = Type;
		Fn.ParamTypes.Add(Param1);
		OutFunctions.Add(Fn);
	}
}

void UTransientBufferDataInterface::GetSupportedOutputs(TArray<FShaderFunctionDefinition>& OutFunctions) const
{
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("WriteValue");
		Fn.bHasReturnType = false;
		FShaderParamTypeDefinition Param0 = {};
		Param0.FundamentalType = EShaderFundamentalType::Uint;
		Param0.DimType = EShaderFundamentalDimensionType::Scalar;
		Fn.ParamTypes.Add(Param0);
		FShaderParamTypeDefinition Param1 = Type;
		Fn.ParamTypes.Add(Param1);
		OutFunctions.Add(Fn);
	}
	if (SupportsAtomics())
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("WriteAtomicAdd");
		Fn.bHasReturnType = false;
		FShaderParamTypeDefinition Param0 = {};
		Param0.FundamentalType = EShaderFundamentalType::Uint;
		Param0.DimType = EShaderFundamentalDimensionType::Scalar;
		Fn.ParamTypes.Add(Param0);
		FShaderParamTypeDefinition Param1 = Type;
		Fn.ParamTypes.Add(Param1);
		OutFunctions.Add(Fn);
	}
}

BEGIN_SHADER_PARAMETER_STRUCT(FTransientBufferDataInterfaceParameters, )
	SHADER_PARAMETER(uint32, StartOffset)
	SHADER_PARAMETER(uint32, BufferSize)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<int>, BufferSRV)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<int>, BufferUAV)
END_SHADER_PARAMETER_STRUCT()

void UTransientBufferDataInterface::GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& OutBuilder) const
{
	OutBuilder.AddNestedStruct<FTransientBufferDataInterfaceParameters>(UID);
}

void UTransientBufferDataInterface::GetHLSL(FString& OutHLSL) const
{
	OutHLSL += TEXT("#define BUFFER_TYPE ");
	OutHLSL += Type.TypeDeclaration;
	OutHLSL += TEXT(" \n");
	if (SupportsAtomics()) { OutHLSL += TEXT("#define BUFFER_TYPE_SUPPORTS_ATOMIC 1\n"); }
	OutHLSL += TEXT("#include \"/Plugin/Optimus/Private/DataInterfaceRawBuffer.ush\"\n");
	OutHLSL += TEXT("#undef BUFFER_TYPE\n");
	if (SupportsAtomics()) { OutHLSL += TEXT("#undef BUFFER_TYPE_SUPPORTS_ATOMIC\n"); }
}

void UTransientBufferDataInterface::ModifyCompilationEnvironment(FShaderCompilerEnvironment& OutEnvironment) const
{
}

FComputeDataProviderRenderProxy* UTransientBufferDataProvider::GetRenderProxy()
{
	return new FTransientBufferDataProviderProxy(ElementStride, NumElements, bClearBeforeUse);
}

FTransientBufferDataProviderProxy::FTransientBufferDataProviderProxy(int32 InElementStride, int32 InNumElements, bool bInClearBeforeUse)
	: ElementStride(InElementStride)
	, NumElements(InNumElements)
	, bClearBeforeUse(bInClearBeforeUse)
{
}

void FTransientBufferDataProviderProxy::AllocateResources(FRDGBuilder& GraphBuilder)
{
	// todo[CF]: Need to know number of invocations and size for each invocation
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Buffer.Add(GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(ElementStride, NumElements), TEXT("TransientBuffer"), ERDGBufferFlags::None));
		BufferSRV.Add(GraphBuilder.CreateSRV(Buffer[Index]));
		BufferUAV.Add(GraphBuilder.CreateUAV(Buffer[Index]));

		if (bClearBeforeUse)
		{
			AddClearUAVPass(GraphBuilder, BufferUAV[Index], 0);
		}
	}
}

void FTransientBufferDataProviderProxy::GetBindings(int32 InvocationIndex, TCHAR const* UID, FBindings& OutBindings) const
{
	FTransientBufferDataInterfaceParameters Parameters;
	FMemory::Memset(&Parameters, 0, sizeof(Parameters));
	Parameters.StartOffset = 0;
	Parameters.BufferSize = NumElements;
	Parameters.BufferSRV = BufferSRV[InvocationIndex];
	Parameters.BufferUAV = BufferUAV[InvocationIndex];

	TArray<uint8> ParamData;
	ParamData.SetNum(sizeof(Parameters));
	FMemory::Memcpy(ParamData.GetData(), &Parameters, sizeof(Parameters));
	OutBindings.Structs.Add(TTuple<FString, TArray<uint8> >(UID, MoveTemp(ParamData)));
}
