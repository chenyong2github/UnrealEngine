// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NNXRuntimeFormat.h"
#include "NNXInferenceModel.generated.h"

UCLASS(BlueprintType)
class NNXCORE_API UMLInferenceModel : public UObject
{
	GENERATED_BODY()

public:

	static UMLInferenceModel* CreateFromFormatDesc(const FNNIModelRaw& Model);
	
	UMLInferenceModel() = default;
	
	const FNNIModelRaw& GetFormatDesc() const;

private:
	UPROPERTY(EditAnywhere, Category = "Neural Network Inference")
	FNNIModelRaw FormatDesc;
};
