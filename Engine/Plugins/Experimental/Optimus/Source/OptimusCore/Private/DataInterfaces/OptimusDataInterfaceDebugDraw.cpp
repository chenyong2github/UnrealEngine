// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusDataInterfaceDebugDraw.h"

#include "Components/PrimitiveComponent.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "OptimusDataDomain.h"
#include "OptimusDataTypeRegistry.h"
#include "RenderGraphBuilder.h"
#include "ShaderCore.h"
#include "ShaderParameterMetadataBuilder.h"


FString UOptimusDebugDrawDataInterface::GetDisplayName() const
{
	return TEXT("Debug Draw");
}

FName UOptimusDebugDrawDataInterface::GetCategory() const
{
	return CategoryName::DataInterfaces;
}

TArray<FOptimusCDIPinDefinition> UOptimusDebugDrawDataInterface::GetPinDefinitions() const
{
	TArray<FOptimusCDIPinDefinition> Defs;
	Defs.Add({ "DebugDraw", "ReadDebugDraw" });
	return Defs;
}

TSubclassOf<UActorComponent> UOptimusDebugDrawDataInterface::GetRequiredComponentClass() const
{
	return UPrimitiveComponent::StaticClass();
}

void UOptimusDebugDrawDataInterface::RegisterTypes() 
{
	FOptimusDataTypeRegistry::Get().RegisterType(
		FName("FDebugDraw"),
		FText::FromString(TEXT("FDebugDraw")),
		FShaderValueType::Get(FName("FDebugDraw"), { FShaderValueType::FStructElement(FName("LocalToWorld"), FShaderValueType::Get(EShaderFundamentalType::Float, 4, 4)) }),
		FName("FDebugDraw"),
		nullptr,
		FLinearColor(0.3f, 0.7f, 0.4f, 1.0f),
		EOptimusDataTypeUsageFlags::None);
}

void UOptimusDebugDrawDataInterface::GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const
{
	FShaderValueTypeHandle DebugDrawType = FOptimusDataTypeRegistry::Get().FindType(FName("FDebugDraw"))->ShaderValueType;

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadDebugDraw"))
		.AddReturnType(DebugDrawType);
}

BEGIN_SHADER_PARAMETER_STRUCT(FDebugDrawDataInterfaceParameters, )
	SHADER_PARAMETER(FMatrix44f, LocalToWorld)
	SHADER_PARAMETER_STRUCT_INCLUDE(ShaderPrint::FShaderParameters, ShaderPrintParameters)
END_SHADER_PARAMETER_STRUCT()

void UOptimusDebugDrawDataInterface::GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& InOutBuilder, FShaderParametersMetadataAllocations& InOutAllocations) const
{
	InOutBuilder.AddNestedStruct<FDebugDrawDataInterfaceParameters>(UID);
}

void UOptimusDebugDrawDataInterface::GetShaderHash(FString& InOutKey) const
{
	GetShaderFileHash(TEXT("/Plugin/Optimus/Private/DataInterfaceDebugDraw.ush"), EShaderPlatform::SP_PCD3D_SM5).AppendString(InOutKey);
}

void UOptimusDebugDrawDataInterface::GetHLSL(FString& OutHLSL) const
{
	OutHLSL += TEXT("#include \"/Plugin/Optimus/Private/DataInterfaceDebugDraw.ush\"\n");
}

UComputeDataProvider* UOptimusDebugDrawDataInterface::CreateDataProvider(TObjectPtr<UObject> InBinding, uint64 InInputMask, uint64 InOutputMask) const
{
	UOptimusDebugDrawDataProvider* Provider = NewObject<UOptimusDebugDrawDataProvider>();
	Provider->PrimitiveComponent = Cast<UPrimitiveComponent>(InBinding);
	Provider->DebugDrawParameters = DebugDrawParameters;
	return Provider;
}


bool UOptimusDebugDrawDataProvider::IsValid() const
{
	return true;
}

FComputeDataProviderRenderProxy* UOptimusDebugDrawDataProvider::GetRenderProxy()
{
	return new FOptimusDebugDrawDataProviderProxy(PrimitiveComponent, DebugDrawParameters);
}


FOptimusDebugDrawDataProviderProxy::FOptimusDebugDrawDataProviderProxy(UPrimitiveComponent* InPrimitiveComponent, FOptimusDebugDrawParameters const& InDebugDrawParameters)
{
	// Split LocalToWorld into a pre-translation and transform for large world coordinate support.
	FMatrix RenderMatrix = InPrimitiveComponent->GetRenderMatrix();
	PreViewTranslation = -RenderMatrix.GetOrigin();
	LocalToWorld = FMatrix44f(RenderMatrix.ConcatTranslation(PreViewTranslation));

	DebugDrawParameters = InDebugDrawParameters;
}

void FOptimusDebugDrawDataProviderProxy::AllocateResources(FRDGBuilder& GraphBuilder)
{
	// Force enable if requested.
	if (DebugDrawParameters.bForceEnable)
	{
		ShaderPrint::SetEnabled(true);
	}

	// Allocate ShaderPrint output buffers.
	ShaderPrint::FShaderPrintSetup Setup(FIntRect(0, 0, 1920, 1080));
	Setup.FontSize = DebugDrawParameters.FontSize;
	Setup.MaxLineCount = Setup.bEnabled ? DebugDrawParameters.MaxLineCount : 0;
	Setup.MaxTriangleCount = Setup.bEnabled ? DebugDrawParameters.MaxTriangleCount : 0;
	Setup.MaxValueCount = Setup.bEnabled ? DebugDrawParameters.MaxCharacterCount : 0;
	Setup.PreViewTranslation = PreViewTranslation;

	FShaderPrintData ShaderPrintData = ShaderPrint::CreateShaderPrintData(GraphBuilder, Setup);
	
	// Cache parameters for later GatherDispatchData().
	ShaderPrint::SetParameters(GraphBuilder, ShaderPrintData, ShaderPrintParameters);

	if (ShaderPrint::IsEnabled(ShaderPrintData))
	{
		// Enqueue for display at next view render.
		FFrozenShaderPrintData FrozenShaderPrintData = ShaderPrint::FreezeShaderPrintData(GraphBuilder, ShaderPrintData);
		ShaderPrint::SubmitShaderPrintData(FrozenShaderPrintData);
	}
}

void FOptimusDebugDrawDataProviderProxy::GatherDispatchData(FDispatchSetup const& InDispatchSetup, FCollectedDispatchData& InOutDispatchData)
{
	if (!ensure(InDispatchSetup.ParameterStructSizeForValidation == sizeof(FDebugDrawDataInterfaceParameters)))
	{
		return;
	}

	for (int32 InvocationIndex = 0; InvocationIndex < InDispatchSetup.NumInvocations; ++InvocationIndex)
	{
		FDebugDrawDataInterfaceParameters* Parameters = (FDebugDrawDataInterfaceParameters*)(InOutDispatchData.ParameterBuffer + InDispatchSetup.ParameterBufferOffset + InDispatchSetup.ParameterBufferStride * InvocationIndex);
		Parameters->LocalToWorld = LocalToWorld;
		Parameters->ShaderPrintParameters = ShaderPrintParameters;
	}
}
