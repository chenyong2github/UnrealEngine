// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataInterfaceSkeletalMeshRead.h"

#include "Components/SkeletalMeshComponent.h"
#include "ComputeFramework/ComputeKernelPermutationSet.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "GPUSkinCache.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "ShaderParameterMetadataBuilder.h"
#include "SkeletalRenderPublic.h"

void USkeletalMeshReadDataInterface::GetPermutations(FComputeKernelPermutationSet& OutPermutationSet) const
{
	// Need to be able to support these permutations according to the skeletal mesh settings.
	OutPermutationSet.BooleanOptions.Add(FComputeKernelPermutationBool(TEXT("GPUSKIN_UNLIMITED_BONE_INFLUENCE")));
	OutPermutationSet.BooleanOptions.Add(FComputeKernelPermutationBool(TEXT("GPUSKIN_BONE_INDEX_UINT16")));
}

void USkeletalMeshReadDataInterface::GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const
{
	// Functions must match those exposed in data interface shader code.
	// todo[CF]: Make these easier to write. Maybe even get from shader code reflection?
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("ReadNumVertices");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = {};
		ReturnParam.FundamentalType = EShaderFundamentalType::Uint;
		ReturnParam.DimType = EShaderFundamentalDimensionType::Scalar;
		Fn.ParamTypes.Add(ReturnParam);
		OutFunctions.Add(Fn);
	}
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("ReadNumTriangles");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = {};
		ReturnParam.FundamentalType = EShaderFundamentalType::Uint;
		ReturnParam.DimType = EShaderFundamentalDimensionType::Scalar;
		Fn.ParamTypes.Add(ReturnParam);
		OutFunctions.Add(Fn);
	}
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("ReadPosition");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = {};
		ReturnParam.FundamentalType = EShaderFundamentalType::Float;
		ReturnParam.DimType = EShaderFundamentalDimensionType::Vector;
		ReturnParam.VectorDimension = 3;
		Fn.ParamTypes.Add(ReturnParam);
		FShaderParamTypeDefinition Param0 = {};
		Param0.FundamentalType = EShaderFundamentalType::Uint;
		Param0.DimType = EShaderFundamentalDimensionType::Scalar;
		Fn.ParamTypes.Add(Param0);
		OutFunctions.Add(Fn);
	}
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("ReadTangentX");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = {};
		ReturnParam.FundamentalType = EShaderFundamentalType::Float;
		ReturnParam.DimType = EShaderFundamentalDimensionType::Vector;
		ReturnParam.VectorDimension = 4;
		Fn.ParamTypes.Add(ReturnParam);
		FShaderParamTypeDefinition Param0 = {};
		Param0.FundamentalType = EShaderFundamentalType::Uint;
		Param0.DimType = EShaderFundamentalDimensionType::Scalar;
		Fn.ParamTypes.Add(Param0);
		OutFunctions.Add(Fn);
	}
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("ReadTangentZ");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = {};
		ReturnParam.FundamentalType = EShaderFundamentalType::Float;
		ReturnParam.DimType = EShaderFundamentalDimensionType::Vector;
		ReturnParam.VectorDimension = 4;
		Fn.ParamTypes.Add(ReturnParam);
		FShaderParamTypeDefinition Param0 = {};
		Param0.FundamentalType = EShaderFundamentalType::Uint;
		Param0.DimType = EShaderFundamentalDimensionType::Scalar;
		Fn.ParamTypes.Add(Param0);
		OutFunctions.Add(Fn);
	}
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("ReadUV");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = {};
		ReturnParam.FundamentalType = EShaderFundamentalType::Float;
		ReturnParam.DimType = EShaderFundamentalDimensionType::Vector;
		ReturnParam.VectorDimension = 2;
		Fn.ParamTypes.Add(ReturnParam);
		FShaderParamTypeDefinition Param0 = {};
		Param0.FundamentalType = EShaderFundamentalType::Uint;
		Param0.DimType = EShaderFundamentalDimensionType::Scalar;
		Fn.ParamTypes.Add(Param0);
		OutFunctions.Add(Fn);
	}
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("ReadBoneMatrix");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = {};
		ReturnParam.FundamentalType = EShaderFundamentalType::Float;
		ReturnParam.DimType = EShaderFundamentalDimensionType::Matrix;
		ReturnParam.MatrixRowCount = 3;
		ReturnParam.MatrixColumnCount = 4;
		Fn.ParamTypes.Add(ReturnParam);
		FShaderParamTypeDefinition Param0 = {};
		Param0.FundamentalType = EShaderFundamentalType::Uint;
		Param0.DimType = EShaderFundamentalDimensionType::Scalar;
		Fn.ParamTypes.Add(Param0);
		OutFunctions.Add(Fn);
	}
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("ReadBlendMatrix");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = {};
		ReturnParam.FundamentalType = EShaderFundamentalType::Float;
		ReturnParam.DimType = EShaderFundamentalDimensionType::Matrix;
		ReturnParam.MatrixRowCount = 3;
		ReturnParam.MatrixColumnCount = 4;
		Fn.ParamTypes.Add(ReturnParam);
		FShaderParamTypeDefinition Param0 = {};
		Param0.FundamentalType = EShaderFundamentalType::Uint;
		Param0.DimType = EShaderFundamentalDimensionType::Scalar;
		Fn.ParamTypes.Add(Param0);
		OutFunctions.Add(Fn);
	}
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("ReadTriangleVertexIndices");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = {};
		ReturnParam.FundamentalType = EShaderFundamentalType::Uint;
		ReturnParam.DimType = EShaderFundamentalDimensionType::Vector;
		ReturnParam.VectorDimension = 3;
		Fn.ParamTypes.Add(ReturnParam);
		FShaderParamTypeDefinition Param0 = {};
		Param0.FundamentalType = EShaderFundamentalType::Uint;
		Param0.DimType = EShaderFundamentalDimensionType::Scalar;
		Fn.ParamTypes.Add(Param0);
		OutFunctions.Add(Fn);
	}
}

