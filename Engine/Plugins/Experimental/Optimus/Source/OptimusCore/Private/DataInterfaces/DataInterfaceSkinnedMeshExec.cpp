// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataInterfaces/DataInterfaceSkinnedMeshExec.h"

#include "OptimusDataDomain.h"

#include "Components/SkinnedMeshComponent.h"
#include "ComputeFramework/ShaderParameterMetadataBuilder.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "RenderGraphBuilder.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SkeletalMeshDeformerHelpers.h"
#include "SkeletalRenderPublic.h"

FString USkinnedMeshExecDataInterface::GetDisplayName() const
{
	return TEXT("Execute Skinned Mesh");
}

FName USkinnedMeshExecDataInterface::GetCategory() const
{
	return CategoryName::ExecutionDataInterfaces;
}

TArray<FOptimusCDIPinDefinition> USkinnedMeshExecDataInterface::GetPinDefinitions() const
{
	using namespace Optimus::DomainName;
	
	TArray<FOptimusCDIPinDefinition> Defs;
	Defs.Add({ "NumThreads", "ReadNumThreads" });

	return Defs;
}

void USkinnedMeshExecDataInterface::GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const
{
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("ReadNumThreads");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = {};
		ReturnParam.ValueType = FShaderValueType::Get(EShaderFundamentalType::Int, 3);
		Fn.ParamTypes.Add(ReturnParam);
		OutFunctions.Add(Fn);
	}
}


BEGIN_SHADER_PARAMETER_STRUCT(FSkinedMeshExecDataInterfaceParameters, )
	SHADER_PARAMETER(FIntVector, NumThreads)
END_SHADER_PARAMETER_STRUCT()

void USkinnedMeshExecDataInterface::GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& OutBuilder) const
{
	OutBuilder.AddNestedStruct<FSkinedMeshExecDataInterfaceParameters>(UID);
}

void USkinnedMeshExecDataInterface::GetHLSL(FString& OutHLSL) const
{
	OutHLSL += TEXT("#include \"/Plugin/Optimus/Private/DataInterfaceSkinnedMeshExec.ush\"\n");
}

void USkinnedMeshExecDataInterface::GetSourceTypes(TArray<UClass*>& OutSourceTypes) const
{
	OutSourceTypes.Add(USkinnedMeshComponent::StaticClass());
}

UComputeDataProvider* USkinnedMeshExecDataInterface::CreateDataProvider(TArrayView< TObjectPtr<UObject> > InSourceObjects, uint64 InInputMask, uint64 InOutputMask) const
{
	USkinnedMeshExecDataProvider* Provider = NewObject<USkinnedMeshExecDataProvider>();

	if (InSourceObjects.Num() == 1)
	{
		Provider->SkinnedMesh = Cast<USkinnedMeshComponent>(InSourceObjects[0]);
		Provider->Domain = Domain;
	}

	return Provider;
}


bool USkinnedMeshExecDataProvider::IsValid() const
{
	return
		SkinnedMesh != nullptr &&
		SkinnedMesh->MeshObject != nullptr;
}

FComputeDataProviderRenderProxy* USkinnedMeshExecDataProvider::GetRenderProxy()
{
	return new FSkinnedMeshExecDataProviderProxy(SkinnedMesh, Domain);
}


FSkinnedMeshExecDataProviderProxy::FSkinnedMeshExecDataProviderProxy(USkinnedMeshComponent* InSkinnedMeshComponent, ESkinnedMeshExecDomain InDomain)
{
	SkeletalMeshObject = InSkinnedMeshComponent->MeshObject;
	Domain = InDomain;
}

int32 FSkinnedMeshExecDataProviderProxy::GetDispatchThreadCount(TArray<FIntVector>& ThreadCounts) const
{
	FSkeletalMeshRenderData const& SkeletalMeshRenderData = SkeletalMeshObject->GetSkeletalMeshRenderData();
	FSkeletalMeshLODRenderData const* LodRenderData = SkeletalMeshRenderData.GetPendingFirstLOD(0);
	const int32 NumInvocations = LodRenderData->RenderSections.Num();

	ThreadCounts.Reset();
	ThreadCounts.Reserve(NumInvocations);
	for (int32 InvocationIndex = 0; InvocationIndex < NumInvocations; ++InvocationIndex)
	{
		FSkelMeshRenderSection const& RenderSection = LodRenderData->RenderSections[InvocationIndex];
		const int32 NumThreads = Domain == ESkinnedMeshExecDomain::Vertex ? RenderSection.NumVertices : RenderSection.NumTriangles;
		ThreadCounts.Add(FIntVector(NumThreads, 1, 1));
	}
	
	return NumInvocations;
}

void FSkinnedMeshExecDataProviderProxy::GatherDispatchData(FDispatchSetup const& InDispatchSetup, FCollectedDispatchData& InOutDispatchData)
{
	if (!ensure(InDispatchSetup.ParameterStructSizeForValidation == sizeof(FSkinedMeshExecDataInterfaceParameters)))
	{
		return;
	}

	FSkeletalMeshRenderData const& SkeletalMeshRenderData = SkeletalMeshObject->GetSkeletalMeshRenderData();
	FSkeletalMeshLODRenderData const* LodRenderData = SkeletalMeshRenderData.GetPendingFirstLOD(0);
	const int32 NumInvocations = LodRenderData->RenderSections.Num();
	if (!ensure(NumInvocations == InDispatchSetup.NumInvocations))
	{
		return;
	}

	for (int32 InvocationIndex = 0; InvocationIndex < NumInvocations; ++InvocationIndex)
	{
		FSkelMeshRenderSection const& RenderSection = LodRenderData->RenderSections[InvocationIndex];
		const int32 NumThreads = Domain == ESkinnedMeshExecDomain::Vertex ? RenderSection.NumVertices : RenderSection.NumTriangles;

		FSkinedMeshExecDataInterfaceParameters* Parameters = (FSkinedMeshExecDataInterfaceParameters*)(InOutDispatchData.ParameterBuffer + InDispatchSetup.ParameterBufferOffset + InDispatchSetup.ParameterBufferStride * InvocationIndex);
		Parameters->NumThreads = FIntVector(NumThreads, 1, 1);
	}
}
