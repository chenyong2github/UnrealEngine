// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataInterfaces/DataInterfaceScene.h"

#include "Components/SceneComponent.h"
#include "ComputeFramework/ComputeKernelPermutationSet.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "CoreGlobals.h"
#include "Engine/World.h"
#include "SceneInterface.h"
#include "ShaderParameterMetadataBuilder.h"

FString USceneDataInterface::GetDisplayName() const
{
	return TEXT("Scene Data");
}

TArray<FOptimusCDIPinDefinition> USceneDataInterface::GetPinDefinitions() const
{
	TArray<FOptimusCDIPinDefinition> Defs;
	Defs.Add({"GameTime", "ReadGameTime"});
	Defs.Add({"GameTimeDelta", "ReadGameTimeDelta"});
	Defs.Add({"FrameNumber", "ReadFrameNumber"});
	return Defs;
}

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
		ReturnParam.ValueType = FShaderValueType::Get(EShaderFundamentalType::Float);
		Fn.ParamTypes.Add(ReturnParam);
		OutFunctions.Add(Fn);
	}
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("ReadGameTimeDelta");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = {};
		ReturnParam.ValueType = FShaderValueType::Get(EShaderFundamentalType::Float);
		Fn.ParamTypes.Add(ReturnParam);
		OutFunctions.Add(Fn);
	}
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("ReadFrameNumber");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = {};
		
		ReturnParam.ValueType = FShaderValueType::Get(EShaderFundamentalType::Uint);
		Fn.ParamTypes.Add(ReturnParam);
		OutFunctions.Add(Fn);
	}
}

BEGIN_SHADER_PARAMETER_STRUCT(FSceneDataInterfaceParameters, )
	SHADER_PARAMETER(float, GameTime)
	SHADER_PARAMETER(float, GameTimeDelta)
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

void USceneDataInterface::GetSourceTypes(TArray<UClass*>& OutSourceTypes) const
{
	OutSourceTypes.Add(USceneComponent::StaticClass());
}

UComputeDataProvider* USceneDataInterface::CreateDataProvider(TArrayView< TObjectPtr<UObject> > InSourceObjects, uint64 InInputMask, uint64 InOutputMask) const
{
	USceneDataProvider* Provider = NewObject<USceneDataProvider>();

	if (InSourceObjects.Num() == 1)
	{
		Provider->SceneComponent = Cast<USceneComponent>(InSourceObjects[0]);
	}

	return Provider;
}


FComputeDataProviderRenderProxy* USceneDataProvider::GetRenderProxy()
{
	return new FSceneDataProviderProxy(SceneComponent);
}


FSceneDataProviderProxy::FSceneDataProviderProxy(USceneComponent* SceneComponent)
{
	bool bUseSceneTime = SceneComponent != nullptr;
#if WITH_EDITOR
	// Don't tick time in Editor unless in PIE.
	if (GIsEditor)
	{
		bUseSceneTime &= (bUseSceneTime && SceneComponent->GetWorld() != nullptr && SceneComponent->GetWorld()->WorldType != EWorldType::Editor);
	}
#endif
	GameTime = bUseSceneTime ? SceneComponent->GetWorld()->TimeSeconds : 0;
	GameTimeDelta = bUseSceneTime ? SceneComponent->GetWorld()->DeltaTimeSeconds : 0;
	FrameNumber = bUseSceneTime ? SceneComponent->GetScene()->GetFrameNumber() : 0;
}

void FSceneDataProviderProxy::GetBindings(int32 InvocationIndex, TCHAR const* UID, FBindings& OutBindings) const
{
	FSceneDataInterfaceParameters Parameters;
	FMemory::Memset(&Parameters, 0, sizeof(Parameters));
	Parameters.GameTime = GameTime;
	Parameters.GameTimeDelta = GameTimeDelta;
	Parameters.FrameNumber = FrameNumber;

	TArray<uint8> ParamData;
	ParamData.SetNum(sizeof(Parameters));
	FMemory::Memcpy(ParamData.GetData(), &Parameters, sizeof(Parameters));
	OutBindings.Structs.Add(TTuple<FString, TArray<uint8> >(UID, MoveTemp(ParamData)));
}