BEGIN_SHADER_PARAMETER_STRUCT(FSkeletalMeshReadDataInterfaceParameters, )
	SHADER_PARAMETER(uint32, NumVertices)
	SHADER_PARAMETER(uint32, NumTriangles)
	SHADER_PARAMETER(uint32, InputStreamStart)
	SHADER_PARAMETER(uint32, InputWeightStart)
	SHADER_PARAMETER(uint32, InputWeightStride)
	SHADER_PARAMETER(uint32, InputWeightIndexSize)
	SHADER_PARAMETER(uint32, IndexBufferStart)
	SHADER_PARAMETER_SRV(Buffer<float>, PositionInputBuffer)
	SHADER_PARAMETER_SRV(Buffer<SNORM float4>, TangentInputBuffer)
	SHADER_PARAMETER_SRV(Buffer<float2>, UVInputBuffer)
	SHADER_PARAMETER_SRV(Buffer<float4>, BoneMatrices)
	SHADER_PARAMETER_SRV(Buffer<uint>, InputWeightStream)
	SHADER_PARAMETER_SRV(Buffer<uint>, InputWeightLookupStream)
	SHADER_PARAMETER_SRV(Buffer<uint>, IndexBuffer)
END_SHADER_PARAMETER_STRUCT()

void USkeletalMeshReadDataInterface::GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& OutBuilder) const
{
	OutBuilder.AddNestedStruct<FSkeletalMeshReadDataInterfaceParameters>(UID);
}

void USkeletalMeshReadDataInterface::GetHLSL(FString& OutHLSL) const
{
	OutHLSL += TEXT("#include \"/Plugin/Optimus/Private/DataInterfaceSkeletalMeshRead.ush\"\n");
}

FComputeDataProviderRenderProxy* USkeletalMeshReadDataProvider::GetRenderProxy()
{
	return new FSkeletalMeshReadDataProviderProxy(SkeletalMesh);
}

FSkeletalMeshReadDataProviderProxy::FSkeletalMeshReadDataProviderProxy(USkeletalMeshComponent* SkeletalMeshComponent)
{
	SkeletalMeshObject = SkeletalMeshComponent != nullptr ? SkeletalMeshComponent->MeshObject : nullptr;
	
	// todo[CF]: Remove GPUSkinCache access for this provider. It's only used as a hack to get latest bone matrices.
	FSceneInterface* Scene = SkeletalMeshComponent != nullptr ? SkeletalMeshComponent->GetScene() : nullptr;
	GPUSkinCache = Scene != nullptr ? Scene->GetGPUSkinCache() : nullptr;
}

