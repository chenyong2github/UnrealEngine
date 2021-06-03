// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataInterfaces/DataInterfaceScene.h"

#include "Components/SceneComponent.h"
#include "ComputeFramework/ComputeKernelPermutationSet.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "Engine/World.h"
#include "SceneInterface.h"
#include "ShaderParameterMetadataBuilder.h"

void USceneDataInterface::GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const
{
	// Functions must match those exposed in data interface shader code.
	// todo[CF]: Make these easier to write. Maybe even get from shader code reflection?
	// todo[CF]: Expose other general scene information here.
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("ReadGameTime");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = {};
		ReturnParam.FundamentalType = EShaderFundamentalType::Float;
		ReturnParam.DimType = EShaderFundamentalDimensionType::Scalar;
		Fn.ParamTypes.Add(ReturnParam);
		OutFunctions.Add(Fn);
	}
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("ReadFrameNumber");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = {};
		ReturnParam.FundamentalType = EShaderFundamentalType::Uint;
		ReturnParam.DimType = EShaderFundamentalDimensionType::Scalar;
		Fn.ParamTypes.Add(ReturnParam);
		OutFunctions.Add(Fn);
	}
}

BEGIN_SHADER_PARAMETER_STRUCT(FSceneDataInterfaceParameters, )
	SHADER_PARAMETER(float, GameTime)
	SHADER_PARAMETER(uint32, FrameNumber)
END_SHADER_PARAMETER_STRUCT()

void USceneDataInterface::GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& OutBuilder) const
{
	OutBuilder.AddNestedStruct<FSceneDataInterfaceParameters>(UID);
}

void USceneDataInterface::GetHLSL(FString& OutHLSL) const
{
	OutHLSL += TEXT("#include \"/Plugin/Optimus/Private/DataInterfaceScene.ush\"\n");
}

FComputeDataProviderRenderProxy* USceneDataProvider::GetRenderProxy()
{
	return new FSceneDataProviderProxy(SceneComponent);
}

FSceneDataProviderProxy::FSceneDataProviderProxy(USceneComponent* SceneComponent)
{
	GameTime = SceneComponent != nullptr ? SceneComponent->GetWorld()->TimeSeconds : 0;
	FrameNumber = SceneComponent != nullptr ? SceneComponent->GetScene()->GetFrameNumber() : 0;
}

void FSceneDataProviderProxy::GetBindings(int32 InvocationIndex, TCHAR const* UID, FBindings& OutBindings) const
{
	FSceneDataInterfaceParameters Parameters;
	FMemory::Memset(&Parameters, 0, sizeof(Parameters));
	Parameters.GameTime = GameTime;
	Parameters.FrameNumber = FrameNumber;

	TArray<uint8> ParamData;
	ParamData.SetNum(sizeof(Parameters));
	FMemory::Memcpy(ParamData.GetData(), &Parameters, sizeof(Parameters));
	OutBindings.Structs.Add(TTuple<FString, TArray<uint8> >(UID, MoveTemp(ParamData)));
}
