// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataInterfaces/DataInterfaceSkeleton.h"

#include "Components/SkeletalMeshComponent.h"
#include "ComputeFramework/ComputeKernelPermutationSet.h"
#include "ComputeFramework/ComputeKernelPermutationVector.h"
#include "ComputeFramework/ShaderParameterMetadataBuilder.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "OptimusDataDomain.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SkeletalMeshDeformerHelpers.h"
#include "SkeletalRenderPublic.h"

FString USkeletonDataInterface::GetDisplayName() const
{
	return TEXT("Skeleton");
}

TArray<FOptimusCDIPinDefinition> USkeletonDataInterface::GetPinDefinitions() const
{
	TArray<FOptimusCDIPinDefinition> Defs;

	using namespace Optimus::DomainName;

	Defs.Add({"NumBones", "ReadNumBones", Vertex, "ReadNumVertices"});
	Defs.Add({"BoneMatrix", "ReadBoneMatrix", {{Vertex, "ReadNumVertices"}, {Bone, "ReadNumBones"}}});
	Defs.Add({"BoneWeight", "ReadBoneWeight", {{Vertex, "ReadNumVertices"}, {Bone, "ReadNumBones"}}});
	Defs.Add({"WeightedBoneMatrix", "ReadWeightedBoneMatrix", Vertex, "ReadNumVertices" });

	return Defs;
}

void USkeletonDataInterface::GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const
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
		Fn.Name = TEXT("ReadNumBones");
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
		Fn.Name = TEXT("ReadBoneMatrix");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = {};
		ReturnParam.ValueType = FShaderValueType::Get(EShaderFundamentalType::Float, 3, 4);
		Fn.ParamTypes.Add(ReturnParam);
		FShaderParamTypeDefinition Param0 = {};
		Param0.ValueType = FShaderValueType::Get(EShaderFundamentalType::Uint);
		Fn.ParamTypes.Add(Param0);
		FShaderParamTypeDefinition Param1 = {};
		Param1.ValueType = FShaderValueType::Get(EShaderFundamentalType::Uint);
		Fn.ParamTypes.Add(Param1);
		OutFunctions.Add(Fn);
	}
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("ReadBoneWeight");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = {};
		ReturnParam.ValueType = FShaderValueType::Get(EShaderFundamentalType::Float);
		Fn.ParamTypes.Add(ReturnParam);
		FShaderParamTypeDefinition Param0 = {};
		Param0.ValueType = FShaderValueType::Get(EShaderFundamentalType::Uint);
		Fn.ParamTypes.Add(Param0);
		FShaderParamTypeDefinition Param1 = {};
		Param1.ValueType = FShaderValueType::Get(EShaderFundamentalType::Uint);
		Fn.ParamTypes.Add(Param1);
		OutFunctions.Add(Fn);
	}
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("ReadWeightedBoneMatrix");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = {};
		ReturnParam.ValueType = FShaderValueType::Get(EShaderFundamentalType::Float, 3, 4);
		Fn.ParamTypes.Add(ReturnParam);
		FShaderParamTypeDefinition Param0 = {};
		Param0.ValueType = FShaderValueType::Get(EShaderFundamentalType::Uint);
		Fn.ParamTypes.Add(Param0);
		OutFunctions.Add(Fn);
	}
}

BEGIN_SHADER_PARAMETER_STRUCT(FSkeletonDataInterfaceParameters, )
	SHADER_PARAMETER(uint32, NumVertices)
	SHADER_PARAMETER(uint32, NumBoneInfluences)
	SHADER_PARAMETER(uint32, InputWeightStart)
	SHADER_PARAMETER(uint32, InputWeightStride)
	SHADER_PARAMETER(uint32, InputWeightIndexSize)
	SHADER_PARAMETER_SRV(Buffer<float4>, BoneMatrices)
	SHADER_PARAMETER_SRV(Buffer<uint>, InputWeightStream)
	SHADER_PARAMETER_SRV(Buffer<uint>, InputWeightLookupStream)
