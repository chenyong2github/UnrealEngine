// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusDataInterfaceRawBuffer.h"

#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "OptimusDeformerInstance.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "ShaderParameterMetadataBuilder.h"
#include "SkeletalRenderPublic.h"


const int32 UOptimusRawBufferDataInterface::ReadValueInputIndex = 1;
const int32 UOptimusRawBufferDataInterface::WriteValueOutputIndex = 0;


void UOptimusRawBufferDataInterface::FillProviderFromComponent(
	const USkinnedMeshComponent* InComponent,
	UOptimusRawBufferDataProvider* InProvider
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

	if (DataDomain.LevelNames[0] == Optimus::DomainName::Triangle)
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


bool UOptimusRawBufferDataInterface::SupportsAtomics() const
{
	return ValueType->Type == EShaderFundamentalType::Int;
}

TArray<FOptimusCDIPinDefinition> UOptimusRawBufferDataInterface::GetPinDefinitions() const
{
	// FIXME: Multi-level support by proxying through a data interface.
	TArray<FOptimusCDIPinDefinition> Defs;
	Defs.Add({"ValueIn", "ReadValue", DataDomain.LevelNames[0], "ReadNumValues"});
	Defs.Add({"ValueOut", "WriteValue", DataDomain.LevelNames[0], "ReadNumValues"});
	return Defs;
}


TSubclassOf<UActorComponent> UOptimusRawBufferDataInterface::GetRequiredComponentClass() const
{
	return USceneComponent::StaticClass();
}


void UOptimusRawBufferDataInterface::GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const
{
	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadNumValues"))
		.AddReturnType(EShaderFundamentalType::Uint);

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadValue"))
		.AddReturnType(ValueType)
		.AddParam(EShaderFundamentalType::Uint);

	if (SupportsAtomics())
	{
		OutFunctions.AddDefaulted_GetRef()
			.SetName(TEXT("WriteAtomicAdd"))
			.AddReturnType(ValueType)
			.AddParam(EShaderFundamentalType::Uint)
			.AddParam(ValueType);
	}
}

void UOptimusRawBufferDataInterface::GetSupportedOutputs(TArray<FShaderFunctionDefinition>& OutFunctions) const
{
	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("WriteValue"))
		.AddParam(EShaderFundamentalType::Uint)
		.AddParam(ValueType);

	if (SupportsAtomics())
	{
		OutFunctions.AddDefaulted_GetRef()
			.SetName(TEXT("WriteAtomicAdd"))
			.AddParam(EShaderFundamentalType::Uint)
			.AddParam(ValueType);
	}
}

BEGIN_SHADER_PARAMETER_STRUCT(FTransientBufferDataInterfaceParameters, )
	SHADER_PARAMETER(uint32, StartOffset)
	SHADER_PARAMETER(uint32, BufferSize)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<int>, BufferSRV)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<int>, BufferUAV)
END_SHADER_PARAMETER_STRUCT()

void UOptimusTransientBufferDataInterface::GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& InOutBuilder, FShaderParametersMetadataAllocations& InOutAllocations) const
{
	InOutBuilder.AddNestedStruct<FTransientBufferDataInterfaceParameters>(UID);
}

BEGIN_SHADER_PARAMETER_STRUCT(FPersistentBufferDataInterfaceParameters, )
	SHADER_PARAMETER(uint32, StartOffset)
	SHADER_PARAMETER(uint32, BufferSize)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<int>, BufferUAV)
END_SHADER_PARAMETER_STRUCT()

void UOptimusPersistentBufferDataInterface::GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& InOutBuilder, FShaderParametersMetadataAllocations& InOutAllocations) const
{
	InOutBuilder.AddNestedStruct<FPersistentBufferDataInterfaceParameters>(UID);
}

void UOptimusRawBufferDataInterface::GetShaderHash(FString& InOutKey) const
{
	GetShaderFileHash(TEXT("/Plugin/Optimus/Private/DataInterfaceRawBuffer.ush"), EShaderPlatform::SP_PCD3D_SM5).AppendString(InOutKey);
}

void UOptimusRawBufferDataInterface::GetHLSL(FString& OutHLSL) const
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

FString UOptimusTransientBufferDataInterface::GetDisplayName() const
{
	return TEXT("Transient");
}

UComputeDataProvider* UOptimusTransientBufferDataInterface::CreateDataProvider(
	TObjectPtr<UObject> InBinding, uint64 InInputMask, uint64 InOutputMask
	) const
{
	UOptimusTransientBufferDataProvider *Provider = NewObject<UOptimusTransientBufferDataProvider>();
	FillProviderFromComponent(Cast<USkinnedMeshComponent>(InBinding), Provider);
	Provider->bClearBeforeUse = bClearBeforeUse;
	return Provider;
}


