// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusDataInterfaceJiggleSpring.h"

#include "Components/SkeletalMeshComponent.h"
#include "ComputeFramework/ComputeKernelPermutationVector.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "OptimusDataDomain.h"
#include "ShaderParameterMetadataBuilder.h"
#include "RenderGraphBuilder.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SkeletalMeshDeformerHelpers.h"
#include "SkeletalRenderPublic.h"

//
// FOptimusJiggleSpringParameters
//

bool FOptimusJiggleSpringParameters::ReadWeightsFile(const FFilePath& FilePath, TArray<float>& Values) const
{
#if WITH_EDITOR
	if (!FilePath.FilePath.IsEmpty())
	{
		FPlatformFileManager& FileManager = FPlatformFileManager::Get();
		IPlatformFile& PlatformFile = FileManager.GetPlatformFile();
		if (!PlatformFile.FileExists(*FilePath.FilePath))
		{
			return false;
		}
		TSharedPtr<IFileHandle> File(PlatformFile.OpenRead(*FilePath.FilePath, false));
		if (File.IsValid())
		{
			if (FilePath.FilePath.EndsWith(".bin"))
			{
				size_t NumValues = 0;
				if (!File->Read(reinterpret_cast<uint8*>(&NumValues), sizeof(size_t)))
					return false;
				Values.SetNum(NumValues);

				int64 BytesRemaining = File->Size() - File->Tell();
				uint8 DataSize = BytesRemaining / NumValues;
				if (DataSize == sizeof(float))
				{
					if (!File->Read(reinterpret_cast<uint8*>(&Values[0]), NumValues * DataSize))
						return false;
				}
				else if (DataSize == sizeof(double))
				{
					TArray<double> Tmp;
					Tmp.SetNum(NumValues);
					if (!File->Read(reinterpret_cast<uint8*>(&Tmp[0]), NumValues * DataSize))
						return false;
					for (int32 i = 0; i < NumValues; i++)
						Values[i] = Tmp[i];
				}
				else
				{
					return false;
				}
			}
			else
			{
				return false;
			}
		}
		else
		{
			return false;
		}
	}
#endif
	return true;
}


//
// Interface
//

FString
UOptimusJiggleSpringDataInterface::GetDisplayName() const
{
	return TEXT("Jiggle Spring");
}

TArray<FOptimusCDIPinDefinition> 
UOptimusJiggleSpringDataInterface::GetPinDefinitions() const
{
	TArray<FOptimusCDIPinDefinition> Defs;
	Defs.Add({ "Stiffness", "ReadStiffness", Optimus::DomainName::Vertex, "ReadNumVertices" });
	Defs.Add({ "Damping", "ReadDamping", Optimus::DomainName::Vertex, "ReadNumVertices" });
	return Defs;
}

void 
UOptimusJiggleSpringDataInterface::GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const
{
	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadNumVertices"))
		.AddReturnType(EShaderFundamentalType::Uint);

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadStiffness"))
		.AddReturnType(EShaderFundamentalType::Float)
		.AddParam(EShaderFundamentalType::Uint);

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadDamping"))
		.AddReturnType(EShaderFundamentalType::Float)
		.AddParam(EShaderFundamentalType::Uint);
}

BEGIN_SHADER_PARAMETER_STRUCT(FJiggleSpringDataInterfaceParameters, )
	SHADER_PARAMETER(uint32, NumVertices)
	SHADER_PARAMETER(uint32, NumStiffnessWeights)
	SHADER_PARAMETER(uint32, NumDampingWeights)
	SHADER_PARAMETER(float, Stiffness)
	SHADER_PARAMETER(float, Damping)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float>, StiffnessWeightsBuffer)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float>, DampingWeightsBuffer)
END_SHADER_PARAMETER_STRUCT()

void 
UOptimusJiggleSpringDataInterface::GetShaderParameters(
	TCHAR const* UID, 
	FShaderParametersMetadataBuilder& InOutBuilder, 
	FShaderParametersMetadataAllocations& InOutAllocations) const
{
	InOutBuilder.AddNestedStruct<FJiggleSpringDataInterfaceParameters>(UID);
}

