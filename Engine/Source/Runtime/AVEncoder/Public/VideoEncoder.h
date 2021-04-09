// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VideoEncoderInput.h"
#include "Misc/FrameRate.h"

namespace AVEncoder
{
	class FVideoEncoder
	{
	public:
		virtual ~FVideoEncoder();

		struct FLayerConfig
		{
			uint32			Width = 0;
			uint32			Height = 0;
			uint32			MaxBitrate = 0;
			uint32			TargetBitrate = 0;
			uint32			QPMax = 0;
		};

		struct FInit : public FLayerConfig
		{
			uint32			MaxFramerate = 0;
			FFrameRate		TimeBase;
		};

		// --- setup

		virtual bool Setup(TSharedRef<FVideoEncoderInput> InInput, const FInit& InInit);
		virtual void Shutdown();

		// --- properties

		// --- layers


		// get the max. number of supported layers - the original resolution counts as a layer so at least one layer is supported.
		virtual uint32 GetMaxLayers() const { return 1; }
		// add a layer to encode - each consecutive layer must be smaller than the previous one
		virtual bool AddLayer(const FLayerConfig& InLayerConfig);
		// get the current number of layers
		uint32 GetNumLayers() const { return LayerInfos.Num(); }

		uint32 GetWidth(uint32 InLayerIndex) const { return (InLayerIndex < static_cast<uint32>(LayerInfos.Num())) ? LayerInfos[InLayerIndex]->Width : 0; }
		uint32 GetHeight(uint32 InLayerIndex) const { return (InLayerIndex < static_cast<uint32>(LayerInfos.Num())) ? LayerInfos[InLayerIndex]->Height : 0; }

		virtual void UpdateFrameRate(uint32 InMaxFramerate) {}
		virtual void UpdateLayerBitrate(uint32 InLayerIndex, uint32 InMaxBitRate, uint32 InTargetBitRate) {}
		virtual void UpdateLayerResolution(uint32 InLayerIndex, uint32 InWidth, uint32 InHeight) {}

		// --- input

		// new packet callback prototype void(uint32 LayerIndex, const FCodecPacket& Packet)
		using OnFrameEncodedCallback = TFunction<void(const FVideoEncoderInputFrame* /* InCompletedFrame */)>;

		struct FEncodeOptions
		{
			bool					bForceKeyFrame = false;
			OnFrameEncodedCallback	OnFrameEncoded;
		};

		virtual void Encode(const FVideoEncoderInputFrame* InFrame, const FEncodeOptions& InOptions) = 0;

		// --- output

		// new packet callback prototype void(uint32 LayerIndex, const FCodecPacket& Packet)
		using OnEncodedPacketCallback = TFunction<void(uint32 /* LayerIndex */, const FVideoEncoderInputFrame* /* Frame */, const FCodecPacket& /* Packet */)>;

		void SetOnEncodedPacket(OnEncodedPacketCallback InCallback) { OnEncodedPacket = MoveTemp(InCallback); }
		void ClearOnEncodedPacket() { OnEncodedPacket = nullptr; }

	protected:
		FVideoEncoder() = default;

		class FLayerInfo
		{
		public:
			explicit FLayerInfo(const FLayerConfig& InLayerConfig);
			virtual ~FLayerInfo() = default;

			uint32			Width;
			uint32			Height;
			uint32			MaxBitrate;
			uint32			TargetBitrate;
			uint32			QPMax;
		};

		TArray<FLayerInfo*>		LayerInfos;

		virtual FLayerInfo* CreateLayer(uint32 InLayerIndex, const FLayerInfo& InLayerInfo);
		virtual void DestroyLayer(FLayerInfo* InLayerInfo);

		OnEncodedPacketCallback			OnEncodedPacket;
	};
}