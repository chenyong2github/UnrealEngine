// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeFramework/ComputeGraph.h"

#include "ComputeFramework/ComputeDataInterface.h"
#include "ComputeFramework/ComputeFramework.h"
#include "ComputeFramework/ComputeKernel.h"
#include "ComputeFramework/ComputeKernelShared.h"
#include "ComputeFramework/ComputeKernelSource.h"
#include "ShaderParameterMetadataBuilder.h"

UComputeGraph::UComputeGraph(const FObjectInitializer& ObjectInitializer)
	: UObject(ObjectInitializer)
{
}

UComputeGraph::UComputeGraph(FVTableHelper& Helper)
	: Super(Helper)
{
}

UComputeGraph::~UComputeGraph() = default;

void UComputeGraph::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	// PostLoad our kernel dependencies before compiling.
	for (UComputeKernel* Kernel : KernelInvocations)
	{
		if (Kernel != nullptr)
		{
			Kernel->ConditionalPostLoad();
		}
	}

	if (ValidateGraph())
	{
		CacheResourceShadersForRendering();
	}
#endif
}

bool UComputeGraph::ValidateGraph(FString* OutErrors)
{
	// todo[CF]:
	// Check same number of kernel in/outs as edges.
	// Check each edge connects matching function types.
	// Check graph is DAG
	return true;
}

TCHAR const* UComputeGraph::GetDataInterfaceUID(int32 DataInterfaceIndex)
{
	// Use static TChunkedArray so that data is never released/reallocated.
	static TChunkedArray<FString, 512> UIDStore;
	
	if (DataInterfaceIndex >= UIDStore.Num())
	{
		UIDStore.Add(DataInterfaceIndex + 1 - UIDStore.Num());
	}
	if (UIDStore[DataInterfaceIndex].IsEmpty())
	{
		UIDStore[DataInterfaceIndex] = FString::Printf(TEXT("DI%03d"), DataInterfaceIndex);
	}
	return *UIDStore[DataInterfaceIndex];
}

#if WITH_EDITOR

namespace
{
	/** Add HLSL code to implement an external function. */
	void GetFunctionShimHLSL(FShaderFunctionDefinition const& FnImpl, FShaderFunctionDefinition const& FnWrap, TCHAR const* UID, FString& InOutHLSL)
	{
		const bool bHasReturn = FnWrap.bHasReturnType;
		const int32 NumParams = FnWrap.ParamTypes.Num();

		TStringBuilder<512> StringBuilder;

		StringBuilder.Append(bHasReturn ? *FnWrap.ParamTypes[0].TypeDeclaration : TEXT("void"));
		StringBuilder.Append(TEXT(" "));
		StringBuilder.Append(*FnWrap.Name);
		StringBuilder.Append(TEXT("("));
		
		for (int32 ParameterIndex = bHasReturn ? 1 : 0; ParameterIndex < NumParams; ++ParameterIndex)
		{
			StringBuilder.Append(*FnWrap.ParamTypes[ParameterIndex].TypeDeclaration);
			StringBuilder.Appendf(TEXT(" P%d"), ParameterIndex);
			StringBuilder.Append((ParameterIndex < NumParams - 1) ? TEXT(", ") : TEXT(""));
		}

		StringBuilder.Append(TEXT(") { "));
		StringBuilder.Append(bHasReturn ? TEXT("return ") : TEXT(""));
		StringBuilder.Append(UID).Append(TEXT("_")).Append(*FnImpl.Name);
		StringBuilder.Append(TEXT("("));

		for (int32 ParameterIndex = bHasReturn ? 1 : 0; ParameterIndex < NumParams; ++ParameterIndex)
		{
			StringBuilder.Appendf(TEXT("P%d"), ParameterIndex);
			StringBuilder.Append((ParameterIndex < NumParams - 1) ? TEXT(", ") : TEXT(""));
		}

		StringBuilder.Append(TEXT("); }\n"));

		InOutHLSL += StringBuilder.ToString();
	}
}

