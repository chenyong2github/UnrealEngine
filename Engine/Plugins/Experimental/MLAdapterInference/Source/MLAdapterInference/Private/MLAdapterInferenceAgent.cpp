// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLAdapterInferenceAgent.h"
#include "MLAdapterTypes.h"
#include "MLAdapterInferenceTypes.h"
#include "HAL/UnrealMemory.h"

void UMLAdapterInferenceAgent::Think(const float DeltaTime)
{
	if (Brain == nullptr)
	{
		UE_LOG(LogMLAdapterInference, Warning, TEXT("Agent beginning to Think but Brain is null"));
		return;
	}

	TArray<uint8> Buffer;
	FMLAdapterMemoryWriter Writer(Buffer);
	GetObservations(Writer);

	const float* DataPtr = (float*)Buffer.GetData();
	FMemory::Memcpy(Brain->GetInputDataPointerMutable(), DataPtr, Buffer.Num());

	Brain->Run();

	const FNeuralTensor& Tensor = Brain->GetOutputTensor();
	const TArray<uint8>& Data = Tensor.GetUnderlyingUInt8ArrayRef();
	FMLAdapterMemoryReader Reader(Data);
	DigestActions(Reader);
}