END_SHADER_PARAMETER_STRUCT()

void USkeletonDataInterface::GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& OutBuilder) const
{
	OutBuilder.AddNestedStruct<FSkeletonDataInterfaceParameters>(UID);
}

void USkeletonDataInterface::GetPermutations(FComputeKernelPermutationVector& OutPermutationVector) const
{
	// Need to be able to support these permutations according to the skeletal mesh settings.
	// todo[CF]: Filter these based on which functions in the data interface are attached. That will reduce unnecessary permutations.
	OutPermutationVector.AddPermutation(TEXT("ENABLE_DEFORMER_BONES"), 2);
	OutPermutationVector.AddPermutation(TEXT("GPUSKIN_UNLIMITED_BONE_INFLUENCE"), 2);
	OutPermutationVector.AddPermutation(TEXT("GPUSKIN_BONE_INDEX_UINT16"), 2);
	//OutPermutationVector.AddPermutation(TEXT("MERGE_DUPLICATED_VERTICES"), 2);
}

void USkeletonDataInterface::GetHLSL(FString& OutHLSL) const
{
	OutHLSL += TEXT("#include \"/Plugin/Optimus/Private/DataInterfaceSkeleton.ush\"\n");
}

void USkeletonDataInterface::GetSourceTypes(TArray<UClass*>& OutSourceTypes) const
{
	OutSourceTypes.Add(USkinnedMeshComponent::StaticClass());
}

UComputeDataProvider* USkeletonDataInterface::CreateDataProvider(TArrayView< TObjectPtr<UObject> > InSourceObjects, uint64 InInputMask, uint64 InOutputMask) const
{
	USkeletonDataProvider* Provider = NewObject<USkeletonDataProvider>();

	if (InSourceObjects.Num() == 1)
	{
		Provider->SkinnedMesh = Cast<USkinnedMeshComponent>(InSourceObjects[0]);
	}

	return Provider;
}


bool USkeletonDataProvider::IsValid() const
{
	return
		SkinnedMesh != nullptr &&
		SkinnedMesh->MeshObject != nullptr;
}

FComputeDataProviderRenderProxy* USkeletonDataProvider::GetRenderProxy()
{
	return new FSkeletonDataProviderProxy(SkinnedMesh);
}


FSkeletonDataProviderProxy::FSkeletonDataProviderProxy(USkinnedMeshComponent* SkinnedMeshComponent)
{
	SkeletalMeshObject = SkinnedMeshComponent->MeshObject;
	BoneRevisionNumber = SkinnedMeshComponent->GetBoneTransformRevisionNumber();
}

struct FSkeletonDataInterfacePermutationIds
{
	uint32 EnableDeformerBones = 0;
	uint32 UnlimitedBoneInfluence = 0;
	uint32 BoneIndexUint16 = 0;

	FSkeletonDataInterfacePermutationIds(FComputeKernelPermutationVector const& PermutationVector)
	{
		{
			static FString Name(TEXT("ENABLE_DEFORMER_BONES"));
			static uint32 Hash = GetTypeHash(Name);
			EnableDeformerBones = PermutationVector.GetPermutationBits(Name, Hash, 1);
		}
		{
			static FString Name(TEXT("GPUSKIN_UNLIMITED_BONE_INFLUENCE"));
			static uint32 Hash = GetTypeHash(Name);
			UnlimitedBoneInfluence = PermutationVector.GetPermutationBits(Name, Hash, 1);
		}
		{
			static FString Name(TEXT("GPUSKIN_BONE_INDEX_UINT16"));
			static uint32 Hash = GetTypeHash(Name);
			BoneIndexUint16 = PermutationVector.GetPermutationBits(Name, Hash, 1);
		}
	}
};

