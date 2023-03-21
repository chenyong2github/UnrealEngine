// Copyright Epic Games, Inc. All Rights Reserved.

#include "DeformerDataInterfaceGroomWrite.h"

#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "OptimusDataDomain.h"
#include "RenderGraphBuilder.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "ShaderParameterMetadataBuilder.h"
#include "SkeletalMeshDeformerHelpers.h"
#include "SkeletalRenderPublic.h"
#include "GroomComponent.h"
#include "GroomInstance.h"
#include "DeformerGroomComponentSource.h"

FString UOptimusGroomWriteDataInterface::GetDisplayName() const
{
	return TEXT("Write Groom");
}

FName UOptimusGroomWriteDataInterface::GetCategory() const
{
	return CategoryName::OutputDataInterfaces;
}

TArray<FOptimusCDIPinDefinition> UOptimusGroomWriteDataInterface::GetPinDefinitions() const
{
	FName ControlPoint(UOptimusGroomComponentSource::Domains::ControlPoint);
	FName Curve(UOptimusGroomComponentSource::Domains::Curve);

	TArray<FOptimusCDIPinDefinition> Defs;
	Defs.Add({ "Position", "WritePosition", ControlPoint, "ReadNumControlPoints" });
	return Defs;
}


TSubclassOf<UActorComponent> UOptimusGroomWriteDataInterface::GetRequiredComponentClass() const
{
	return UGroomComponent::StaticClass();
}


void UOptimusGroomWriteDataInterface::GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const
{
	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadNumControlPoints"))
		.AddReturnType(EShaderFundamentalType::Uint);
}

void UOptimusGroomWriteDataInterface::GetSupportedOutputs(TArray<FShaderFunctionDefinition>& OutFunctions) const
{
	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("WritePosition"))
		.AddParam(EShaderFundamentalType::Uint)
		.AddParam(EShaderFundamentalType::Float, 3);
}

BEGIN_SHADER_PARAMETER_STRUCT(FGroomWriteDataInterfaceParameters, )
	SHADER_PARAMETER(uint32, NumControlPoints)
	SHADER_PARAMETER(uint32, NumCurves)
	SHADER_PARAMETER(uint32, OutputStreamStart)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, PositionOffsetBufferSRV)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint4>, PositionBufferSRV)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint4>, PositionBufferUAV)
END_SHADER_PARAMETER_STRUCT()

void UOptimusGroomWriteDataInterface::GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& InOutBuilder, FShaderParametersMetadataAllocations& InOutAllocations) const
{
	InOutBuilder.AddNestedStruct<FGroomWriteDataInterfaceParameters>(UID);
}

TCHAR const* UOptimusGroomWriteDataInterface::TemplateFilePath = TEXT("/Plugin/Runtime/HairStrands/Private/DeformerDataInterfaceGroomWrite.ush");

TCHAR const* UOptimusGroomWriteDataInterface::GetShaderVirtualPath() const
{
	return TemplateFilePath;
}

void UOptimusGroomWriteDataInterface::GetShaderHash(FString& InOutKey) const
{
	GetShaderFileHash(TemplateFilePath, EShaderPlatform::SP_PCD3D_SM5).AppendString(InOutKey);
}

void UOptimusGroomWriteDataInterface::GetHLSL(FString& OutHLSL, FString const& InDataInterfaceName) const
{
	TMap<FString, FStringFormatArg> TemplateArgs =
	{
		{ TEXT("DataInterfaceName"), InDataInterfaceName },
	};

	FString TemplateFile;
	LoadShaderSourceFile(TemplateFilePath, EShaderPlatform::SP_PCD3D_SM5, &TemplateFile, nullptr);
	OutHLSL += FString::Format(*TemplateFile, TemplateArgs);
}

UComputeDataProvider* UOptimusGroomWriteDataInterface::CreateDataProvider(TObjectPtr<UObject> InBinding, uint64 InInputMask, uint64 InOutputMask) const
{
	UOptimusGroomWriteDataProvider* Provider = NewObject<UOptimusGroomWriteDataProvider>();
	Provider->GroomComponent = Cast<UGroomComponent>(InBinding);
	Provider->OutputMask = InOutputMask;
	return Provider;
}


FComputeDataProviderRenderProxy* UOptimusGroomWriteDataProvider::GetRenderProxy()
{
	return new FOptimusGroomWriteDataProviderProxy(GroomComponent, OutputMask);
}


