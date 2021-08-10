// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataInterfaces/DataInterfaceSkinCacheWrite.h"

#include "Components/SkeletalMeshComponent.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "GPUSkinCache.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "ShaderParameterMetadataBuilder.h"
#include "SkeletalRenderPublic.h"

FString USkeletalMeshSkinCacheDataInterface::GetDisplayName() const
{
	return TEXT("Write Skeletal Mesh");
}

TArray<FOptimusCDIPinDefinition> USkeletalMeshSkinCacheDataInterface::GetPinDefinitions() const
{
	TArray<FOptimusCDIPinDefinition> Defs;
	Defs.Add({"Position", "WritePosition", "ReadNumVertices", "Vertex"});
	Defs.Add({"TangentX", "WriteTangentX", "ReadNumVertices", "Vertex"});
	Defs.Add({"TangentZ", "WriteTangentZ", "ReadNumVertices", "Vertex"});

	return Defs;
}

void USkeletalMeshSkinCacheDataInterface::GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const
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

void USkeletalMeshSkinCacheDataInterface::GetSupportedOutputs(TArray<FShaderFunctionDefinition>& OutFunctions) const
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
}

BEGIN_SHADER_PARAMETER_STRUCT(FSkinCacheWriteDataInterfaceParameters, )
	SHADER_PARAMETER(uint32, NumVertices)
	SHADER_PARAMETER(uint32, OutputStreamStart)
	SHADER_PARAMETER_UAV(RWBuffer<float>, PositionBufferUAV)
	SHADER_PARAMETER_UAV(RWBuffer<SNORM float4>, TangentBufferUAV)
END_SHADER_PARAMETER_STRUCT()

void USkeletalMeshSkinCacheDataInterface::GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& OutBuilder) const
{
	OutBuilder.AddNestedStruct<FSkinCacheWriteDataInterfaceParameters>(UID);
}

void USkeletalMeshSkinCacheDataInterface::GetHLSL(FString& OutHLSL) const
{
	OutHLSL += TEXT("#include \"/Plugin/Optimus/Private/DataInterfaceSkinCacheWrite.ush\"\n");
}

void USkeletalMeshSkinCacheDataInterface::GetSourceTypes(TArray<UClass*>& OutSourceTypes) const
{
	OutSourceTypes.Add(USkeletalMeshComponent::StaticClass());
}

UComputeDataProvider* USkeletalMeshSkinCacheDataInterface::CreateDataProvider(UObject* InOuter, TArrayView< TObjectPtr<UObject> > InSourceObjects) const
{
	USkeletalMeshSkinCacheDataProvider* Provider = NewObject<USkeletalMeshSkinCacheDataProvider>(InOuter);

	if (InSourceObjects.Num() == 1)
	{
		Provider->SkeletalMesh = Cast<USkeletalMeshComponent>(InSourceObjects[0]);
	}

	return Provider;
}


bool USkeletalMeshSkinCacheDataProvider::IsValid() const
{
	return
		SkeletalMesh != nullptr &&
		SkeletalMesh->MeshObject != nullptr &&
		SkeletalMesh->GetScene() != nullptr &&
		SkeletalMesh->GetScene()->GetGPUSkinCache() != nullptr;
}

FComputeDataProviderRenderProxy* USkeletalMeshSkinCacheDataProvider::GetRenderProxy()
{
	return new FSkeletalMeshSkinCacheDataProviderProxy(SkeletalMesh);
}


FSkeletalMeshSkinCacheDataProviderProxy::FSkeletalMeshSkinCacheDataProviderProxy(USkeletalMeshComponent* SkeletalMeshComponent)
{
	SkeletalMeshObject = SkeletalMeshComponent->MeshObject;
	GPUSkinCache = SkeletalMeshComponent->GetScene()->GetGPUSkinCache();
}

int32 FSkeletalMeshSkinCacheDataProviderProxy::GetInvocationCount() const
{
	FSkeletalMeshRenderData const& SkeletalMeshRenderData = SkeletalMeshObject->GetSkeletalMeshRenderData();
	FSkeletalMeshLODRenderData const* LodRenderData = SkeletalMeshRenderData.GetPendingFirstLOD(0);
	return LodRenderData->RenderSections.Num();
}

FIntVector FSkeletalMeshSkinCacheDataProviderProxy::GetDispatchDim(int32 InvocationIndex, FIntVector GroupDim) const
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

void FSkeletalMeshSkinCacheDataProviderProxy::GetBindings(int32 InvocationIndex, TCHAR const* UID, FBindings& OutBindings) const
{
	const int32 SectionIdx = InvocationIndex;

	FSkinCacheWriteDataInterfaceParameters Parameters;
	FMemory::Memset(&Parameters, 0, sizeof(FSkinCacheWriteDataInterfaceParameters));

	FSkeletalMeshRenderData const& SkeletalMeshRenderData = SkeletalMeshObject->GetSkeletalMeshRenderData();
	FSkeletalMeshLODRenderData const* LodRenderData = SkeletalMeshRenderData.GetPendingFirstLOD(0);
	FSkelMeshRenderSection const& RenderSection = LodRenderData->RenderSections[SectionIdx];

	FRWBuffer* OutputPositionBuffer = GPUSkinCache->GetPositionBuffer(SkeletalMeshObject->GetComponentId(), SectionIdx);
	FRWBuffer* OutputTangentBuffer = GPUSkinCache->GetTangentBuffer(SkeletalMeshObject->GetComponentId(), SectionIdx);
	if (!ensure(OutputPositionBuffer != nullptr && OutputTangentBuffer != nullptr))
	{
		// Don't return a binding. Will trigger another ensure and fail to dispatch shader.
		return;
	}

	Parameters.NumVertices = RenderSection.GetNumVertices();
	Parameters.OutputStreamStart = RenderSection.GetVertexBufferIndex();
	Parameters.PositionBufferUAV = OutputPositionBuffer->UAV;
	Parameters.TangentBufferUAV = OutputTangentBuffer->UAV;

	TArray<uint8> ParamData;
	ParamData.SetNum(sizeof(FSkinCacheWriteDataInterfaceParameters));
	FMemory::Memcpy(ParamData.GetData(), &Parameters, sizeof(FSkinCacheWriteDataInterfaceParameters));
	OutBindings.Structs.Add(TTuple<FString, TArray<uint8> >(UID, MoveTemp(ParamData)));
}
