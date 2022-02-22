// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataInterfaces/DataInterfaceMorphTarget.h"

#include "Components/SkeletalMeshComponent.h"
#include "ComputeFramework/ComputeKernelPermutationVector.h"
#include "ComputeFramework/ShaderParameterMetadataBuilder.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "OptimusDataDomain.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SkeletalMeshDeformerHelpers.h"
#include "SkeletalRenderPublic.h"

FString UMorphTargetDataInterface::GetDisplayName() const
{
	return TEXT("Morph Target");
}

TArray<FOptimusCDIPinDefinition> UMorphTargetDataInterface::GetPinDefinitions() const
{
	TArray<FOptimusCDIPinDefinition> Defs;

	using namespace Optimus::DomainName;

	Defs.Add({ "NumVertices", "ReadNumVertices" });
	Defs.Add({ "DeltaPosition", "ReadDeltaPosition", Vertex, "ReadNumVertices" });
	Defs.Add({ "DeltaNormal", "ReadDeltaNormal", Vertex, "ReadNumVertices" });

	return Defs;
}

void UMorphTargetDataInterface::GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const
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
		Fn.Name = TEXT("ReadDeltaPosition");
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
		Fn.Name = TEXT("ReadDeltaNormal");
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

BEGIN_SHADER_PARAMETER_STRUCT(FMorphTargetDataInterfaceParameters, )
	SHADER_PARAMETER(uint32, NumVertices)
	SHADER_PARAMETER(uint32, InputStreamStart)
	SHADER_PARAMETER_SRV(Buffer<float>, MorphBuffer)
END_SHADER_PARAMETER_STRUCT()

void UMorphTargetDataInterface::GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& OutBuilder) const
{
	OutBuilder.AddNestedStruct<FMorphTargetDataInterfaceParameters>(UID);
}

void UMorphTargetDataInterface::GetPermutations(FComputeKernelPermutationVector& OutPermutationVector) const
{
	OutPermutationVector.AddPermutation(TEXT("ENABLE_DEFORMER_MORPHTARGET"), 2);
}

void UMorphTargetDataInterface::GetHLSL(FString& OutHLSL) const
{
	OutHLSL += TEXT("#include \"/Plugin/Optimus/Private/DataInterfaceMorphTarget.ush\"\n");
}

void UMorphTargetDataInterface::GetSourceTypes(TArray<UClass*>& OutSourceTypes) const
{
	OutSourceTypes.Add(USkeletalMeshComponent::StaticClass());
}

UComputeDataProvider* UMorphTargetDataInterface::CreateDataProvider(TArrayView< TObjectPtr<UObject> > InSourceObjects, uint64 InInputMask, uint64 InOutputMask) const
{
	UMorphTargetDataProvider* Provider = NewObject<UMorphTargetDataProvider>();

	if (InSourceObjects.Num() == 1)
	{
		Provider->SkeletalMesh = Cast<USkeletalMeshComponent>(InSourceObjects[0]);
	}

	return Provider;
}


bool UMorphTargetDataProvider::IsValid() const
{
	return
		SkeletalMesh != nullptr &&
		SkeletalMesh->MeshObject != nullptr;
}

FComputeDataProviderRenderProxy* UMorphTargetDataProvider::GetRenderProxy()
{
	return new FMorphTargetDataProviderProxy(SkeletalMesh);
}


FMorphTargetDataProviderProxy::FMorphTargetDataProviderProxy(USkeletalMeshComponent* SkeletalMeshComponent)
{
	SkeletalMeshObject = SkeletalMeshComponent->MeshObject;
	FrameNumber = SkeletalMeshComponent->GetScene()->GetFrameNumber() + 1; // +1 matches the logic for FrameNumberToPrepare in FSkeletalMeshObjectGPUSkin::Update()
}

int32 FMorphTargetDataProviderProxy::GetInvocationCount() const
{
	FSkeletalMeshRenderData const& SkeletalMeshRenderData = SkeletalMeshObject->GetSkeletalMeshRenderData();
	FSkeletalMeshLODRenderData const* LodRenderData = SkeletalMeshRenderData.GetPendingFirstLOD(0);
	return LodRenderData->RenderSections.Num();
}

FIntVector FMorphTargetDataProviderProxy::GetDispatchDim(int32 InvocationIndex, FIntVector GroupDim) const
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

struct FMorphTargetDataInterfacePermutationIds
{
	uint32 EnableDeformerMorphTarget = 0;

	FMorphTargetDataInterfacePermutationIds(FComputeKernelPermutationVector const& PermutationVector)
	{
		{
			static FString Name(TEXT("ENABLE_DEFORMER_MORPHTARGET"));
			static uint32 Hash = GetTypeHash(Name);
			EnableDeformerMorphTarget = PermutationVector.GetPermutationBits(Name, Hash, 1);
		}
	}
};

void FMorphTargetDataProviderProxy::GatherDispatchData(FDispatchSetup const& InDispatchSetup, FCollectedDispatchData& InOutDispatchData)
{
	if (!ensure(InDispatchSetup.ParameterStructSizeForValidation == sizeof(FMorphTargetDataInterfaceParameters)))
	{
		return;
	}

	FSkeletalMeshRenderData const& SkeletalMeshRenderData = SkeletalMeshObject->GetSkeletalMeshRenderData();
	FSkeletalMeshLODRenderData const* LodRenderData = SkeletalMeshRenderData.GetPendingFirstLOD(0);
	if (!ensure(LodRenderData->RenderSections.Num() == InDispatchSetup.NumInvocations))
	{
		return;
	}

	FMorphTargetDataInterfacePermutationIds PermutationIds(InDispatchSetup.PermutationVector);

	FRHIShaderResourceView* NullSRVBinding = GWhiteVertexBufferWithSRV->ShaderResourceViewRHI.GetReference();

	for (int32 InvocationIndex = 0; InvocationIndex < InDispatchSetup.NumInvocations; ++InvocationIndex)
	{
		FSkelMeshRenderSection const& RenderSection = LodRenderData->RenderSections[InvocationIndex];

		const int32 LodIndex = SkeletalMeshRenderData.GetPendingFirstLODIdx(0);
		const bool bPreviousFrame = false;
		FRHIShaderResourceView* MorphBufferSRV = FSkeletalMeshDeformerHelpers::GetMorphTargetBufferForReading(SkeletalMeshObject, LodIndex, InvocationIndex, FrameNumber, bPreviousFrame);
		const bool bValidMorph = MorphBufferSRV != nullptr;

		FMorphTargetDataInterfaceParameters* Parameters = (FMorphTargetDataInterfaceParameters*)(InOutDispatchData.ParameterBuffer + InDispatchSetup.ParameterBufferOffset + InDispatchSetup.ParameterBufferStride * InvocationIndex);
		Parameters->NumVertices = RenderSection.NumVertices;
		Parameters->InputStreamStart = RenderSection.BaseVertexIndex;
		Parameters->MorphBuffer = MorphBufferSRV != nullptr ? MorphBufferSRV : NullSRVBinding;

		InOutDispatchData.PermutationId[InvocationIndex] |= (bValidMorph ? PermutationIds.EnableDeformerMorphTarget : 0);
	}
}