void UComputeGraph::CacheResourceShadersForRendering()
{
	const uint32 CompilationFlags = uint32(EComputeKernelCompilationFlags::ApplyCompletedShaderMapForRendering) | uint32(EComputeKernelCompilationFlags::Force);
	CacheResourceShadersForRendering(CompilationFlags);
}

void UComputeGraph::CacheResourceShadersForRendering(uint32 CompilationFlags)
{
	// We expect the graph to be validated before attempting to compile.
	if (FApp::CanEverRender() && ensure(ValidateGraph()))
	{
		KernelResources.SetNum(KernelInvocations.Num());
		for (int32 KernelIndex = 0; KernelIndex < KernelInvocations.Num(); ++KernelIndex)
		{
			UComputeKernel* Kernel = KernelInvocations[KernelIndex];
			UComputeKernelSource* KernelSource = Kernel == nullptr ? nullptr : Kernel->KernelSource;

			if (KernelSource == nullptr)
			{
				if (KernelResources[KernelIndex].IsValid())
				{
					KernelResources[KernelIndex]->Invalidate();
					KernelResources[KernelIndex] = nullptr;
				}

				continue;
			}

			if (!KernelResources[KernelIndex].IsValid())
			{
				KernelResources[KernelIndex] = MakeUnique<FComputeKernelResource>();
			}

			FComputeKernelResource* KernelResource = KernelResources[KernelIndex].Get();

			TArray<int32> RelevantEdgeIndices;
			TArray<int32> DataProviderIndices;
			for (int32 GraphEdgeIndex = 0; GraphEdgeIndex < GraphEdges.Num(); ++GraphEdgeIndex)
			{
				if (GraphEdges[GraphEdgeIndex].KernelIndex == KernelIndex)
				{
					RelevantEdgeIndices.Add(GraphEdgeIndex);
					DataProviderIndices.AddUnique(GraphEdges[GraphEdgeIndex].DataInterfaceIndex);
				}
			}

			// Collect data interface shader code.
			FString HLSL;
			for (int32 DataProviderIndex : DataProviderIndices)
			{
				UComputeDataInterface* DataInterface = DataInterfaces[DataProviderIndex];
				
				// Add a unique prefix to generate unique names in the data interface shader code.
				TCHAR const* UID = GetDataInterfaceUID(DataProviderIndex);
				HLSL += FString::Printf(TEXT("#define DI_UID %s_\n"), UID);
				DataInterface->GetHLSL(HLSL);
				HLSL += TEXT("#undef DI_UID\n");
			}

			// Bind every external kernel function to the associated data input function.
			for (int32 GraphEdgeIndex : RelevantEdgeIndices)
			{
				if (GraphEdges[GraphEdgeIndex].bKernelInput)
				{
					TArray<FShaderFunctionDefinition> DataProviderFunctions;
					DataInterfaces[GraphEdges[GraphEdgeIndex].DataInterfaceIndex]->GetSupportedInputs(DataProviderFunctions);
					FShaderFunctionDefinition& DataProviderFunction = DataProviderFunctions[GraphEdges[GraphEdgeIndex].DataInterfaceBindingIndex];
					FShaderFunctionDefinition& KernelFunction = KernelSource->ExternalInputs[GraphEdges[GraphEdgeIndex].KernelBindingIndex];
					TCHAR const* UID = GetDataInterfaceUID(GraphEdges[GraphEdgeIndex].DataInterfaceIndex);
					GetFunctionShimHLSL(DataProviderFunction, KernelFunction, UID, HLSL);
				}
				else
				{
					TArray<FShaderFunctionDefinition> DataProviderFunctions;
					DataInterfaces[GraphEdges[GraphEdgeIndex].DataInterfaceIndex]->GetSupportedOutputs(DataProviderFunctions);
					FShaderFunctionDefinition& DataProviderFunction = DataProviderFunctions[GraphEdges[GraphEdgeIndex].DataInterfaceBindingIndex];
					FShaderFunctionDefinition& KernelFunction = KernelSource->ExternalOutputs[GraphEdges[GraphEdgeIndex].KernelBindingIndex];
					TCHAR const* UID = GetDataInterfaceUID(GraphEdges[GraphEdgeIndex].DataInterfaceIndex);
					GetFunctionShimHLSL(DataProviderFunction, KernelFunction, UID, HLSL);
				}
			}

			HLSL += KernelSource->GetSource();

			// Collect and build shader parameter metadata.
			FShaderParametersMetadataBuilder Builder;
			KernelSource->GetShaderParameters(Builder);

			for (int32 DataProviderIndex : DataProviderIndices)
			{
				UComputeDataInterface* DataInterface = DataInterfaces[DataProviderIndex];
				TCHAR const* UID = GetDataInterfaceUID(DataProviderIndex);
				DataInterface->GetShaderParameters(UID, Builder);
			}

			FShaderParametersMetadata* ShaderMetadata = Builder.Build(FShaderParametersMetadata::EUseCase::ShaderParameterStruct, *GetName());

			// Now we have all the information that the KernelResource will need for compilation.
			KernelResource->SetupResource(
				GMaxRHIFeatureLevel,
				GetName(),
				KernelSource->GetEntryPoint(),
				MoveTemp(HLSL),
				KernelSource->GetSourceCodeHash(),
				ShaderMetadata);

			const ERHIFeatureLevel::Type CacheFeatureLevel = GMaxRHIFeatureLevel;
			const EShaderPlatform ShaderPlatform = GShaderPlatformForFeatureLevel[CacheFeatureLevel];

			CacheShadersForResource(ShaderPlatform, nullptr, CompilationFlags | uint32(EComputeKernelCompilationFlags::Force), KernelResource);
		}
	}
}

