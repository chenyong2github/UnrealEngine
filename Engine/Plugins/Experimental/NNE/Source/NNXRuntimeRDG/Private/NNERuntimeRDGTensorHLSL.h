// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NNXRuntimeRDG.h"
#include "NNECoreTypes.h"
#include "RHIGPUReadback.h"

using FCpuBufferRef = void*;

class FRDGBuilder;

namespace UE::NNIRuntimeRDG::Private::Hlsl
{

class FTensorHLSL : public NNX::FTensorRDG
{

private:

	FCpuBufferRef UploadBuffer{};
	FCpuBufferRef DownloadBuffer{};

	TUniquePtr<FRHIGPUBufferReadback> Readback;

public:

	FTensorHLSL() = default;

	FTensorHLSL(const NNX::FTensorRDG &TensorRDG)
		: FTensorHLSL(TensorRDG.GetName(), TensorRDG.GetDataType(), TensorRDG.GetShape())
	{
		this->PreparedData = TensorRDG.GetPreparedData<uint8>();//This copy the data, we should find another way
	}
	
	FTensorHLSL(const FString& Name, ENNETensorDataType DataType, const NNX::FTensorShape& Shape)
	{
		this->Name = Name;
		this->DataType = DataType;
		this->Shape = Shape;
		Volume = Shape.Volume();
		DataSize = (uint64)UE::NNECore::GetTensorDataTypeSizeInBytes(DataType) * Volume;
	}

	bool HasUploadBuffer() const { return UploadBuffer != FCpuBufferRef{}; }
	void SetUploadBuffer(FCpuBufferRef Inbuffer){ check(!HasPreparedData()); UploadBuffer = Inbuffer; }
	FCpuBufferRef GetUploadBuffer() const { return UploadBuffer; }

	bool HasDownloadBuffer() const { return DownloadBuffer != FCpuBufferRef{}; }
	void SetDownloadBuffer(FCpuBufferRef Inbuffer){ DownloadBuffer = Inbuffer; }
	FCpuBufferRef GetDownloadBuffer() const { return DownloadBuffer; }

	void EnqueueUploadRdg(FRDGBuilder& GraphBuilder);
	void EnqueueDownloadRdg(FRDGBuilder& GraphBuilder, bool bUseManualTransitions);

	void Resolve();
};

using FTensorHLSLRef = FTensorHLSL*;

} // namespace UE::NNIRuntimeRDG::Private::Hlsl