void FSkeletonDataProviderProxy::GatherDispatchData(FDispatchSetup const& InDispatchSetup, FCollectedDispatchData& InOutDispatchData)
{
	if (!ensure(InDispatchSetup.ParameterStructSizeForValidation == sizeof(FSkeletonDataInterfaceParameters)))
	{
		return;
	}

	FSkeletalMeshRenderData const& SkeletalMeshRenderData = SkeletalMeshObject->GetSkeletalMeshRenderData();
	FSkeletalMeshLODRenderData const* LodRenderData = SkeletalMeshRenderData.GetPendingFirstLOD(0);
	if (!ensure(LodRenderData->RenderSections.Num() == InDispatchSetup.NumInvocations))
	{
		return;
	}

	FSkeletonDataInterfacePermutationIds PermutationIds(InDispatchSetup.PermutationVector);

	FRHIShaderResourceView* NullSRVBinding = GWhiteVertexBufferWithSRV->ShaderResourceViewRHI.GetReference();

	for (int32 InvocationIndex = 0; InvocationIndex < InDispatchSetup.NumInvocations; ++InvocationIndex)
	{
		FSkelMeshRenderSection const& RenderSection = LodRenderData->RenderSections[InvocationIndex];

		const int32 LodIndex = SkeletalMeshRenderData.GetPendingFirstLODIdx(0);
		const bool bPreviousFrame = false;
		FRHIShaderResourceView* BoneBufferSRV = FSkeletalMeshDeformerHelpers::GetBoneBufferForReading(SkeletalMeshObject, LodIndex, InvocationIndex, bPreviousFrame);

		FSkinWeightVertexBuffer const* WeightBuffer = LodRenderData->GetSkinWeightVertexBuffer();
		check(WeightBuffer != nullptr);
		FRHIShaderResourceView* SkinWeightBufferSRV = WeightBuffer->GetDataVertexBuffer()->GetSRV();
		const bool bUnlimitedBoneInfluences = WeightBuffer->GetBoneInfluenceType() == GPUSkinBoneInfluenceType::UnlimitedBoneInfluence;
		FRHIShaderResourceView* InputWeightLookupStreamSRV = bUnlimitedBoneInfluences ? WeightBuffer->GetLookupVertexBuffer()->GetSRV() : nullptr;
		const bool bValidBones = (BoneBufferSRV != nullptr) && (SkinWeightBufferSRV != nullptr) && (!bUnlimitedBoneInfluences || InputWeightLookupStreamSRV != nullptr);
		const bool bUse16BitBoneIndex = WeightBuffer->Use16BitBoneIndex();

		FSkeletonDataInterfaceParameters* Parameters = (FSkeletonDataInterfaceParameters*)(InOutDispatchData.ParameterBuffer + InDispatchSetup.ParameterBufferOffset + InDispatchSetup.ParameterBufferStride * InvocationIndex);
		Parameters->NumVertices = RenderSection.NumVertices;
		Parameters->NumBoneInfluences = WeightBuffer->GetMaxBoneInfluences();
		Parameters->InputWeightStart = (WeightBuffer->GetConstantInfluencesVertexStride() * RenderSection.GetVertexBufferIndex()) / sizeof(float);
		Parameters->InputWeightStride = WeightBuffer->GetConstantInfluencesVertexStride();
		Parameters->InputWeightIndexSize = WeightBuffer->GetBoneIndexByteSize();
		Parameters->BoneMatrices = BoneBufferSRV != nullptr ? BoneBufferSRV : NullSRVBinding;
		Parameters->InputWeightStream = SkinWeightBufferSRV != nullptr ? SkinWeightBufferSRV : NullSRVBinding;
		Parameters->InputWeightLookupStream = InputWeightLookupStreamSRV != nullptr ? InputWeightLookupStreamSRV : NullSRVBinding;

		InOutDispatchData.PermutationId[InvocationIndex] |= (bValidBones ? PermutationIds.EnableDeformerBones : 0);
		InOutDispatchData.PermutationId[InvocationIndex] |= (bUnlimitedBoneInfluences ? PermutationIds.UnlimitedBoneInfluence : 0);
		InOutDispatchData.PermutationId[InvocationIndex] |= (bUse16BitBoneIndex ? PermutationIds.BoneIndexUint16 : 0);
	}
}