int32 FSkeletalMeshReadDataProviderProxy::GetInvocationCount() const
{
	if (SkeletalMeshObject == nullptr || GPUSkinCache == nullptr)
	{
		return 0;
	}

	FSkeletalMeshRenderData const& SkeletalMeshRenderData = SkeletalMeshObject->GetSkeletalMeshRenderData();
	FSkeletalMeshLODRenderData const* LodRenderData = SkeletalMeshRenderData.GetPendingFirstLOD(0);
	return LodRenderData->RenderSections.Num();
}

FIntVector FSkeletalMeshReadDataProviderProxy::GetDispatchDim(int32 InvocationIndex, FIntVector GroupDim) const
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

void FSkeletalMeshReadDataProviderProxy::GetPermutations(int32 InvocationIndex, FComputeKernelPermutationSet& OutPermutationSet) const
{
	// todo[CF]: Set permutations required for our skeletal mesh.
}

void FSkeletalMeshReadDataProviderProxy::GetBindings(int32 InvocationIndex, TCHAR const* UID, FBindings& OutBindings) const
{
	const int32 SectionIdx = InvocationIndex;

	FSkeletalMeshRenderData const& SkeletalMeshRenderData = SkeletalMeshObject->GetSkeletalMeshRenderData();
	FSkeletalMeshLODRenderData const* LodRenderData = SkeletalMeshRenderData.GetPendingFirstLOD(0);
	FSkelMeshRenderSection const& RenderSection = LodRenderData->RenderSections[SectionIdx];

	FRHIShaderResourceView* MeshVertexBufferSRV = LodRenderData->StaticVertexBuffers.PositionVertexBuffer.GetSRV();
	FRHIShaderResourceView* MeshTangentBufferSRV = LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.GetTangentsSRV();

	FSkinWeightVertexBuffer const* WeightBuffer = LodRenderData->GetSkinWeightVertexBuffer();
	FRHIShaderResourceView* SkinWeightBufferSRV = WeightBuffer->GetDataVertexBuffer()->GetSRV();
	const bool bUnlimitedBoneInfluences = FGPUBaseSkinVertexFactory::GetUnlimitedBoneInfluences();
	FRHIShaderResourceView* InputWeightLookupStreamSRV = bUnlimitedBoneInfluences ? WeightBuffer->GetLookupVertexBuffer()->GetSRV() : nullptr;
	
	const TArray<FMatrix>& RefToLocals = SkeletalMeshObject->GetReferenceToLocalMatrices();
	FRHIShaderResourceView* BoneBufferSRV = GPUSkinCache->GetBoneBuffer(SkeletalMeshObject->GetComponentId(), SectionIdx);

	FSkeletalMeshReadDataInterfaceParameters Parameters;
	FMemory::Memset(&Parameters, 0, sizeof(FSkeletalMeshReadDataInterfaceParameters));

	Parameters.NumVertices = RenderSection.GetNumVertices();
	//Parameters.NumTriangles;
	Parameters.InputStreamStart = RenderSection.GetVertexBufferIndex();
	Parameters.InputWeightStart = (WeightBuffer->GetConstantInfluencesVertexStride() * RenderSection.GetVertexBufferIndex()) / sizeof(float);
	Parameters.InputWeightStride = WeightBuffer->GetConstantInfluencesVertexStride();
	Parameters.InputWeightIndexSize = WeightBuffer->GetBoneIndexByteSize();
	//Parameters.IndexBufferStart;
	Parameters.PositionInputBuffer = MeshVertexBufferSRV;
	Parameters.TangentInputBuffer = MeshTangentBufferSRV;
	//Parameters.UVInputBuffer;
	Parameters.BoneMatrices = BoneBufferSRV;
	Parameters.InputWeightStream = SkinWeightBufferSRV;
	Parameters.InputWeightLookupStream = InputWeightLookupStreamSRV;
	//Parameters.IndexBuffer;

	TArray<uint8> ParamData;
	ParamData.SetNum(sizeof(FSkeletalMeshReadDataInterfaceParameters));
	FMemory::Memcpy(ParamData.GetData(), &Parameters, sizeof(FSkeletalMeshReadDataInterfaceParameters));
	OutBindings.Structs.Add(TTuple<FString, TArray<uint8> >(UID, MoveTemp(ParamData)));
}
