// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataInterfaces/DataInterfaceSkinnedMesh.h"

#include "Components/SkeletalMeshComponent.h"
#include "ComputeFramework/ComputeKernelPermutationSet.h"
#include "ComputeFramework/ComputeKernelPermutationVector.h"
#include "ComputeFramework/ShaderParameterMetadataBuilder.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "OptimusDataDomain.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SkeletalRenderPublic.h"

FString USkinnedMeshDataInterface::GetDisplayName() const
{
	return TEXT("Skinned Mesh");
}

TArray<FOptimusCDIPinDefinition> USkinnedMeshDataInterface::GetPinDefinitions() const
{
	TArray<FOptimusCDIPinDefinition> Defs;

	using namespace Optimus::DomainName;

	Defs.Add({"NumVertices", "ReadNumVertices"});
	Defs.Add({"Position", "ReadPosition", Vertex, "ReadNumVertices"});
	Defs.Add({"TangentX", "ReadTangentX", Vertex, "ReadNumVertices"});
	Defs.Add({"TangentZ", "ReadTangentZ", Vertex, "ReadNumVertices"});
	Defs.Add({"NumUVChannels", "ReadNumUVChannels"});
	Defs.Add({"UV", "ReadUV", {{Vertex, "ReadNumVertices"}, {UVChannel, "ReadNumUVChannels"}}});
	Defs.Add({"NumTriangles", "ReadNumTriangles"});
	Defs.Add({"IndexBuffer", "ReadIndexBuffer", Triangle, "ReadNumTriangles"});

	return Defs;
}

void USkinnedMeshDataInterface::GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const
{
	// Functions must match those exposed in data interface shader code.
	// todo[CF]: Make these easier to write. Maybe even get from shader code reflection?
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("ReadNumVertices");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = {};
		ReturnParam.ValueType = FShaderValueType::Get(EShaderFundamentalType::Uint);
		Fn.ParamTypes.Add(ReturnParam);
		OutFunctions.Add(Fn);
	}
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("ReadNumTriangles");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = {};
		ReturnParam.ValueType = FShaderValueType::Get(EShaderFundamentalType::Uint);
		Fn.ParamTypes.Add(ReturnParam);
		OutFunctions.Add(Fn);
	}
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("ReadNumUVChannels");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = {};
		ReturnParam.ValueType = FShaderValueType::Get(EShaderFundamentalType::Uint);
		Fn.ParamTypes.Add(ReturnParam);
		OutFunctions.Add(Fn);
	}
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("ReadIndexBuffer");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = {};
		ReturnParam.ValueType = FShaderValueType::Get(EShaderFundamentalType::Uint);
		Fn.ParamTypes.Add(ReturnParam);
		FShaderParamTypeDefinition Param0 = {};
		Param0.ValueType = FShaderValueType::Get(EShaderFundamentalType::Uint);
		Fn.ParamTypes.Add(Param0);
		OutFunctions.Add(Fn);
	}
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("ReadPosition");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = {};
		ReturnParam.ValueType = FShaderValueType::Get(EShaderFundamentalType::Float, 3);
		Fn.ParamTypes.Add(ReturnParam);
		FShaderParamTypeDefinition Param0 = {};
		Param0.ValueType = FShaderValueType::Get(EShaderFundamentalType::Uint);
		Fn.ParamTypes.Add(Param0);
		OutFunctions.Add(Fn);
	}
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("ReadTangentX");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = {};
		ReturnParam.ValueType = FShaderValueType::Get(EShaderFundamentalType::Float, 4);
		Fn.ParamTypes.Add(ReturnParam);
		FShaderParamTypeDefinition Param0 = {};
		Param0.ValueType = FShaderValueType::Get(EShaderFundamentalType::Uint);
		Fn.ParamTypes.Add(Param0);
		OutFunctions.Add(Fn);
	}
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("ReadTangentZ");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = {};
		ReturnParam.ValueType = FShaderValueType::Get(EShaderFundamentalType::Float, 4);
		Fn.ParamTypes.Add(ReturnParam);
		FShaderParamTypeDefinition Param0 = {};
		Param0.ValueType = FShaderValueType::Get(EShaderFundamentalType::Uint);
		Fn.ParamTypes.Add(Param0);
		OutFunctions.Add(Fn);
	}
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("ReadUV");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = {};
		ReturnParam.ValueType = FShaderValueType::Get(EShaderFundamentalType::Float, 2);
		Fn.ParamTypes.Add(ReturnParam);
		FShaderParamTypeDefinition Param0 = {};
		Param0.ValueType = FShaderValueType::Get(EShaderFundamentalType::Uint);
		Fn.ParamTypes.Add(Param0);
		FShaderParamTypeDefinition Param1 = {};
		Param0.ValueType = FShaderValueType::Get(EShaderFundamentalType::Uint);
		Fn.ParamTypes.Add(Param1);
		OutFunctions.Add(Fn);
	}
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("ReadDuplicatedIndicesStart");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = {};
		ReturnParam.ValueType = FShaderValueType::Get(EShaderFundamentalType::Uint);
		Fn.ParamTypes.Add(ReturnParam);
		FShaderParamTypeDefinition Param0 = {};
		Param0.ValueType = FShaderValueType::Get(EShaderFundamentalType::Uint);
		Fn.ParamTypes.Add(Param0);
		OutFunctions.Add(Fn);
	}
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("ReadDuplicatedIndicesLength");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = {};
		ReturnParam.ValueType = FShaderValueType::Get(EShaderFundamentalType::Uint);
		Fn.ParamTypes.Add(ReturnParam);
		FShaderParamTypeDefinition Param0 = {};
		Param0.ValueType = FShaderValueType::Get(EShaderFundamentalType::Uint);
		Fn.ParamTypes.Add(Param0);
		OutFunctions.Add(Fn);
	}
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("ReadDuplicatedIndex");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = {};
		ReturnParam.ValueType = FShaderValueType::Get(EShaderFundamentalType::Uint);
		Fn.ParamTypes.Add(ReturnParam);
		FShaderParamTypeDefinition Param0 = {};
		Param0.ValueType = FShaderValueType::Get(EShaderFundamentalType::Uint);
		Fn.ParamTypes.Add(Param0);
		OutFunctions.Add(Fn);
	}
}

