// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RHI.h"
#include "CommonRenderResources.h"
#include "ScreenRendering.h"
#include "RHIStaticStates.h"
#include "Modules/ModuleManager.h"
#include "PixelStreamingPrivate.h"
#include "WebRTCIncludes.h"
#include "Async/Async.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIGPUReadback.h"
#include "PixelShaderUtils.h"
#include "Runtime/Renderer/Private/ScreenPass.h"

namespace UE::PixelStreaming
{
	template <typename T>
	void DoOnGameThread(T&& Func)
	{
		if (IsInGameThread())
		{
			Func();
		}
		else
		{
			AsyncTask(ENamedThreads::GameThread, [Func]() { Func(); });
		}
	}

	template <typename T>
	void DoOnGameThreadAndWait(uint32 Timeout, T&& Func)
	{
		if (IsInGameThread())
		{
			Func();
		}
		else
		{
			FEvent* TaskEvent = FPlatformProcess::GetSynchEventFromPool();
			AsyncTask(ENamedThreads::GameThread, [Func, TaskEvent]() {
				Func();
				TaskEvent->Trigger();
			});
			TaskEvent->Wait(Timeout);
			FPlatformProcess::ReturnSynchEventToPool(TaskEvent);
		}
	}

	inline webrtc::SdpVideoFormat CreateH264Format(webrtc::H264::Profile profile, webrtc::H264::Level level)
	{
		const absl::optional<std::string> ProfileString =
			webrtc::H264::ProfileLevelIdToString(webrtc::H264::ProfileLevelId(profile, level));
		check(ProfileString);
		return webrtc::SdpVideoFormat(
			cricket::kH264CodecName,
			{ { cricket::kH264FmtpProfileLevelId, *ProfileString },
				{ cricket::kH264FmtpLevelAsymmetryAllowed, "1" },
				{ cricket::kH264FmtpPacketizationMode, "1" } });
	}

	inline void MemCpyStride(void* Dest, const void* Src, size_t DestStride, size_t SrcStride, size_t Height)
	{
		char* DestPtr = static_cast<char*>(Dest);
		const char* SrcPtr = static_cast<const char*>(Src);
		size_t Row = Height;
		while (Row--)
		{
			FMemory::Memcpy(DestPtr + DestStride * Row, SrcPtr + SrcStride * Row, DestStride);
		}
	}

	inline FTextureRHIRef CreateRHITexture(uint32 Width, uint32 Height)
	{
		// Create empty texture
		FRHITextureCreateDesc Desc =
			FRHITextureCreateDesc::Create2D(TEXT("PixelStreamingBlankTexture"), Width, Height, PF_B8G8R8A8)
			.SetClearValue(FClearValueBinding::None)
			.SetFlags(ETextureCreateFlags::RenderTargetable);

		if (RHIGetInterfaceType() == ERHIInterfaceType::Vulkan)
		{
			Desc.AddFlags(ETextureCreateFlags::External)
				.SetInitialState(ERHIAccess::Present);
		}
		else
		{
			Desc.AddFlags(ETextureCreateFlags::Shared)
				.SetInitialState(ERHIAccess::CopyDest);
		}

		return GDynamicRHI->RHICreateTexture(Desc);
	}

	inline FTextureRHIRef CreateCPUReadbackTexture(uint32 Width, uint32 Height)
	{
		// Create empty texture
		FRHITextureCreateDesc Desc =
			FRHITextureCreateDesc::Create2D(TEXT("PixelStreamingBlankTexture"), Width, Height, PF_B8G8R8A8)
			.SetClearValue(FClearValueBinding::None)
			.SetFlags(ETextureCreateFlags::HideInVisualizeTexture | ETextureCreateFlags::CPUReadback);

		if (RHIGetInterfaceType() == ERHIInterfaceType::Vulkan)
		{
			Desc.SetInitialState(ERHIAccess::Present);
		}
		else
		{
			Desc.SetInitialState(ERHIAccess::CopyDest);
		}

		return GDynamicRHI->RHICreateTexture(Desc);
	}

	inline void CopyTextureToRHI(FTextureRHIRef SourceTexture, FTextureRHIRef DestinationTexture, FGPUFenceRHIRef& CopyFence)
	{
		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

		IRendererModule* RendererModule = &FModuleManager::GetModuleChecked<IRendererModule>("Renderer");

		// #todo-renderpasses there's no explicit resolve here? Do we need one?
		FRHIRenderPassInfo RPInfo(DestinationTexture, ERenderTargetActions::Load_Store);

		RHICmdList.BeginRenderPass(RPInfo, TEXT("CopyBackbuffer"));

		{
			RHICmdList.SetViewport(0, 0, 0.0f, DestinationTexture->GetDesc().Extent.X, DestinationTexture->GetDesc().Extent.Y, 1.0f);

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

			if (DestinationTexture->GetDesc().Extent.X != SourceTexture->GetDesc().Extent.X || DestinationTexture->GetDesc().Extent.Y != SourceTexture->GetDesc().Extent.Y)
			{
				PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Bilinear>::GetRHI(), SourceTexture);
			}
			else
			{
				PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Point>::GetRHI(), SourceTexture);
			}

