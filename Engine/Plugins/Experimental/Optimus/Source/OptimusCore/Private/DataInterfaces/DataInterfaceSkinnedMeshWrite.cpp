// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataInterfaces/DataInterfaceSkinnedMeshWrite.h"

#include "OptimusDataDomain.h"

#include "Components/SkinnedMeshComponent.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "RenderGraphBuilder.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "ShaderParameterMetadataBuilder.h"
#include "SkeletalMeshDeformerHelpers.h"
#include "SkeletalRenderPublic.h"

FString USkinnedMeshWriteDataInterface::GetDisplayName() const
{
	return TEXT("Write Skinned Mesh");
}

TArray<FOptimusCDIPinDefinition> USkinnedMeshWriteDataInterface::GetPinDefinitions() const
{
	using namespace Optimus::DomainName;
	
	TArray<FOptimusCDIPinDefinition> Defs;
	Defs.Add({ "Position", "WritePosition", Vertex, "ReadNumVertices" });
	Defs.Add({ "TangentX", "WriteTangentX", Vertex, "ReadNumVertices" });
	Defs.Add({ "TangentZ", "WriteTangentZ", Vertex, "ReadNumVertices" });
	Defs.Add({ "Color", "WriteColor", Vertex, "ReadNumVertices" });

	return Defs;
}

void USkinnedMeshWriteDataInterface::GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const
{
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("ReadNumVertices");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = {};
		ReturnParam.ValueType = FShaderValueType::Get(EShaderFundamentalType::Uint);
		Fn.ParamTypes.Add(ReturnParam);
		OutFunctions.Add(Fn);
	}
}

void USkinnedMeshWriteDataInterface::GetSupportedOutputs(TArray<FShaderFunctionDefinition>& OutFunctions) const
{
	// Functions must match those exposed in data interface shader code.
	// todo[CF]: Make these easier to write. Maybe even get from shader code reflection?
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("WritePosition");
		Fn.bHasReturnType = false;
		FShaderParamTypeDefinition Param0 = {};
		Param0.ValueType = FShaderValueType::Get(EShaderFundamentalType::Uint);
		Fn.ParamTypes.Add(Param0);
		FShaderParamTypeDefinition Param1 = {};
		Param1.ValueType = FShaderValueType::Get(EShaderFundamentalType::Float, 3);
		Fn.ParamTypes.Add(Param1);
		OutFunctions.Add(Fn);
	}
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("WriteTangentX");
		Fn.bHasReturnType = false;
		FShaderParamTypeDefinition Param0 = {};
		Param0.ValueType = FShaderValueType::Get(EShaderFundamentalType::Uint);
		Fn.ParamTypes.Add(Param0);
		FShaderParamTypeDefinition Param1 = {};
		Param1.ValueType = FShaderValueType::Get(EShaderFundamentalType::Float, 4);
		Fn.ParamTypes.Add(Param1);
		OutFunctions.Add(Fn);
	}
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("WriteTangentZ");
		Fn.bHasReturnType = false;
		FShaderParamTypeDefinition Param0 = {};
		Param0.ValueType = FShaderValueType::Get(EShaderFundamentalType::Uint);
		Fn.ParamTypes.Add(Param0);
		FShaderParamTypeDefinition Param1 = {};
		Param1.ValueType = FShaderValueType::Get(EShaderFundamentalType::Float, 4);
		Fn.ParamTypes.Add(Param1);
		OutFunctions.Add(Fn);
	}
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("WriteColor");
		Fn.bHasReturnType = false;
		FShaderParamTypeDefinition Param0 = {};
		Param0.ValueType = FShaderValueType::Get(EShaderFundamentalType::Uint);
		Fn.ParamTypes.Add(Param0);
		FShaderParamTypeDefinition Param1 = {};
		Param1.ValueType = FShaderValueType::Get(EShaderFundamentalType::Float, 4);
		Fn.ParamTypes.Add(Param1);
		OutFunctions.Add(Fn);
	}
}

BEGIN_SHADER_PARAMETER_STRUCT(FSkinedMeshWriteDataInterfaceParameters, )
	SHADER_PARAMETER(uint32, NumVertices)
	SHADER_PARAMETER(uint32, OutputStreamStart)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<float>, PositionBufferUAV)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<SNORM float4>, TangentBufferUAV)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<UNORM float4>, ColorBufferUAV)
END_SHADER_PARAMETER_STRUCT()

void USkinnedMeshWriteDataInterface::GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& OutBuilder) const
{
	OutBuilder.AddNestedStruct<FSkinedMeshWriteDataInterfaceParameters>(UID);
}

void USkinnedMeshWriteDataInterface::GetHLSL(FString& OutHLSL) const
{
	OutHLSL += TEXT("#include \"/Plugin/Optimus/Private/DataInterfaceSkinnedMeshWrite.ush\"\n");
}

