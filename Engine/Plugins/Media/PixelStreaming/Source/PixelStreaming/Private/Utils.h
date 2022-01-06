// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingPrivate.h"

#if PLATFORM_WINDOWS || PLATFORM_XBOXONE
	#include "Windows/WindowsPlatformMisc.h"
#elif PLATFORM_LINUX
	#include "Linux/LinuxPlatformMisc.h"
#endif

#include "RHI.h"
#include "Containers/StringConv.h"
#include "Containers/UnrealString.h"
#include "Templates/SharedPointer.h"
#include "Dom/JsonObject.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Templates/Atomic.h"
#include "WebRTCIncludes.h"
#include <string>
#include "CommonRenderResources.h"
#include "ScreenRendering.h"
#include "RHIStaticStates.h"
#include "Modules/ModuleManager.h"
#include "GenericPlatform/GenericPlatformProcess.h"
#include "PixelStreamingPrivate.h"
#include "Async/Async.h"

#if PLATFORM_WINDOWS || PLATFORM_XBOXONE
inline bool IsWindows7Plus()
{
	return FPlatformMisc::VerifyWindowsVersion(6, 1);
}

inline bool IsWindows8Plus()
{
	return FPlatformMisc::VerifyWindowsVersion(6, 2);
}
#endif

inline FString ToString(const std::string& Str)
{
	auto Conv = StringCast<TCHAR>(Str.c_str(), Str.size());
	FString Res{ Conv.Length(), Conv.Get() };
	return Res;
}

inline std::string to_string(const FString& Str)
{
	auto Ansi = StringCast<ANSICHAR>(*Str, Str.Len());
	std::string Res{ Ansi.Get(), static_cast<SIZE_T>(Ansi.Length()) };
	return Res;
}

inline FString ToString(const TSharedPtr<FJsonObject>& JsonObj, bool bPretty = true)
{
	FString Res;
	if (bPretty)
	{
		auto JsonWriter = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Res);
		FJsonSerializer::Serialize(JsonObj.ToSharedRef(), JsonWriter);
	}
	else
	{
		auto JsonWriter = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Res);
		FJsonSerializer::Serialize(JsonObj.ToSharedRef(), JsonWriter);
	}
	return Res;
}

inline const TCHAR* ToString(webrtc::PeerConnectionInterface::SignalingState Val)
{
	TCHAR const* SignallingStatesStr[] = {
		TEXT("Stable"),
		TEXT("HaveLocalOffer"),
		TEXT("HaveLocalPrAnswer"),
		TEXT("HaveRemoteOffer"),
		TEXT("HaveRemotePrAnswer"),
		TEXT("Closed")
	};

	return ensureMsgf(0 <= Val && Val <= webrtc::PeerConnectionInterface::kClosed, TEXT("Invalid `webrtc::PeerConnectionInterface::SignalingState` value: %d"), static_cast<uint32>(Val)) ? SignallingStatesStr[Val] : TEXT("Unknown");
}

inline const TCHAR* ToString(webrtc::PeerConnectionInterface::IceConnectionState Val)
{
	TCHAR const* IceConnectionStatsStr[] = {
		TEXT("IceConnectionNew"),
		TEXT("IceConnectionChecking"),
		TEXT("IceConnectionConnected"),
		TEXT("IceConnectionCompleted"),
		TEXT("IceConnectionFailed"),
		TEXT("IceConnectionDisconnected"),
		TEXT("IceConnectionClosed")
	};

	return ensureMsgf(
		0 <= Val && Val < webrtc::PeerConnectionInterface::kIceConnectionMax,
		TEXT("Invalid `webrtc::PeerConnectionInterface::IceConnectionState` value: %d"),
		static_cast<uint32>(Val))
			? IceConnectionStatsStr[Val]
			: TEXT("Unknown");
}