BEGIN_SHADER_PARAMETER_STRUCT(FSkinnedMeshDataInterfaceParameters, )
	SHADER_PARAMETER(uint32, NumVertices)
	SHADER_PARAMETER(uint32, NumTriangles)
	SHADER_PARAMETER(uint32, NumUVChannels)
	SHADER_PARAMETER(uint32, IndexBufferStart)
	SHADER_PARAMETER(uint32, InputStreamStart)
	SHADER_PARAMETER_SRV(Buffer<uint>, IndexBuffer)
	SHADER_PARAMETER_SRV(Buffer<float>, PositionInputBuffer)
	SHADER_PARAMETER_SRV(Buffer<SNORM float4>, TangentInputBuffer)
	SHADER_PARAMETER_SRV(Buffer<float2>, UVInputBuffer)
	SHADER_PARAMETER_SRV(Buffer<uint>, DuplicatedIndicesIndices)
	SHADER_PARAMETER_SRV(Buffer<uint>, DuplicatedIndices)
END_SHADER_PARAMETER_STRUCT()

void USkinnedMeshDataInterface::GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& OutBuilder) const
{
	OutBuilder.AddNestedStruct<FSkinnedMeshDataInterfaceParameters>(UID);
}

void USkinnedMeshDataInterface::GetHLSL(FString& OutHLSL) const
{
	OutHLSL += TEXT("#include \"/Plugin/Optimus/Private/DataInterfaceSkinnedMesh.ush\"\n");
}

void USkinnedMeshDataInterface::GetSourceTypes(TArray<UClass*>& OutSourceTypes) const
{
	OutSourceTypes.Add(USkinnedMeshComponent::StaticClass());
}

UComputeDataProvider* USkinnedMeshDataInterface::CreateDataProvider(TArrayView< TObjectPtr<UObject> > InSourceObjects, uint64 InInputMask, uint64 InOutputMask) const
{
	USkinnedMeshDataProvider* Provider = NewObject<USkinnedMeshDataProvider>();

	if (InSourceObjects.Num() == 1)
	{
		Provider->SkinnedMesh = Cast<USkinnedMeshComponent>(InSourceObjects[0]);
	}

	return Provider;
}