			FIntPoint TargetBufferSize(DestinationTexture->GetDesc().Extent.X, DestinationTexture->GetDesc().Extent.Y);
			RendererModule->DrawRectangle(RHICmdList, 0, 0, // Dest X, Y
				DestinationTexture->GetDesc().Extent.X,		// Dest Width
				DestinationTexture->GetDesc().Extent.Y,		// Dest Height
				0, 0,										// Source U, V
				1, 1,										// Source USize, VSize
				TargetBufferSize,							// Target buffer size
				FIntPoint(1, 1),							// Source texture size
				VertexShader, EDRF_Default);
		}

		RHICmdList.EndRenderPass();

		RHICmdList.WriteGPUFence(CopyFence);

		RHICmdList.ImmediateFlush(EImmediateFlushType::WaitForOutstandingTasksOnly);
	}

	inline void CopyTextureToReadbackTexture(FTextureRHIRef SourceTexture, TSharedPtr<FRHIGPUTextureReadback> GPUTextureReadback, void* OutBuffer)
	{
		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
		FRDGBuilder GraphBuilder(RHICmdList);

		FRDGTextureRef RDGSourceTexture = RegisterExternalTexture(GraphBuilder, SourceTexture, TEXT("SourceCopyTextureToReadbackTexture"));
		FRDGTextureRef RDGStagingTexture = nullptr;

		if(RDGSourceTexture->Desc.Format != EPixelFormat::PF_B8G8R8A8)
		{
			// We need the pixel format to be BGRA8 so we first draw it to a staging texture
			{
				const FRDGTextureDesc Desc(FRDGTextureDesc::Create2D(RDGSourceTexture->Desc.Extent, PF_B8G8R8A8, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_RenderTargetable));
				RDGStagingTexture = GraphBuilder.CreateTexture(Desc, TEXT("StagingCopyTextureToReadbackTexture"));
			}

			FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

			TShaderMapRef<FScreenPassVS> VertexShader(ShaderMap);

			// Setup the pixel shader
			TShaderMapRef<FCopyRectPS> PixelShader(ShaderMap);

			FCopyRectPS::FParameters* PixelShaderParameters = GraphBuilder.AllocParameters<FCopyRectPS::FParameters>();
			PixelShaderParameters->InputTexture = RDGSourceTexture;
			PixelShaderParameters->InputSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			PixelShaderParameters->RenderTargets[0] = FRenderTargetBinding(RDGStagingTexture, ERenderTargetLoadAction::ELoad);

			ClearUnusedGraphResources(PixelShader, PixelShaderParameters);

			// We are not doing any clever blending stuff so we just use defaults here
			FRHIBlendState* BlendState = FScreenPassPipelineState::FDefaultBlendState::GetRHI();
			FRHIDepthStencilState* DepthStencilState = FScreenPassPipelineState::FDefaultDepthStencilState::GetRHI();

			// Create the pipline state that will execute
			const FScreenPassPipelineState PipelineState(VertexShader, PixelShader, BlendState, DepthStencilState);

			// Add the pass the the graph builder
			GraphBuilder.AddPass(
				RDG_EVENT_NAME("PixelStreamingChangePixelFormat"),
				PixelShaderParameters,
				ERDGPassFlags::Raster,
				[PipelineState, Extent = RDGSourceTexture->Desc.Extent, PixelShader, PixelShaderParameters](FRHICommandList& RHICmdList)
				{
					PipelineState.Validate();
					
					RHICmdList.SetViewport(0.0f, 0.0f, 0.0f, Extent.X, Extent.Y, 1.0f);

					SetScreenPassPipelineState(RHICmdList, PipelineState);

					SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *PixelShaderParameters);

					DrawRectangle(
						RHICmdList,
						0, 0, Extent.X, Extent.Y,
						0, 0, Extent.X, Extent.Y,
						Extent,
						Extent,
						PipelineState.VertexShader,
						EDRF_UseTriangleOptimization);
				});
		}
		else
		{
			// Otherwise if the PixelFormat is already BGRA8 we can just use the SourceTexture as the staging texture
			RDGStagingTexture = RDGSourceTexture;
		}

		// Do the copy from staging RBGA8 texture into the readback
		AddEnqueueCopyPass(GraphBuilder, GPUTextureReadback.Get(), RDGStagingTexture);

		GraphBuilder.Execute();

		// Lock and copy out the content of the TextureReadback to the CPU
		int32 OutRowPitchInPixels;
		int32 BlockSize = GPixelFormats[RDGStagingTexture->Desc.Format].BlockBytes;
		void* ResultsBuffer = GPUTextureReadback->Lock(OutRowPitchInPixels);
		if (RDGSourceTexture->Desc.Extent.X == OutRowPitchInPixels)
		{
			// Source pixel width is the same as the stride of the result buffer (ie no padding), we can do a plain memcpy
			FPlatformMemory::Memcpy(OutBuffer, ResultsBuffer, (RDGSourceTexture->Desc.Extent.X * RDGSourceTexture->Desc.Extent.Y * BlockSize));
		} 
		else
		{
			// Source pixel width differs from the stride of the result buffer (ie padding), do a memcpy that accounts for this
			MemCpyStride(OutBuffer, ResultsBuffer, RDGSourceTexture->Desc.Extent.X * BlockSize, OutRowPitchInPixels * BlockSize, RDGSourceTexture->Desc.Extent.Y); 
		}
		GPUTextureReadback->Unlock();
	}

	inline size_t SerializeToBuffer(rtc::CopyOnWriteBuffer& Buffer, size_t Pos, const void* Data, size_t DataSize)
	{
		FMemory::Memcpy(&Buffer[Pos], reinterpret_cast<const uint8_t*>(Data), DataSize);
		return Pos + DataSize;
	}
} // namespace UE::PixelStreaming