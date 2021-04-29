// Copyright Epic Games, Inc. All Rights Reserved.

#include "VideoEncoder.h"
#include "VideoEncoderCommon.h"

DEFINE_LOG_CATEGORY(LogVideoEncoder);

namespace AVEncoder
{

FVideoEncoder::~FVideoEncoder()
{
	for (int32 LayerIndex = LayerInfos.Num(); LayerIndex-- > 0; )
	{
		DestroyLayer(LayerInfos[LayerIndex]);
	}
}

bool FVideoEncoder::Setup(TSharedRef<FVideoEncoderInput> InInput, const FInit& InInit)
{
	bool		bSuccess = false;
	if (GetNumLayers() == 0)
	{
		bSuccess = AddLayer(InInit);
	}
	return bSuccess;
}

void FVideoEncoder::Shutdown()
{
}

bool FVideoEncoder::AddLayer(const FLayerConfig& InLayerConfig)
{
	if (static_cast<uint32>(LayerInfos.Num()) >= GetMaxLayers())
	{
		UE_LOG(LogVideoEncoder, Error, TEXT("Encoder does not support more than %d layers."), GetMaxLayers());
		return false;
	}
	FLayerInfo		LayerInfo(InLayerConfig);
	FLayerInfo* NewLayerInfo = CreateLayer(LayerInfos.Num(), LayerInfo);
	if (NewLayerInfo)
	{
		LayerInfos.Push(NewLayerInfo);
	}
	return NewLayerInfo != nullptr;
}

FVideoEncoder::FLayerInfo* FVideoEncoder::CreateLayer(uint32 InLayerIndex, const FLayerInfo& InLayerInfo)
{
	return new FLayerInfo(InLayerInfo);
}

void FVideoEncoder::DestroyLayer(FLayerInfo* InLayerInfo)
{
	delete InLayerInfo;
}

FVideoEncoder::FLayerInfo::FLayerInfo(const FLayerConfig& InLayerConfig)
	: Width(InLayerConfig.Width)
	, Height(InLayerConfig.Height)
	, MaxBitrate(InLayerConfig.MaxBitrate)
	, TargetBitrate(InLayerConfig.TargetBitrate)
	, QPMax(InLayerConfig.QPMax)
{
}

} /* namespace AVEncoder */