bool USkinnedMeshDataProvider::IsValid() const
{
	return
		SkinnedMesh != nullptr &&
		SkinnedMesh->MeshObject != nullptr;
}

FComputeDataProviderRenderProxy* USkinnedMeshDataProvider::GetRenderProxy()
{
	return new FSkinnedMeshDataProviderProxy(SkinnedMesh);
}


FSkinnedMeshDataProviderProxy::FSkinnedMeshDataProviderProxy(USkinnedMeshComponent* SkinnedMeshComponent)
{
	SkeletalMeshObject = SkinnedMeshComponent->MeshObject;
}

void FSkinnedMeshDataProviderProxy::GatherDispatchData(FDispatchSetup const& InDispatchSetup, FCollectedDispatchData& InOutDispatchData)
{
	if (!ensure(InDispatchSetup.ParameterStructSizeForValidation == sizeof(FSkinnedMeshDataInterfaceParameters)))
	{
		return;
	}

	FSkeletalMeshRenderData const& SkeletalMeshRenderData = SkeletalMeshObject->GetSkeletalMeshRenderData();
	FSkeletalMeshLODRenderData const* LodRenderData = SkeletalMeshRenderData.GetPendingFirstLOD(0);
	if (!ensure(LodRenderData->RenderSections.Num() == InDispatchSetup.NumInvocations))
	{
		return;
	}

	FRHIShaderResourceView* NullSRVBinding = GWhiteVertexBufferWithSRV->ShaderResourceViewRHI.GetReference();

	for (int32 InvocationIndex = 0; InvocationIndex < InDispatchSetup.NumInvocations; ++InvocationIndex)
	{
		FSkelMeshRenderSection const& RenderSection = LodRenderData->RenderSections[InvocationIndex];

		FRHIShaderResourceView* IndexBufferSRV = LodRenderData->MultiSizeIndexContainer.GetIndexBuffer()->GetSRV();
		FRHIShaderResourceView* MeshVertexBufferSRV = LodRenderData->StaticVertexBuffers.PositionVertexBuffer.GetSRV();
		FRHIShaderResourceView* MeshTangentBufferSRV = LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.GetTangentsSRV();
		FRHIShaderResourceView* MeshUVBufferSRV = LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.GetTexCoordsSRV();

		FRHIShaderResourceView* DuplicatedIndicesIndicesSRV = RenderSection.DuplicatedVerticesBuffer.LengthAndIndexDuplicatedVerticesIndexBuffer.VertexBufferSRV;
		FRHIShaderResourceView* DuplicatedIndicesSRV = RenderSection.DuplicatedVerticesBuffer.DuplicatedVerticesIndexBuffer.VertexBufferSRV;
		const bool bValidDuplicatedIndices = (DuplicatedIndicesIndicesSRV != nullptr) && (DuplicatedIndicesSRV != nullptr);

		FSkinnedMeshDataInterfaceParameters* Parameters = (FSkinnedMeshDataInterfaceParameters*)(InOutDispatchData.ParameterBuffer + InDispatchSetup.ParameterBufferOffset + InDispatchSetup.ParameterBufferStride * InvocationIndex);
		Parameters->NumVertices = RenderSection.NumVertices;
		Parameters->NumTriangles = RenderSection.NumTriangles;
		Parameters->NumUVChannels = LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords();
		Parameters->IndexBufferStart = RenderSection.BaseIndex;
		Parameters->InputStreamStart = RenderSection.BaseVertexIndex;
		Parameters->IndexBuffer = IndexBufferSRV != nullptr ? IndexBufferSRV : NullSRVBinding;
		Parameters->PositionInputBuffer = MeshVertexBufferSRV != nullptr ? MeshVertexBufferSRV : NullSRVBinding;
		Parameters->TangentInputBuffer = MeshTangentBufferSRV != nullptr ? MeshTangentBufferSRV : NullSRVBinding;
		Parameters->UVInputBuffer = MeshUVBufferSRV != nullptr ? MeshUVBufferSRV : NullSRVBinding;
		Parameters->DuplicatedIndicesIndices = DuplicatedIndicesIndicesSRV != nullptr ? DuplicatedIndicesIndicesSRV : NullSRVBinding;
		Parameters->DuplicatedIndices = DuplicatedIndicesSRV != nullptr ? DuplicatedIndicesSRV : NullSRVBinding;
	}
}
