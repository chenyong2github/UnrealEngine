// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NNXInferenceModel.generated.h"

/// Runtime inference format
UENUM()
enum class EMLInferenceFormat : uint8
{
	Invalid,
	ONNX,				//!< ONNX Open Neural Network Exchange
	ORT,				//!< ONNX Runtime (only for CPU)
	NNXRT				//!< NNX Runtime format
};

UCLASS(BlueprintType)
class NNXCORE_API UMLInferenceModel : public UObject
{
	GENERATED_BODY()

public:

	static UMLInferenceModel* CreateFromData(EMLInferenceFormat Format, TArrayView<uint8> Data);

	UMLInferenceModel();

	EMLInferenceFormat GetFormat() const;

	// Return model data
	const TArray<uint8>& GetData() const;

	// Return data size in bytes
	uint32 GetDataSize() const;

private:

	// TODO: Formats: ONNX, ORT, NNXRT
	UPROPERTY(EditAnywhere, Category = "Neural Network Inference")
	TArray<uint8>		Data;

	UPROPERTY(EditAnywhere, Category = "Neural Network Inference")
	EMLInferenceFormat	Format;
};