FString UOptimusPersistentBufferDataInterface::GetDisplayName() const
{
	return TEXT("Persistent");
}


UComputeDataProvider* UOptimusPersistentBufferDataInterface::CreateDataProvider(
	TObjectPtr<UObject> InBinding, uint64 InInputMask, uint64 InOutputMask
	) const
{
	UOptimusPersistentBufferDataProvider *Provider = NewObject<UOptimusPersistentBufferDataProvider>();

	if (USkinnedMeshComponent* Component = Cast<USkinnedMeshComponent>(InBinding))
	{
		FillProviderFromComponent(Component, Provider);

		Provider->SkinnedMeshComponent = Component;
		Provider->ResourceName = ResourceName;
	}

	return Provider;
}


bool UOptimusRawBufferDataProvider::IsValid() const
{
	return !NumElementsPerInvocation.IsEmpty();
}


FComputeDataProviderRenderProxy* UOptimusTransientBufferDataProvider::GetRenderProxy()
{
	return new FOptimusTransientBufferDataProviderProxy(ElementStride, NumElementsPerInvocation, bClearBeforeUse);
}


bool UOptimusPersistentBufferDataProvider::IsValid() const
{
	const UOptimusDeformerInstance* DeformerInstance = Cast<UOptimusDeformerInstance>(SkinnedMeshComponent->MeshDeformerInstance);
	if (DeformerInstance == nullptr || !DeformerInstance->GetBufferPool().IsValid())
	{
		return false;
	}

	return UOptimusRawBufferDataProvider::IsValid();
}


FComputeDataProviderRenderProxy* UOptimusPersistentBufferDataProvider::GetRenderProxy()
{
	FOptimusPersistentBufferPoolPtr BufferPoolPtr;
	
	const UOptimusDeformerInstance* DeformerInstance = Cast<UOptimusDeformerInstance>(SkinnedMeshComponent->MeshDeformerInstance);
	if (ensure(DeformerInstance))
	{
		BufferPoolPtr = DeformerInstance->GetBufferPool();
	}	
	return new FOptimusPersistentBufferDataProviderProxy(BufferPoolPtr, ResourceName, ElementStride, NumElementsPerInvocation);
}


FOptimusTransientBufferDataProviderProxy::FOptimusTransientBufferDataProviderProxy(
	int32 InElementStride,
	TArray<int32> InInvocationElementCount,
	bool bInClearBeforeUse
	)
	: ElementStride(InElementStride)
	, InvocationElementCount(InInvocationElementCount)
	, bClearBeforeUse(bInClearBeforeUse)
{
	
}

void FOptimusTransientBufferDataProviderProxy::AllocateResources(FRDGBuilder& GraphBuilder)
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

void FOptimusTransientBufferDataProviderProxy::GatherDispatchData(FDispatchSetup const& InDispatchSetup, FCollectedDispatchData& InOutDispatchData)
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



FOptimusPersistentBufferDataProviderProxy::FOptimusPersistentBufferDataProviderProxy(
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


void FOptimusPersistentBufferDataProviderProxy::AllocateResources(
	FRDGBuilder& GraphBuilder
	)
{
	BufferPool->GetResourceBuffers(GraphBuilder, ResourceName, ElementStride, InvocationElementCount, Buffers);
	BufferUAVs.Reserve(Buffers.Num());
	for (FRDGBufferRef BufferRef : Buffers)
	{
		BufferUAVs.Add(GraphBuilder.CreateUAV(BufferRef));
	}
}


void FOptimusPersistentBufferDataProviderProxy::GatherDispatchData(
	FDispatchSetup const& InDispatchSetup,
	FCollectedDispatchData& InOutDispatchData
	)
{
	if (!ensure(InDispatchSetup.ParameterStructSizeForValidation == sizeof(FPersistentBufferDataInterfaceParameters)))
	{
		return;
	}

	if (!ensure(Buffers.Num() == InvocationElementCount.Num()))
	{
		return;
	}
	
	for (int32 InvocationIndex = 0; InvocationIndex < InvocationElementCount.Num(); ++InvocationIndex)
	{
		FPersistentBufferDataInterfaceParameters* Parameters =
			reinterpret_cast<FPersistentBufferDataInterfaceParameters*>(InOutDispatchData.ParameterBuffer + InDispatchSetup.ParameterBufferOffset + InDispatchSetup.ParameterBufferStride * InvocationIndex);
		
		Parameters->StartOffset = 0;
		Parameters->BufferSize = InvocationElementCount[InvocationIndex];
		Parameters->BufferUAV = BufferUAVs[InvocationIndex];
	}

	FComputeDataProviderRenderProxy::GatherDispatchData(InDispatchSetup, InOutDispatchData);
}