void USkinnedMeshWriteDataInterface::GetSourceTypes(TArray<UClass*>& OutSourceTypes) const
{
	OutSourceTypes.Add(USkinnedMeshComponent::StaticClass());
}

UComputeDataProvider* USkinnedMeshWriteDataInterface::CreateDataProvider(TArrayView< TObjectPtr<UObject> > InSourceObjects, uint64 InInputMask, uint64 InOutputMask) const
{
	USkinnedMeshWriteDataProvider* Provider = NewObject<USkinnedMeshWriteDataProvider>();
	Provider->OutputMask = InOutputMask;

	if (InSourceObjects.Num() == 1)
	{
		Provider->SkinnedMesh = Cast<USkinnedMeshComponent>(InSourceObjects[0]);
	}

	return Provider;
}


bool USkinnedMeshWriteDataProvider::IsValid() const
{
	return
		SkinnedMesh != nullptr &&
		SkinnedMesh->MeshObject != nullptr;
}

FComputeDataProviderRenderProxy* USkinnedMeshWriteDataProvider::GetRenderProxy()
{
	return new FSkinnedMeshWriteDataProviderProxy(SkinnedMesh, OutputMask);
}


FSkinnedMeshWriteDataProviderProxy::FSkinnedMeshWriteDataProviderProxy(USkinnedMeshComponent* InSkinnedMeshComponent, uint64 InOutputMask)
{
	SkeletalMeshObject = InSkinnedMeshComponent->MeshObject;
	OutputMask = InOutputMask;
}

int32 FSkinnedMeshWriteDataProviderProxy::GetInvocationCount() const
{
 	FSkeletalMeshRenderData const& SkeletalMeshRenderData = SkeletalMeshObject->GetSkeletalMeshRenderData();
 	FSkeletalMeshLODRenderData const* LodRenderData = SkeletalMeshRenderData.GetPendingFirstLOD(0);
 	return LodRenderData->RenderSections.Num();
}

FIntVector FSkinnedMeshWriteDataProviderProxy::GetDispatchDim(int32 InvocationIndex, FIntVector GroupDim) const
{
	// todo[CF]: Need to know which parameter drives the dispatch size. There's quite some complexity here as this relies on much more info from the kernel.
	// Just assume one thread per vertex will drive this for now.
	FSkeletalMeshRenderData const& SkeletalMeshRenderData = SkeletalMeshObject->GetSkeletalMeshRenderData();
	FSkeletalMeshLODRenderData const* LodRenderData = SkeletalMeshRenderData.GetPendingFirstLOD(0);
	FSkelMeshRenderSection const& RenderSection = LodRenderData->RenderSections[InvocationIndex];
	const int32 NumVertices = RenderSection.GetNumVertices();
	const int32 NumGroupThreads = GroupDim.X * GroupDim.Y * GroupDim.Z;
	const int32 NumGroups = FMath::DivideAndRoundUp(NumVertices, NumGroupThreads);
	return FIntVector(NumGroups, 1, 1);
}