void 
UOptimusJiggleSpringDataInterface::GetPermutations(FComputeKernelPermutationVector& OutPermutationVector) const
{
	OutPermutationVector.AddPermutation(TEXT("ENABLE_DEFORMER_JIGGLE_SPRING"), 2);
}

void 
UOptimusJiggleSpringDataInterface::GetShaderHash(FString& InOutKey) const
{
	GetShaderFileHash(
		TEXT("/Plugin/Optimus/Private/DataInterfaceJiggleSpring.ush"), 
		EShaderPlatform::SP_PCD3D_SM5).AppendString(InOutKey);
}

void 
UOptimusJiggleSpringDataInterface::GetHLSL(FString& OutHLSL) const
{
	OutHLSL += TEXT("#include \"/Plugin/Optimus/Private/DataInterfaceJiggleSpring.ush\"\n");
}

UComputeDataProvider* 
UOptimusJiggleSpringDataInterface::CreateDataProvider(TObjectPtr<UObject> InBinding, uint64 InInputMask, uint64 InOutputMask) const
{
	UOptimusJiggleSpringDataProvider* Provider = NewObject<UOptimusJiggleSpringDataProvider>();
	Provider->SkeletalMesh = Cast<USkeletalMeshComponent>(InBinding);
	Provider->JiggleSpringParameters = JiggleSpringParameters;
	return Provider;
}

//
// DataProvider
//

bool 
UOptimusJiggleSpringDataProvider::IsValid() const
{
	return 
		SkeletalMesh != nullptr &&
		SkeletalMesh->MeshObject != nullptr;
}

FComputeDataProviderRenderProxy* 
UOptimusJiggleSpringDataProvider::GetRenderProxy()
{
	// Not sure where file reading should happen...
	if(!JiggleSpringParameters.StiffnessWeights.Num() && !JiggleSpringParameters.StiffnessWeightsFile.FilePath.IsEmpty())
		JiggleSpringParameters.ReadWeightsFile(JiggleSpringParameters.StiffnessWeightsFile, JiggleSpringParameters.StiffnessWeights);
	if(!JiggleSpringParameters.DampingWeights.Num() && !JiggleSpringParameters.DampingWeightsFile.FilePath.IsEmpty())
		JiggleSpringParameters.ReadWeightsFile(JiggleSpringParameters.DampingWeightsFile, JiggleSpringParameters.DampingWeights);
	return new FOptimusJiggleSpringDataProviderProxy(SkeletalMesh, JiggleSpringParameters);
}

//
// Proxy
//

FOptimusJiggleSpringDataProviderProxy::FOptimusJiggleSpringDataProviderProxy(
	USkeletalMeshComponent* SkeletalMeshComponent, 
	FOptimusJiggleSpringParameters const& InJiggleSpringParameters)
{
	SkeletalMeshObject = SkeletalMeshComponent->MeshObject;
	JiggleSpringParameters = InJiggleSpringParameters;
}

