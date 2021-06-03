// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ComputeFramework/ComputeDataInterface.h"
#include "ComputeFramework/ComputeDataProvider.h"
#include "DataInterfaceScene.generated.h"

class USceneComponent;

/** Compute Framework Data Interface for reading general scene data. */
UCLASS(Category = ComputeFramework)
class USceneDataInterface : public UComputeDataInterface
{
	GENERATED_BODY()

public:
	//~ Begin UComputeDataInterface Interface
	void GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const override;
	void GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& OutBuilder) const override;
	void GetHLSL(FString& OutHLSL) const override;
	//~ End UComputeDataInterface Interface
};

/** Compute Framework Data Provider for reading general scene data. */
UCLASS(BlueprintType, editinlinenew, Category = ComputeFramework)
class USceneDataProvider : public UComputeDataProvider
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Binding)
	TObjectPtr<USceneComponent> SceneComponent = nullptr;

	//~ Begin UComputeDataProvider Interface
	FComputeDataProviderRenderProxy* GetRenderProxy() override;
	//~ End UComputeDataProvider Interface
};

class FSceneDataProviderProxy : public FComputeDataProviderRenderProxy
{
public:
	FSceneDataProviderProxy(USceneComponent* SceneComponent);

	//~ Begin FComputeDataProviderRenderProxy Interface
	void GetBindings(int32 InvocationIndex, TCHAR const* UID, FBindings& OutBindings) const override;
	//~ End FComputeDataProviderRenderProxy Interface

private:
	float GameTime;
	uint32 FrameNumber;
};