void FSkinnedMeshWriteDataProviderProxy::AllocateResources(FRDGBuilder& GraphBuilder)
{
	// Allocate required buffers
	FSkeletalMeshRenderData const& SkeletalMeshRenderData = SkeletalMeshObject->GetSkeletalMeshRenderData();
	const int32 LodIndex = SkeletalMeshRenderData.GetPendingFirstLODIdx(0);
	FSkeletalMeshLODRenderData const* LodRenderData = SkeletalMeshRenderData.GetPendingFirstLOD(0);
	const int32 NumVertices = LodRenderData->GetNumVertices();

	// We will extract buffers from RDG.
	// It could be better to for memory to use QueueBufferExtraction instead of ConvertToExternalBuffer but that will require an extra hook after graph execution.
	TRefCountPtr<FRDGPooledBuffer> PositionBufferExternal;
	TRefCountPtr<FRDGPooledBuffer> TangentBufferExternal;
	TRefCountPtr<FRDGPooledBuffer> ColorBufferExternal;

	if (OutputMask & 1)
	{
		const uint32 PosBufferBytesPerElement = 4;
		PositionBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(PosBufferBytesPerElement, NumVertices * 3), TEXT("SkinnedMeshPositionBuffer"), ERDGBufferFlags::None);
		PositionBufferUAV = GraphBuilder.CreateUAV(PositionBuffer, PF_R32_FLOAT, ERDGUnorderedAccessViewFlags::SkipBarrier);
		PositionBufferExternal = GraphBuilder.ConvertToExternalBuffer(PositionBuffer);
		GraphBuilder.SetBufferAccessFinal(PositionBuffer, ERHIAccess::VertexOrIndexBuffer | ERHIAccess::SRVMask);
	}
	else
	{
		PositionBufferUAV = GraphBuilder.CreateUAV(GraphBuilder.RegisterExternalBuffer(GWhiteVertexBufferWithRDG->Buffer), PF_R32_FLOAT);
	}

	// OpenGL ES does not support writing to RGBA16_SNORM images, instead pack data into SINT in the shader
	const EPixelFormat TangentsFormat = IsOpenGLPlatform(GMaxRHIShaderPlatform) ? PF_R16G16B16A16_SINT : PF_R16G16B16A16_SNORM;

	if (OutputMask & 2)
	{
		const uint32 TangentBufferBytesPerElement = 8;
		TangentBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(TangentBufferBytesPerElement, NumVertices * 2), TEXT("SkinnedMeshTangentBuffer"), ERDGBufferFlags::None);
		TangentBufferUAV = GraphBuilder.CreateUAV(TangentBuffer, TangentsFormat, ERDGUnorderedAccessViewFlags::SkipBarrier);
		TangentBufferExternal = GraphBuilder.ConvertToExternalBuffer(TangentBuffer);
		GraphBuilder.SetBufferAccessFinal(TangentBuffer, ERHIAccess::VertexOrIndexBuffer | ERHIAccess::SRVMask);
	}
	else
	{
		TangentBufferUAV = GraphBuilder.CreateUAV(GraphBuilder.RegisterExternalBuffer(GWhiteVertexBufferWithRDG->Buffer), TangentsFormat);
	}

	if (OutputMask & 8)
	{
		const uint32 ColorBufferBytesPerElement = 4;
		ColorBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(ColorBufferBytesPerElement, NumVertices), TEXT("SkinnedMeshColorBuffer"), ERDGBufferFlags::None);
		ColorBufferUAV = GraphBuilder.CreateUAV(ColorBuffer, PF_B8G8R8A8, ERDGUnorderedAccessViewFlags::SkipBarrier);
		ColorBufferExternal = GraphBuilder.ConvertToExternalBuffer(ColorBuffer);
		GraphBuilder.SetBufferAccessFinal(ColorBuffer, ERHIAccess::VertexOrIndexBuffer | ERHIAccess::SRVMask);
	}
	else
	{
		ColorBufferUAV = GraphBuilder.CreateUAV(GraphBuilder.RegisterExternalBuffer(GWhiteVertexBufferWithRDG->Buffer), PF_B8G8R8A8);
	}

	// Set to vertex factories
	const int32 NumSections = LodRenderData->RenderSections.Num();
	FSkeletalMeshDeformerHelpers::SetVertexFactoryBufferOverrides(SkeletalMeshObject, LodIndex, FSkeletalMeshDeformerHelpers::EOverrideType::Partial, PositionBufferExternal, TangentBufferExternal, ColorBufferExternal);

#if RHI_RAYTRACING
	// This can create RHI resources but it queues and doesn't actually build ray tracing structures. Not sure if we need to put inside a render graph pass?
	// Also note that for ray tracing we may want to support a second graph execution if ray tracing LOD needs to be different to render LOD.
	FSkeletalMeshDeformerHelpers::UpdateRayTracingGeometry(SkeletalMeshObject, LodIndex, PositionBufferExternal);
#endif
}

void FSkinnedMeshWriteDataProviderProxy::GetBindings(int32 InvocationIndex, TCHAR const* UID, FBindings& OutBindings) const
{
	const int32 SectionIdx = InvocationIndex;

	FSkinedMeshWriteDataInterfaceParameters Parameters;
	FMemory::Memset(&Parameters, 0, sizeof(FSkinedMeshWriteDataInterfaceParameters));

	FSkeletalMeshRenderData const& SkeletalMeshRenderData = SkeletalMeshObject->GetSkeletalMeshRenderData();
	FSkeletalMeshLODRenderData const* LodRenderData = SkeletalMeshRenderData.GetPendingFirstLOD(0);
	FSkelMeshRenderSection const& RenderSection = LodRenderData->RenderSections[SectionIdx];

	Parameters.NumVertices = RenderSection.GetNumVertices();
	Parameters.OutputStreamStart = RenderSection.GetVertexBufferIndex();
 	Parameters.PositionBufferUAV = PositionBufferUAV;
	Parameters.TangentBufferUAV = TangentBufferUAV;
	Parameters.ColorBufferUAV = ColorBufferUAV;

	TArray<uint8> ParamData;
	ParamData.SetNum(sizeof(FSkinedMeshWriteDataInterfaceParameters));
	FMemory::Memcpy(ParamData.GetData(), &Parameters, sizeof(FSkinedMeshWriteDataInterfaceParameters));
	OutBindings.Structs.Add(TTuple<FString, TArray<uint8> >(UID, MoveTemp(ParamData)));
}