FOptimusGroomWriteDataProviderProxy::FOptimusGroomWriteDataProviderProxy(UGroomComponent* InGroomComponent, uint64 InOutputMask)
{
	const uint32 InstanceCount = InGroomComponent ? InGroomComponent->GetGroupCount() : 0;
	for (uint32 Index = 0; Index < InstanceCount; ++Index)
	{
		Instances.Add(InGroomComponent->GetGroupInstance(Index));
	}
}

bool FOptimusGroomWriteDataProviderProxy::IsValid(FValidationData const& InValidationData) const
{
	if (InValidationData.ParameterStructSize != sizeof(FParameters))
	{
		return false;
	}
	if (InValidationData.NumInvocations != Instances.Num())
	{
		return false;
	}

	return true;
}

void FOptimusGroomWriteDataProviderProxy::AllocateResources(FRDGBuilder& GraphBuilder)
{
	for (FHairGroupInstance* GroomInstance : Instances)
	{
		if (GroomInstance)
		{
			// Allocate required buffers
			const int32 NumControlPoints = GroomInstance->Strands.Data->GetNumPoints();
			const int32 NumCurves = GroomInstance->Strands.Data->GetNumCurves();

			FResources& R = Resources.AddDefaulted_GetRef();
			if (OutputMask & 1)
			{
				R.PositionOffsetBufferSRV = Register(GraphBuilder, GroomInstance->Strands.RestResource->PositionOffsetBuffer, ERDGImportedBufferFlags::CreateSRV).SRV;
				R.PositionBufferSRV = Register(GraphBuilder, GroomInstance->Strands.RestResource->PositionBuffer, ERDGImportedBufferFlags::CreateSRV).SRV;
				R.PositionBufferUAV = Register(GraphBuilder, GroomInstance->Strands.DeformedResource->GetDeformerBuffer(GraphBuilder), ERDGImportedBufferFlags::CreateUAV).UAV;
				
				FRDGBufferRef PositionBuffer_fallback = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(8, 1), TEXT("Groom.DeformedPositionBuffer"), ERDGBufferFlags::None);
				R.PositionBufferSRV_fallback = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(GWhiteVertexBufferWithRDG->Buffer), PF_R16G16B16A16_UINT);
				R.PositionBufferUAV_fallback = GraphBuilder.CreateUAV(PositionBuffer_fallback, PF_R16G16B16A16_UINT, ERDGUnorderedAccessViewFlags::SkipBarrier);
			}
			else
			{
				R.PositionOffsetBufferSRV = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(GWhiteVertexBufferWithRDG->Buffer), PF_A32B32G32R32F);
				R.PositionBufferSRV = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(GWhiteVertexBufferWithRDG->Buffer), PF_R16G16B16A16_UINT);
				R.PositionBufferUAV = GraphBuilder.CreateUAV(GraphBuilder.RegisterExternalBuffer(GWhiteVertexBufferWithRDG->Buffer), PF_R16G16B16A16_UINT);
			}
		}	
	}
}

void FOptimusGroomWriteDataProviderProxy::GatherDispatchData(FDispatchData const& InDispatchData)
{
	const TStridedView<FParameters> ParameterArray = MakeStridedParameterView<FParameters>(InDispatchData);
	for (int32 InvocationIndex = 0; InvocationIndex < ParameterArray.Num(); ++InvocationIndex)
	{
		if (FHairGroupInstance* GroomInstance = Instances[InvocationIndex])
		{
			FResources& Resource = Resources[InvocationIndex];

			const bool bValid = Resource.PositionBufferUAV != nullptr && Resource.PositionBufferSRV != nullptr && Resource.PositionOffsetBufferSRV != nullptr;
			const int32 NumCurves = bValid ? GroomInstance->Strands.Data->GetNumCurves() : 0;
			const int32 NumControlPoints = bValid ? GroomInstance->Strands.Data->GetNumPoints() : 0;

			FParameters& Parameters = ParameterArray[InvocationIndex];
			Parameters.NumControlPoints = NumControlPoints;
			Parameters.NumCurves = NumCurves;
			Parameters.OutputStreamStart = 0;
			Parameters.PositionOffsetBufferSRV = bValid ? Resource.PositionOffsetBufferSRV : Resource.PositionBufferSRV_fallback;
			Parameters.PositionBufferSRV = bValid ? Resource.PositionBufferSRV : Resource.PositionBufferSRV_fallback;
			Parameters.PositionBufferUAV = bValid ? Resource.PositionBufferUAV : Resource.PositionBufferUAV_fallback;
		}
	}
}