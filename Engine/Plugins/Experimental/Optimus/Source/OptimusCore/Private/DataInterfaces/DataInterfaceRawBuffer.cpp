// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataInterfaces/DataInterfaceRawBuffer.h"

#include "Components/SkeletalMeshComponent.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "ShaderCore.h"
#include "ShaderParameterMetadataBuilder.h"
#include "SkeletalRenderPublic.h"

const int32 UTransientBufferDataInterface::ReadValueInputIndex = 1;
const int32 UTransientBufferDataInterface::WriteValueOutputIndex = 0;

bool UTransientBufferDataInterface::SupportsAtomics() const
{
	return ValueType->Type == EShaderFundamentalType::Int;
}

FString UTransientBufferDataInterface::GetDisplayName() const
{
	return TEXT("Transient");
}

TArray<FOptimusCDIPinDefinition> UTransientBufferDataInterface::GetPinDefinitions() const
{
	TArray<FOptimusCDIPinDefinition> Defs;
	Defs.Add({"ValueIn", "ReadValue", DataDomain.Name, "ReadNumValues"});
	Defs.Add({"ValueOut", "WriteValue", DataDomain.Name, "ReadNumValues"});
	return Defs;
}

void UTransientBufferDataInterface::GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const
{
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("ReadNumValues");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = {};
		ReturnParam.ValueType = FShaderValueType::Get(EShaderFundamentalType::Uint);
		Fn.ParamTypes.Add(ReturnParam);
		OutFunctions.Add(Fn);
	}
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("ReadValue");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = {};
		ReturnParam.ValueType = ValueType;
		Fn.ParamTypes.Add(ReturnParam);
		FShaderParamTypeDefinition Param0 = {};
		Param0.ValueType = FShaderValueType::Get(EShaderFundamentalType::Uint);
		Fn.ParamTypes.Add(Param0);
		OutFunctions.Add(Fn);
	}
	if (SupportsAtomics())
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("ReadAtomicAdd");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = {};
		ReturnParam.ValueType = ValueType;
		Fn.ParamTypes.Add(ReturnParam);
		FShaderParamTypeDefinition Param0 = {};
		Param0.ValueType = FShaderValueType::Get(EShaderFundamentalType::Uint);
		Fn.ParamTypes.Add(Param0);
		FShaderParamTypeDefinition Param1 = {};
		Param1.ValueType = ValueType;
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
		Param0.ValueType = FShaderValueType::Get(EShaderFundamentalType::Uint);
		Fn.ParamTypes.Add(Param0);
		FShaderParamTypeDefinition Param1 = {};
		Param1.ValueType = ValueType;
		Fn.ParamTypes.Add(Param1);
		OutFunctions.Add(Fn);
	}
	if (SupportsAtomics())
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("WriteAtomicAdd");
		Fn.bHasReturnType = false;
		FShaderParamTypeDefinition Param0 = {};
		Param0.ValueType = FShaderValueType::Get(EShaderFundamentalType::Uint);
		Fn.ParamTypes.Add(Param0);
		FShaderParamTypeDefinition Param1 = {};
		Param1.ValueType = ValueType;
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
	OutHLSL += ValueType->ToString();
	OutHLSL += TEXT(" \n");
	if (SupportsAtomics()) { OutHLSL += TEXT("#define BUFFER_TYPE_SUPPORTS_ATOMIC 1\n"); }
	OutHLSL += TEXT("#include \"/Plugin/Optimus/Private/DataInterfaceRawBuffer.ush\"\n");
	OutHLSL += TEXT("#undef BUFFER_TYPE\n");
	if (SupportsAtomics()) { OutHLSL += TEXT("#undef BUFFER_TYPE_SUPPORTS_ATOMIC\n"); }
}

void UTransientBufferDataInterface::ModifyCompilationEnvironment(FShaderCompilerEnvironment& OutEnvironment) const
{
}

void UTransientBufferDataInterface::GetSourceTypes(TArray<UClass*>& OutSourceTypes) const
{
	// Default setup with an assumption that we want to size to match a USkeletalMeshComponent.
	// That's a massive generalization of course...
	OutSourceTypes.Add(USkeletalMeshComponent::StaticClass());
}

UComputeDataProvider* UTransientBufferDataInterface::CreateDataProvider(TArrayView< TObjectPtr<UObject> > InSourceObjects, uint64 InInputMask, uint64 InOutputMask) const
{
	UTransientBufferDataProvider *Provider = NewObject<UTransientBufferDataProvider>();
	Provider->ElementStride = ValueType->GetResourceElementSize();
	Provider->NumElements = 0;

	if (InSourceObjects.Num() == 1)
	{
		// todo[CF]: Clear before use needs to be set according to context. For now we default to clearing.
		//Provider->bClearBeforeUse = false;

		USkeletalMeshComponent* SkeletalMesh = Cast<USkeletalMeshComponent>(InSourceObjects[0]);
		FSkeletalMeshRenderData const* SkeletalMeshRenderData = SkeletalMesh != nullptr ? SkeletalMesh->GetSkeletalMeshRenderData() : nullptr;
		if (SkeletalMeshRenderData != nullptr)
		{
			FSkeletalMeshLODRenderData const* LodRenderData = SkeletalMeshRenderData->GetPendingFirstLOD(0);
			Provider->NumInvocations = LodRenderData->RenderSections.Num();
			// todo[CF]: Over allocating here until the logic for correct buffer size is handled.
			Provider->NumElements = LodRenderData->GetNumVertices() * 8;
		}
	}

	return Provider;
}


bool UTransientBufferDataProvider::IsValid() const
{
	return NumElements != 0;
}

FComputeDataProviderRenderProxy* UTransientBufferDataProvider::GetRenderProxy()
{
	return new FTransientBufferDataProviderProxy(ElementStride, NumInvocations, NumElements, bClearBeforeUse);
}


FTransientBufferDataProviderProxy::FTransientBufferDataProviderProxy(int32 InElementStride, int32 InNumInvocations, int32 InNumElements, bool bInClearBeforeUse)
	: ElementStride(InElementStride)
	, NumInvocations(InNumInvocations)
	, NumElements(InNumElements)
	, bClearBeforeUse(bInClearBeforeUse)
{
}

void FTransientBufferDataProviderProxy::AllocateResources(FRDGBuilder& GraphBuilder)
{
	// todo[CF]: Need to know number of invocations and size for each invocation
	for (int32 Index = 0; Index < NumInvocations; ++Index)
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
