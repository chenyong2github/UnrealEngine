// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingPrivate.h"

#if PLATFORM_WINDOWS || PLATFORM_XBOXONE
#include "Windows/WindowsPlatformMisc.h"
#elif PLATFORM_LINUX
#include "Linux/LinuxPlatformMisc.h"
#endif

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
	TCHAR const* SignallingStatesStr[] = 
	{
		TEXT("Stable"),
		TEXT("HaveLocalOffer"),
		TEXT("HaveLocalPrAnswer"),
		TEXT("HaveRemoteOffer"),
		TEXT("HaveRemotePrAnswer"),
		TEXT("Closed")
	};

	return ensureMsgf(0 <= Val && Val <= webrtc::PeerConnectionInterface::kClosed, TEXT("Invalid `webrtc::PeerConnectionInterface::SignalingState` value: %d"), static_cast<uint32>(Val)) ?
		SignallingStatesStr[Val] :
		TEXT("Unknown");
}

inline const TCHAR* ToString(webrtc::PeerConnectionInterface::IceConnectionState Val)
{
	TCHAR const* IceConnectionStatsStr[] = 
	{
		TEXT("IceConnectionNew"),
		TEXT("IceConnectionChecking"),
		TEXT("IceConnectionConnected"),
		TEXT("IceConnectionCompleted"),
		TEXT("IceConnectionFailed"),
		TEXT("IceConnectionDisconnected"),
		TEXT("IceConnectionClosed")
	};

	return ensureMsgf(0 <= Val && Val < webrtc::PeerConnectionInterface::kIceConnectionMax, TEXT("Invalid `webrtc::PeerConnectionInterface::IceConnectionState` value: %d"), static_cast<uint32>(Val)) ?
		IceConnectionStatsStr[Val] :
		TEXT("Unknown");
}

inline const TCHAR* ToString(webrtc::PeerConnectionInterface::IceGatheringState Val)
{
	TCHAR const* IceGatheringStatsStr[] =
	{
		TEXT("IceGatheringNew"),
		TEXT("IceGatheringGathering"),
		TEXT("IceGatheringComplete")
	};

	return ensureMsgf(0 <= Val && Val <= webrtc::PeerConnectionInterface::kIceGatheringComplete, TEXT("Invalid `webrtc::PeerConnectionInterface::IceGatheringState` value: %d"), static_cast<uint32>(Val)) ?
		IceGatheringStatsStr[Val] :
		TEXT("Unknown");
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
	return ensureMsgf(0 <= FrameTypeInt && FrameTypeInt <= (int)webrtc::VideoFrameType::kVideoFrameDelta, TEXT("Invalid `webrtc::FrameType`: %d"), static_cast<uint32>(FrameType)) ?
		FrameTypesStr[FrameTypeInt] :
		TEXT("Unknown");
}

inline webrtc::SdpVideoFormat CreateH264Format(webrtc::H264::Profile profile, webrtc::H264::Level level)
{
	const absl::optional<std::string> profile_string =
		webrtc::H264::ProfileLevelIdToString(webrtc::H264::ProfileLevelId(profile, level));
	check(profile_string);
	return webrtc::SdpVideoFormat
	(
		cricket::kH264CodecName,
		{
			{cricket::kH264FmtpProfileLevelId, *profile_string},
			{cricket::kH264FmtpLevelAsymmetryAllowed, "1"},
			{cricket::kH264FmtpPacketizationMode, "1"}
		}
	);
}

// returns milliseconds part of current time, useful for logging
inline uint32 RtcTimeMs()
{
	return rtc::TimeMicros() / 1000 % 1000;
}

template<uint32 SmoothingPeriod>
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


inline void CopyTexture(const FTexture2DRHIRef& SourceTexture, FTexture2DRHIRef& DestinationTexture)
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

		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		if(DestinationTexture->GetSizeX() != SourceTexture->GetSizeX() || DestinationTexture->GetSizeY() != SourceTexture->GetSizeY())
		{
			PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Bilinear>::GetRHI(), SourceTexture);
		}
		else
		{
			PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Point>::GetRHI(), SourceTexture);
		}

		RendererModule->DrawRectangle(RHICmdList, 0, 0,                // Dest X, Y
		                              DestinationTexture->GetSizeX(),  // Dest Width
		                              DestinationTexture->GetSizeY(),  // Dest Height
		                              0, 0,                            // Source U, V
		                              1, 1,                            // Source USize, VSize
		                              DestinationTexture->GetSizeXY(), // Target buffer size
		                              FIntPoint(1, 1),                 // Source texture size
		                              VertexShader, EDRF_Default);
	}

	RHICmdList.EndRenderPass();
}

