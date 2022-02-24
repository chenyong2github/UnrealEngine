// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataInterfaces/DataInterfaceRawBuffer.h"

#include "OptimusDeformerInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "ComputeFramework/ShaderParameterMetadataBuilder.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SkeletalRenderPublic.h"
#include "Algo/Accumulate.h"


const int32 URawBufferDataInterface::ReadValueInputIndex = 1;
const int32 URawBufferDataInterface::WriteValueOutputIndex = 0;


USkinnedMeshComponent* URawBufferDataInterface::GetComponentFromSourceObjects(
	TArrayView<TObjectPtr<UObject>> InSourceObjects)
{
	if (InSourceObjects.Num() != 1)
	{
		return nullptr;
	}
	
	return Cast<USkinnedMeshComponent>(InSourceObjects[0]);
}


void URawBufferDataInterface::FillProviderFromComponent(
	const USkinnedMeshComponent* InComponent,
	URawBufferDataProvider* InProvider
	) const
{
	InProvider->ElementStride = ValueType->GetResourceElementSize();
	InProvider->NumElementsPerInvocation.Reset();

	FSkeletalMeshRenderData const* SkeletalMeshRenderData = InComponent != nullptr ? InComponent->GetSkeletalMeshRenderData() : nullptr;
	if (SkeletalMeshRenderData == nullptr)
	{
		return;
	}
	
	FSkeletalMeshLODRenderData const* LodRenderData = SkeletalMeshRenderData->GetPendingFirstLOD(0);

	if (DataDomain.Name == Optimus::DomainName::Triangle)
	{
		for (const FSkelMeshRenderSection& RenderSection: LodRenderData->RenderSections)
		{
			InProvider->NumElementsPerInvocation.Add(RenderSection.NumTriangles);
		}
	}
	else
	{
		// TODO: For now, all other domain types default to vertex counts. 
		for (const FSkelMeshRenderSection& RenderSection: LodRenderData->RenderSections)
		{	
			InProvider->NumElementsPerInvocation.Add(RenderSection.NumVertices);
		}
	}
}


bool URawBufferDataInterface::SupportsAtomics() const
{
	return ValueType->Type == EShaderFundamentalType::Int;
}

TArray<FOptimusCDIPinDefinition> URawBufferDataInterface::GetPinDefinitions() const
{
	TArray<FOptimusCDIPinDefinition> Defs;
	Defs.Add({"ValueIn", "ReadValue", DataDomain.Name, "ReadNumValues"});
	Defs.Add({"ValueOut", "WriteValue", DataDomain.Name, "ReadNumValues"});
	return Defs;
}

void URawBufferDataInterface::GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const
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

void URawBufferDataInterface::GetSupportedOutputs(TArray<FShaderFunctionDefinition>& OutFunctions) const
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

BEGIN_SHADER_PARAMETER_STRUCT(FPersistentBufferDataInterfaceParameters, )
	SHADER_PARAMETER(uint32, StartOffset)
	SHADER_PARAMETER(uint32, BufferSize)
	SHADER_PARAMETER_UAV(RWStructuredBuffer<int>, BufferUAV)
END_SHADER_PARAMETER_STRUCT()


void UPersistentBufferDataInterface::GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& OutBuilder) const
{
	OutBuilder.AddNestedStruct<FPersistentBufferDataInterfaceParameters>(UID);
}

void URawBufferDataInterface::GetHLSL(FString& OutHLSL) const
{
	OutHLSL += TEXT("#define BUFFER_TYPE ");
	OutHLSL += ValueType->ToString();
	OutHLSL += TEXT(" \n");
	if (SupportsAtomics()) { OutHLSL += TEXT("#define BUFFER_TYPE_SUPPORTS_ATOMIC 1\n"); }
	if (UseSplitBuffers()) { OutHLSL += TEXT("#define BUFFER_SPLIT_READ_WRITE 1\n"); }
	OutHLSL += TEXT("#include \"/Plugin/Optimus/Private/DataInterfaceRawBuffer.ush\"\n");
	OutHLSL += TEXT("#undef BUFFER_TYPE\n");
	if (SupportsAtomics()) { OutHLSL += TEXT("#undef BUFFER_TYPE_SUPPORTS_ATOMIC\n"); }
	if (UseSplitBuffers()) { OutHLSL += TEXT("#undef BUFFER_SPLIT_READ_WRITE\n"); }
}


void URawBufferDataInterface::GetSourceTypes(TArray<UClass*>& OutSourceTypes) const
{
	// Default setup with an assumption that we want to size to match a USkinnedMeshComponent.
	// That's a massive generalization of course...
	OutSourceTypes.Add(USkinnedMeshComponent::StaticClass());
}


FString UTransientBufferDataInterface::GetDisplayName() const
{
	return TEXT("Transient");
}

UComputeDataProvider* UTransientBufferDataInterface::CreateDataProvider(TArrayView< TObjectPtr<UObject> > InSourceObjects, uint64 InInputMask, uint64 InOutputMask) const
{
	UTransientBufferDataProvider *Provider = NewObject<UTransientBufferDataProvider>();
	FillProviderFromComponent(GetComponentFromSourceObjects(InSourceObjects), Provider);
	Provider->bClearBeforeUse = bClearBeforeUse;
	return Provider;
}


FString UPersistentBufferDataInterface::GetDisplayName() const
{
	return TEXT("Persistent");
}