void
FOptimusJiggleSpringDataProviderProxy::AllocateResources(FRDGBuilder& GraphBuilder)
{
	// Stiffness weights
	if (JiggleSpringParameters.StiffnessWeights.Num())
	{
		StiffnessWeightsBuffer =
			GraphBuilder.CreateBuffer(
				FRDGBufferDesc::CreateStructuredDesc(sizeof(float), JiggleSpringParameters.StiffnessWeights.Num()),
				TEXT("JiggleSpring.StiffnessWeights"));
		StiffnessWeightsBufferSRV =
			GraphBuilder.CreateSRV(StiffnessWeightsBuffer);
		GraphBuilder.QueueBufferUpload(
			StiffnessWeightsBuffer,
			JiggleSpringParameters.StiffnessWeights.GetData(),
			JiggleSpringParameters.StiffnessWeights.Num() * sizeof(float),
			ERDGInitialDataFlags::None);
	}
	else
	{
		StiffnessWeightsBuffer =
			GraphBuilder.CreateBuffer(
				FRDGBufferDesc::CreateStructuredDesc(sizeof(float), 1),
				TEXT("JiggleSpring.StiffnessWeights"));
		StiffnessWeightsBufferSRV =
			GraphBuilder.CreateSRV(StiffnessWeightsBuffer);
		GraphBuilder.QueueBufferUpload(
			StiffnessWeightsBuffer,
			&NullFloatBuffer,
			1 * sizeof(float),
			ERDGInitialDataFlags::None);
	}
	// Damping weights
	if (JiggleSpringParameters.DampingWeights.Num())
	{
		DampingWeightsBuffer =
			GraphBuilder.CreateBuffer(
				FRDGBufferDesc::CreateStructuredDesc(sizeof(float), JiggleSpringParameters.DampingWeights.Num()),
				TEXT("JiggleSpring.DampingWeights"));
		DampingWeightsBufferSRV =
			GraphBuilder.CreateSRV(DampingWeightsBuffer);
		GraphBuilder.QueueBufferUpload(
			DampingWeightsBuffer,
			JiggleSpringParameters.DampingWeights.GetData(),
			JiggleSpringParameters.DampingWeights.Num() * sizeof(float),
			ERDGInitialDataFlags::None);
	}
	else
	{
		DampingWeightsBuffer =
			GraphBuilder.CreateBuffer(
				FRDGBufferDesc::CreateStructuredDesc(sizeof(float), 1),
				TEXT("JiggleSpring.DampingWeights"));
		DampingWeightsBufferSRV =
			GraphBuilder.CreateSRV(DampingWeightsBuffer);
		GraphBuilder.QueueBufferUpload(
			DampingWeightsBuffer,
			&NullFloatBuffer,
			1 * sizeof(float),
			ERDGInitialDataFlags::None);
	}
}

struct FJiggleSpringDataInterfacePermutationIds
{
	uint32 EnableDeformerJiggleSpring = 0;

	FJiggleSpringDataInterfacePermutationIds(FComputeKernelPermutationVector const& PermutationVector)
	{
		{
			static FString Name(TEXT("ENABLE_DEFORMER_JIGGLE_SPRING"));
			static uint32 Hash = GetTypeHash(Name);
			EnableDeformerJiggleSpring = PermutationVector.GetPermutationBits(Name, Hash, 1);
		}
	}
};

void FOptimusJiggleSpringDataProviderProxy::GatherDispatchData(FDispatchSetup const& InDispatchSetup, FCollectedDispatchData& InOutDispatchData)
{
	if (!ensure(InDispatchSetup.ParameterStructSizeForValidation == sizeof(FJiggleSpringDataInterfaceParameters)))
	{
		return;
	}

	const int32 LodIndex = SkeletalMeshObject->GetLOD();
	FSkeletalMeshRenderData const& SkeletalMeshRenderData = SkeletalMeshObject->GetSkeletalMeshRenderData();
	FSkeletalMeshLODRenderData const* LodRenderData = &SkeletalMeshRenderData.LODRenderData[LodIndex];
	if (!ensure(LodRenderData->RenderSections.Num() == InDispatchSetup.NumInvocations))
	{
		return;
	}

	FJiggleSpringDataInterfacePermutationIds PermutationIds(InDispatchSetup.PermutationVector);

	for (int32 InvocationIndex = 0; InvocationIndex < InDispatchSetup.NumInvocations; ++InvocationIndex)
	{
		FSkelMeshRenderSection const& RenderSection = LodRenderData->RenderSections[InvocationIndex];

		FJiggleSpringDataInterfaceParameters* Parameters = 
			(FJiggleSpringDataInterfaceParameters*)(InOutDispatchData.ParameterBuffer + InDispatchSetup.ParameterBufferOffset + InDispatchSetup.ParameterBufferStride * InvocationIndex);
		Parameters->NumVertices = RenderSection.NumVertices;

		Parameters->NumStiffnessWeights = JiggleSpringParameters.StiffnessWeights.Num();
		Parameters->NumDampingWeights = JiggleSpringParameters.DampingWeights.Num();

		Parameters->Stiffness = JiggleSpringParameters.BaselineStiffness;
		Parameters->Damping = JiggleSpringParameters.BaselineDamping;

		Parameters->StiffnessWeightsBuffer = StiffnessWeightsBufferSRV;
		Parameters->DampingWeightsBuffer = DampingWeightsBufferSRV;

		InOutDispatchData.PermutationId[InvocationIndex] |= PermutationIds.EnableDeformerJiggleSpring;
	}

}