inline const TCHAR* ToString(webrtc::PeerConnectionInterface::IceGatheringState Val)
{
	TCHAR const* IceGatheringStatsStr[] = {
		TEXT("IceGatheringNew"),
		TEXT("IceGatheringGathering"),
		TEXT("IceGatheringComplete")
	};

	return ensureMsgf(
		0 <= Val && Val <= webrtc::PeerConnectionInterface::kIceGatheringComplete,
		TEXT("Invalid `webrtc::PeerConnectionInterface::IceGatheringState` value: %d"),
		static_cast<uint32>(Val))
			? IceGatheringStatsStr[Val]
			: TEXT("Unknown");
}

inline const TCHAR* ToString(webrtc::VideoFrameType FrameType)
{
	TCHAR const* FrameTypesStr[] = {
		TEXT("EmptyFrame"),
		TEXT("AudioFrameSpeech"),
		TEXT("AudioFrameCN"),
		TEXT("VideoFrameKey"),
		TEXT("VideoFrameDelta")
	};
	int FrameTypeInt = (int)FrameType;
	return ensureMsgf(
		0 <= FrameTypeInt && FrameTypeInt <= (int)webrtc::VideoFrameType::kVideoFrameDelta,
		TEXT("Invalid `webrtc::FrameType`: %d"), static_cast<uint32>(FrameType))
			? FrameTypesStr[FrameTypeInt]
			: TEXT("Unknown");
}

inline webrtc::SdpVideoFormat CreateH264Format(webrtc::H264::Profile profile, webrtc::H264::Level level)
{
	const absl::optional<std::string> profile_string =
		webrtc::H264::ProfileLevelIdToString(webrtc::H264::ProfileLevelId(profile, level));
	check(profile_string);
	return webrtc::SdpVideoFormat(
		cricket::kH264CodecName,
		{ { cricket::kH264FmtpProfileLevelId, *profile_string },
			{ cricket::kH264FmtpLevelAsymmetryAllowed, "1" },
			{ cricket::kH264FmtpPacketizationMode, "1" } });
}

// returns milliseconds part of current time, useful for logging
inline uint32 RtcTimeMs()
{
	return rtc::TimeMicros() / 1000 % 1000;
}

template <uint32 SmoothingPeriod>
class FSmoothedValue
{
public:
	double Get() const
	{
		return Value;
	}

	void Update(double NewValue)
	{
		Value = Value * (SmoothingPeriod - 1) / SmoothingPeriod + NewValue / SmoothingPeriod;
	}

	void Reset()
	{
		Value = 0;
	}

private:
	TAtomic<double> Value{ 0 };
};

inline void CopyTexture(FTexture2DRHIRef SourceTexture, FTexture2DRHIRef DestinationTexture, FGPUFenceRHIRef& CopyFence)
{
	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

	IRendererModule* RendererModule = &FModuleManager::GetModuleChecked<IRendererModule>("Renderer");

	// #todo-renderpasses there's no explicit resolve here? Do we need one?
	FRHIRenderPassInfo RPInfo(DestinationTexture, ERenderTargetActions::Load_Store);

	RHICmdList.BeginRenderPass(RPInfo, TEXT("CopyBackbuffer"));

	{
		RHICmdList.SetViewport(0, 0, 0.0f, DestinationTexture->GetSizeX(), DestinationTexture->GetSizeY(), 1.0f);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		// New engine version...
		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
		TShaderMapRef<FScreenPS> PixelShader(ShaderMap);

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

		if (DestinationTexture->GetSizeX() != SourceTexture->GetSizeX() || DestinationTexture->GetSizeY() != SourceTexture->GetSizeY())
		{
			PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Bilinear>::GetRHI(), SourceTexture);
		}
		else
		{
			PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Point>::GetRHI(), SourceTexture);
		}

		RendererModule->DrawRectangle(RHICmdList, 0, 0, // Dest X, Y
			DestinationTexture->GetSizeX(),				// Dest Width
			DestinationTexture->GetSizeY(),				// Dest Height
			0, 0,										// Source U, V
			1, 1,										// Source USize, VSize
			DestinationTexture->GetSizeXY(),			// Target buffer size
			FIntPoint(1, 1),							// Source texture size
			VertexShader, EDRF_Default);
	}

	RHICmdList.EndRenderPass();

	RHICmdList.WriteGPUFence(CopyFence);

	RHICmdList.ImmediateFlush(EImmediateFlushType::WaitForOutstandingTasksOnly);
}