UComputeDataProvider* UPersistentBufferDataInterface::CreateDataProvider(
	TArrayView<TObjectPtr<UObject>> InSourceObjects,
	uint64 InInputMask,
	uint64 InOutputMask
	) const
{
	UPersistentBufferDataProvider *Provider = NewObject<UPersistentBufferDataProvider>();

	if (USkinnedMeshComponent* Component = GetComponentFromSourceObjects(InSourceObjects))
	{
		FillProviderFromComponent(Component, Provider);

		Provider->SkinnedMeshComponent = Component;
		Provider->ResourceName = ResourceName;
	}

	return Provider;
}


bool URawBufferDataProvider::IsValid() const
{
	return !NumElementsPerInvocation.IsEmpty();
}


FComputeDataProviderRenderProxy* UTransientBufferDataProvider::GetRenderProxy()
{
	return new FTransientBufferDataProviderProxy(ElementStride, NumElementsPerInvocation, bClearBeforeUse);
}


bool UPersistentBufferDataProvider::IsValid() const
{
	return URawBufferDataProvider::IsValid();
}


FComputeDataProviderRenderProxy* UPersistentBufferDataProvider::GetRenderProxy()
{
	FOptimusPersistentBufferPoolPtr BufferPoolPtr;
	
	const UOptimusDeformerInstance* DeformerInstance = Cast<UOptimusDeformerInstance>(SkinnedMeshComponent->MeshDeformerInstance);
	if (ensure(DeformerInstance))
	{
		BufferPoolPtr = DeformerInstance->GetBufferPool();
	}	
	return new FPersistentBufferDataProviderProxy(BufferPoolPtr, ResourceName, ElementStride, NumElementsPerInvocation);
}


FTransientBufferDataProviderProxy::FTransientBufferDataProviderProxy(
	int32 InElementStride,
	TArray<int32> InInvocationElementCount,
	bool bInClearBeforeUse
	)
	: ElementStride(InElementStride)
	, InvocationElementCount(InInvocationElementCount)
	, bClearBeforeUse(bInClearBeforeUse)
{
	
}

void FTransientBufferDataProviderProxy::AllocateResources(FRDGBuilder& GraphBuilder)
{
	for (const int32 NumElements: InvocationElementCount)
	{
		// todo[CF]: Over allocating by 8x here until the logic for correct buffer size is handled.
		Buffer.Add(GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(ElementStride, NumElements * 8), TEXT("TransientBuffer"), ERDGBufferFlags::None));
		BufferSRV.Add(GraphBuilder.CreateSRV(Buffer.Last()));
		BufferUAV.Add(GraphBuilder.CreateUAV(Buffer.Last()));

		if (bClearBeforeUse)
		{
			AddClearUAVPass(GraphBuilder, BufferUAV.Last(), 0);
		}
	}
}

void FTransientBufferDataProviderProxy::GatherDispatchData(FDispatchSetup const& InDispatchSetup, FCollectedDispatchData& InOutDispatchData)
{
	if (!ensure(InDispatchSetup.ParameterStructSizeForValidation == sizeof(FTransientBufferDataInterfaceParameters)))
	{
		return;
	}

	for (int32 InvocationIndex = 0; InvocationIndex < InvocationElementCount.Num(); ++InvocationIndex)
	{
		FTransientBufferDataInterfaceParameters* Parameters =
			reinterpret_cast<FTransientBufferDataInterfaceParameters*>(InOutDispatchData.ParameterBuffer + InDispatchSetup.ParameterBufferOffset + InDispatchSetup.ParameterBufferStride * InvocationIndex);
		Parameters->StartOffset = 0;
		Parameters->BufferSize = InvocationElementCount[InvocationIndex];
		Parameters->BufferSRV = BufferSRV[InvocationIndex];
		Parameters->BufferUAV = BufferUAV[InvocationIndex];
	}
}



FPersistentBufferDataProviderProxy::FPersistentBufferDataProviderProxy(
	TSharedPtr<FOptimusPersistentBufferPool> InBufferPool,
	FName InResourceName,
	int32 InElementStride,
	TArray<int32> InInvocationElementCount
	) :
	BufferPool(InBufferPool),
	ResourceName(InResourceName),	
	ElementStride(InElementStride),
	InvocationElementCount(InInvocationElementCount)
{
	
}


void FPersistentBufferDataProviderProxy::AllocateResources(
	FRDGBuilder& GraphBuilder
	)
{
}


void FPersistentBufferDataProviderProxy::GatherDispatchData(
	FDispatchSetup const& InDispatchSetup,
	FCollectedDispatchData& InOutDispatchData
	)
{
	if (!ensure(InDispatchSetup.ParameterStructSizeForValidation == sizeof(FPersistentBufferDataInterfaceParameters)))
	{
		return;
	}

	const TArray<FOptimusPersistentStructuredBufferPtr>& Buffers = BufferPool->GetResourceBuffers(ResourceName, ElementStride, InvocationElementCount);
	if (Buffers.Num() != InvocationElementCount.Num())
	{
		return;
	}
	
	for (int32 InvocationIndex = 0; InvocationIndex < InvocationElementCount.Num(); ++InvocationIndex)
	{
		FPersistentBufferDataInterfaceParameters* Parameters =
			reinterpret_cast<FPersistentBufferDataInterfaceParameters*>(InOutDispatchData.ParameterBuffer + InDispatchSetup.ParameterBufferOffset + InDispatchSetup.ParameterBufferStride * InvocationIndex);
		
		Parameters->StartOffset = 0;
		Parameters->BufferSize = InvocationElementCount[InvocationIndex];
		Parameters->BufferUAV = Buffers[InvocationIndex]->GetUAV();
	}

	FComputeDataProviderRenderProxy::GatherDispatchData(InDispatchSetup, InOutDispatchData);
}
