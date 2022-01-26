// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataInterfaces/DataInterfaceCloth.h"

#include "Components/SkeletalMeshComponent.h"
#include "ComputeFramework/ComputeKernelPermutationSet.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "OptimusDataDomain.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "ShaderParameterMetadataBuilder.h"
#include "SkeletalMeshDeformerHelpers.h"
#include "SkeletalRenderPublic.h"

FString UClothDataInterface::GetDisplayName() const
{
	return TEXT("Cloth");
}

TArray<FOptimusCDIPinDefinition> UClothDataInterface::GetPinDefinitions() const
{
	TArray<FOptimusCDIPinDefinition> Defs;

	using namespace Optimus::DomainName;

	Defs.Add({ "NumVertices", "ReadNumVertices" });
	Defs.Add({ "ClothToLocal", "ReadClothToLocal" });
	Defs.Add({ "ClothWeight", "ReadClothWeight", Vertex, "ReadNumVertices" });
	Defs.Add({ "ClothPosition", "ReadClothPosition", Vertex, "ReadNumVertices" });
	Defs.Add({ "ClothTangentX", "ReadClothTangentX", Vertex, "ReadNumVertices" });
	Defs.Add({ "ClothTangentZ", "ReadClothTangentZ", Vertex, "ReadNumVertices" });

	return Defs;
}

void UClothDataInterface::GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const
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
		Fn.Name = TEXT("ReadClothToLocal");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = {};
		ReturnParam.ValueType = FShaderValueType::Get(EShaderFundamentalType::Float, 4, 4);
		Fn.ParamTypes.Add(ReturnParam);
		OutFunctions.Add(Fn);
	}
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("ReadClothWeight");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = {};
		ReturnParam.ValueType = FShaderValueType::Get(EShaderFundamentalType::Float);
		Fn.ParamTypes.Add(ReturnParam);
		FShaderParamTypeDefinition Param0 = {};
		Param0.ValueType = FShaderValueType::Get(EShaderFundamentalType::Uint);
		Fn.ParamTypes.Add(Param0);
		OutFunctions.Add(Fn);
	}
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("ReadClothPosition");
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
		Fn.Name = TEXT("ReadClothTangentX");
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
		Fn.Name = TEXT("ReadClothTangentZ");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = {};
		ReturnParam.ValueType = FShaderValueType::Get(EShaderFundamentalType::Float, 3);
		Fn.ParamTypes.Add(ReturnParam);
		FShaderParamTypeDefinition Param0 = {};
		Param0.ValueType = FShaderValueType::Get(EShaderFundamentalType::Uint);
		Fn.ParamTypes.Add(Param0);
		OutFunctions.Add(Fn);
	}
}

BEGIN_SHADER_PARAMETER_STRUCT(FClothDataInterfaceParameters, )
	SHADER_PARAMETER(uint32, NumVertices)
	SHADER_PARAMETER(uint32, InputStreamStart)
	SHADER_PARAMETER(uint32, NumInfluencesPerVertex)
	SHADER_PARAMETER(float, ClothBlendWeight)
	SHADER_PARAMETER(FMatrix44f, ClothToLocal)
	SHADER_PARAMETER_SRV(Buffer<float4>, ClothBuffer)
	SHADER_PARAMETER_SRV(Buffer<float2>, ClothPositionsAndNormalsBuffer)
END_SHADER_PARAMETER_STRUCT()

void UClothDataInterface::GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& OutBuilder) const
{
	OutBuilder.AddNestedStruct<FClothDataInterfaceParameters>(UID);
}

void UClothDataInterface::GetHLSL(FString& OutHLSL) const
{
	OutHLSL += TEXT("#include \"/Plugin/Optimus/Private/DataInterfaceCloth.ush\"\n");
}

void UClothDataInterface::GetSourceTypes(TArray<UClass*>& OutSourceTypes) const
{
	OutSourceTypes.Add(USkeletalMeshComponent::StaticClass());
}

UComputeDataProvider* UClothDataInterface::CreateDataProvider(TArrayView< TObjectPtr<UObject> > InSourceObjects, uint64 InInputMask, uint64 InOutputMask) const
{
	UClothDataProvider* Provider = NewObject<UClothDataProvider>();

	if (InSourceObjects.Num() == 1)
	{
		Provider->SkeletalMesh = Cast<USkeletalMeshComponent>(InSourceObjects[0]);
	}

	return Provider;
}


bool UClothDataProvider::IsValid() const
{
	return
		SkeletalMesh != nullptr &&
		SkeletalMesh->MeshObject != nullptr;
}

FComputeDataProviderRenderProxy* UClothDataProvider::GetRenderProxy()
{
	return new FClothDataProviderProxy(SkeletalMesh);
}


FClothDataProviderProxy::FClothDataProviderProxy(USkeletalMeshComponent* SkeletalMeshComponent)
{
	SkeletalMeshObject = SkeletalMeshComponent->MeshObject;
	ClothBlendWeight = SkeletalMeshComponent->ClothBlendWeight;
	FrameNumber = SkeletalMeshComponent->GetScene()->GetFrameNumber() + 1; // +1 matches the logic for FrameNumberToPrepare in FSkeletalMeshObjectGPUSkin::Update()
}