inline size_t SerializeToBuffer(rtc::CopyOnWriteBuffer& Buffer, size_t Pos, const void* Data, size_t DataSize)
{
	FMemory::Memcpy(&Buffer[Pos], reinterpret_cast<const uint8_t*>(Data), DataSize);
	return Pos + DataSize;
}

inline void SendArbitraryData(rtc::scoped_refptr<webrtc::DataChannelInterface> DataChannel, const TArray<uint8>& DataBytes, const uint8 MessageType)
{
	if (!DataChannel)
	{
		return;
	}

	// int32 results in a maximum 4GB file (4,294,967,296 bytes)
	const int32 DataSize = DataBytes.Num();

	// Maximum size of a single buffer should be 16KB as this is spec compliant message length for a single data channel transmission
	const int32 MaxBufferBytes = 16 * 1024;
	const int32 MessageHeader = sizeof(MessageType) + sizeof(DataSize);
	const int32 MaxDataBytesPerMsg = MaxBufferBytes - MessageHeader;

	int32 BytesTransmitted = 0;

	while (BytesTransmitted < DataSize)
	{
		int32 RemainingBytes = DataSize - BytesTransmitted;
		int32 BytesToTransmit = FGenericPlatformMath::Min(MaxDataBytesPerMsg, RemainingBytes);

		rtc::CopyOnWriteBuffer Buffer(MessageHeader + BytesToTransmit);

		size_t Pos = 0;

		// Write message type
		Pos = SerializeToBuffer(Buffer, Pos, &MessageType, sizeof(MessageType));
		// Write size of payload
		Pos = SerializeToBuffer(Buffer, Pos, &DataSize, sizeof(DataSize));
		// Write the data bytes payload
		Pos = SerializeToBuffer(Buffer, Pos, DataBytes.GetData() + BytesTransmitted, BytesToTransmit);

		uint64_t BufferBefore = DataChannel->buffered_amount();
		while (BufferBefore + BytesToTransmit >= 16 * 1024 * 1024) // 16MB (WebRTC Data Channel buffer size)
		{
			FPlatformProcess::Sleep(0.000001f); // sleep 1 microsecond
			BufferBefore = DataChannel->buffered_amount();
		}

		if (!DataChannel->Send(webrtc::DataBuffer(Buffer, true)))
		{
			UE_LOG(PixelStreamer, Error, TEXT("Failed to send data channel packet"));
			return;
		}

		// Increment the number of bytes transmitted
		BytesTransmitted += BytesToTransmit;
	}
}

inline FTexture2DRHIRef CreateTexture(uint32 Width, uint32 Height)
{
	// Create empty texture
	FRHIResourceCreateInfo CreateInfo(TEXT("BlankTexture"));

	FTexture2DRHIRef Texture;
	FString RHIName = GDynamicRHI->GetName();

	if (RHIName == TEXT("Vulkan"))
	{
		Texture = GDynamicRHI->RHICreateTexture2D(Width, Height, EPixelFormat::PF_B8G8R8A8, 1, 1, TexCreate_RenderTargetable | TexCreate_External, ERHIAccess::Present, CreateInfo);
	}
	else
	{
		Texture = GDynamicRHI->RHICreateTexture2D(Width, Height, EPixelFormat::PF_B8G8R8A8, 1, 1, TexCreate_Shared | TexCreate_RenderTargetable, ERHIAccess::CopyDest, CreateInfo);
	}
	return Texture;
}