void UComputeGraph::CacheShadersForResource(
	EShaderPlatform ShaderPlatform,
	const ITargetPlatform* TargetPlatform,
	uint32 CompilationFlags,
	FComputeKernelResource* KernelResource)
{
	bool bCooking = (CompilationFlags & uint32(EComputeKernelCompilationFlags::IsCooking)) != 0;

	const bool bIsDefault = (KernelResource->GetKernelFlags() & uint32(EComputeKernelFlags::IsDefaultKernel)) != 0;
	if (!GIsEditor || GIsAutomationTesting || bIsDefault || bCooking)
	{
		CompilationFlags |= uint32(EComputeKernelCompilationFlags::Synchronous);
	}

	const bool bIsSuccess = KernelResource->CacheShaders(
		ShaderPlatform,
		TargetPlatform,
		CompilationFlags & uint32(EComputeKernelCompilationFlags::ApplyCompletedShaderMapForRendering),
		CompilationFlags & uint32(EComputeKernelCompilationFlags::Synchronous)
	);

	if (!bIsSuccess)
	{
		if (bIsDefault)
		{
			UE_LOG(
				LogComputeFramework,
				Fatal,
				TEXT("Failed to compile default FComputeKernelResource [%s] for platform [%s]!"),
				*KernelResource->GetFriendlyName(),
				*LegacyShaderPlatformToShaderFormat(ShaderPlatform).ToString()
			);
		}

		UE_LOG(
			LogComputeFramework,
			Warning,
			TEXT("Failed to compile FComputeKernelResource [%s] for platform [%s]."),
			*KernelResource->GetFriendlyName(),
			*LegacyShaderPlatformToShaderFormat(ShaderPlatform).ToString()
		);

		auto& CompilationErrors = KernelResource->GetCompileErrors();
		uint32 ErrorCount = CompilationErrors.Num();
		for (uint32 i = 0; i < ErrorCount; ++i)
		{
			UE_LOG(LogComputeFramework, Warning, TEXT("      [Error] - %s"), *CompilationErrors[i]);
		}
	}
}

#endif // WITH_EDITOR