int32 FClothDataProviderProxy::GetInvocationCount() const
{
	FSkeletalMeshRenderData const& SkeletalMeshRenderData = SkeletalMeshObject->GetSkeletalMeshRenderData();
	FSkeletalMeshLODRenderData const* LodRenderData = SkeletalMeshRenderData.GetPendingFirstLOD(0);
	return LodRenderData->RenderSections.Num();
}

FIntVector FClothDataProviderProxy::GetDispatchDim(int32 InvocationIndex, FIntVector GroupDim) const
{
	// todo[CF]: Need to know which parameter drives the dispatch size. There's quite some complexity here as this relies on much more info from the kernel.
	// Just assume one thread per vertex or triangle (whichever is greater) will drive this for now.
	FSkeletalMeshRenderData const& SkeletalMeshRenderData = SkeletalMeshObject->GetSkeletalMeshRenderData();
	FSkeletalMeshLODRenderData const* LodRenderData = SkeletalMeshRenderData.GetPendingFirstLOD(0);
	FSkelMeshRenderSection const& RenderSection = LodRenderData->RenderSections[InvocationIndex];
	const int32 NumThreads = FMath::Max(RenderSection.NumVertices, RenderSection.NumTriangles);
	const int32 NumGroupThreads = GroupDim.X * GroupDim.Y * GroupDim.Z;
	const int32 NumGroups = FMath::DivideAndRoundUp(NumThreads, NumGroupThreads);
	return FIntVector(NumGroups, 1, 1);
}

void FClothDataProviderProxy::GetPermutations(int32 InvocationIndex, FComputeKernelPermutationSet& OutPermutationSet) const
{
	// todo[CF]: Set permutations required such as ENABLE_DEFORMER_CLOTH
}

void FClothDataProviderProxy::GetBindings(int32 InvocationIndex, TCHAR const* UID, FBindings& OutBindings) const
{
	const int32 SectionIdx = InvocationIndex;

	FClothDataInterfaceParameters Parameters;
	FMemory::Memset(&Parameters, 0, sizeof(Parameters));

	FSkeletalMeshRenderData const& SkeletalMeshRenderData = SkeletalMeshObject->GetSkeletalMeshRenderData();
	FSkeletalMeshLODRenderData const* LodRenderData = SkeletalMeshRenderData.GetPendingFirstLOD(0);
	FSkelMeshRenderSection const& RenderSection = LodRenderData->RenderSections[SectionIdx];

	const uint32 NumWrapDeformerWeights = RenderSection.ClothMappingDataLODs.Num() > 0 ? RenderSection.ClothMappingDataLODs[0].Num() : 0;
	const bool bMultipleWrapDeformerInfluences = RenderSection.NumVertices < NumWrapDeformerWeights;
	const int32 NumClothInfluencesPerVertex = bMultipleWrapDeformerInfluences ? 5 : 1; // From ClothingMeshUtils.cpp. Could make this a permutation like with skin cache.

	const int32 LodIndex = SkeletalMeshRenderData.GetPendingFirstLODIdx(0);
	const bool bPreviousFrame = false;
	FSkeletalMeshDeformerHelpers::FClothBuffers ClothBuffers = FSkeletalMeshDeformerHelpers::GetClothBuffersForReading(SkeletalMeshObject, LodIndex, SectionIdx, FrameNumber, bPreviousFrame);
	const bool bValidCloth = (ClothBuffers.ClothInfluenceBuffer != nullptr) && (ClothBuffers.ClothSimulatedPositionAndNormalBuffer != nullptr);

	FRHIShaderResourceView* NullSRVBinding = GWhiteVertexBufferWithSRV->ShaderResourceViewRHI.GetReference();

	Parameters.NumVertices = RenderSection.NumVertices;
	Parameters.InputStreamStart = ClothBuffers.ClothInfluenceBufferOffset;
 	Parameters.ClothBlendWeight = bValidCloth ? ClothBlendWeight : 0.f;
	Parameters.NumInfluencesPerVertex = bValidCloth ? NumClothInfluencesPerVertex : 0;
	Parameters.ClothToLocal = ClothBuffers.ClothToLocal;
 	Parameters.ClothBuffer = bValidCloth ? ClothBuffers.ClothInfluenceBuffer : NullSRVBinding;
 	Parameters.ClothPositionsAndNormalsBuffer = bValidCloth ? ClothBuffers.ClothSimulatedPositionAndNormalBuffer : NullSRVBinding;

	TArray<uint8> ParamData;
	ParamData.SetNum(sizeof(Parameters));
	FMemory::Memcpy(ParamData.GetData(), &Parameters, sizeof(Parameters));
	OutBindings.Structs.Add(TTuple<FString, TArray<uint8> >(UID, MoveTemp(ParamData)));
}
