// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#if PLATFORM_WINDOWS

#include "WmfMediaHardwareVideoDecodingRendering.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GlobalShader.h"
#include "Logging/LogMacros.h"
#include "MediaShaders.h"
#include "PipelineStateCache.h"
#include "RHIStaticStates.h"
#include "SceneUtils.h"
#include "SceneInterface.h"
#include "ShaderParameterUtils.h"

#include "D3D11RHIPrivate.h"
#include "DynamicRHI.h"

#include "WmfMediaHardwareVideoDecodingTextureSample.h"
#include "WmfMediaPrivate.h"

#include "dxgi.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#include "d3d11.h"
#include "Windows/HideWindowsPlatformTypes.h"

#include "WmfMediaHardwareVideoDecodingShaders.h"

struct FRHICommandCopyResource final : public FRHICommand<FRHICommandCopyResource>
{
	TComPtr<ID3D11Texture2D> SampleTexture;
	FTexture2DRHIRef SampleDestinationTexture;

	FRHICommandCopyResource(ID3D11Texture2D* InSampleTexture, FRHITexture2D* InSampleDestinationTexture)
		: SampleTexture(InSampleTexture)
		, SampleDestinationTexture(InSampleDestinationTexture)
	{
	}

	void Execute(FRHICommandListBase& CmdList)
	{
		LLM_SCOPE(ELLMTag::VideoStreaming);
		ID3D11Device* D3D11Device = static_cast<ID3D11Device*>(GDynamicRHI->RHIGetNativeDevice());
		ID3D11DeviceContext* D3D11DeviceContext = nullptr;

		D3D11Device->GetImmediateContext(&D3D11DeviceContext);
		if (D3D11DeviceContext)
		{
			ID3D11Resource* DestinationTexture = reinterpret_cast<ID3D11Resource*>(SampleDestinationTexture->GetNativeResource());
			if (DestinationTexture)
			{
				TComPtr<IDXGIResource> OtherResource(nullptr);
				SampleTexture->QueryInterface(__uuidof(IDXGIResource), (void**)&OtherResource);

				if (OtherResource)
				{
					HANDLE SharedHandle = nullptr;
					if (OtherResource->GetSharedHandle(&SharedHandle) == S_OK)
					{
						if (SharedHandle != 0)
						{
							TComPtr<ID3D11Resource> SharedResource;
							D3D11Device->OpenSharedResource(SharedHandle, __uuidof(ID3D11Texture2D), (void**)&SharedResource);

							if (SharedResource)
							{
								TComPtr<IDXGIKeyedMutex> KeyedMutex;
								SharedResource->QueryInterface(_uuidof(IDXGIKeyedMutex), (void**)&KeyedMutex);

								if (KeyedMutex)
								{
									// Key is 1 : Texture as just been updated
									// Key is 2 : Texture as already been updated.
									// Do not wait to acquire key 1 since there is race no condition between writer and reader.
									if (KeyedMutex->AcquireSync(1, 0) == S_OK)
									{
										// Copy from shared texture of FWmfMediaSink device to Rendering device
										D3D11DeviceContext->CopyResource(DestinationTexture, SharedResource);
										KeyedMutex->ReleaseSync(2);
									}
									else
									{
										// If key 1 cannot be acquired, another reader is already copying the resource
										// and will release key with 2. 
										// Wait to acquire key 2.
										if (KeyedMutex->AcquireSync(2, INFINITE) == S_OK)
										{
											KeyedMutex->ReleaseSync(2);
										}
									}
								}
							}
						}
					}
				}
			}
			D3D11DeviceContext->Release();
		}
	}
};

bool FWmfMediaHardwareVideoDecodingParameters::ConvertTextureFormat_RenderThread(FWmfMediaHardwareVideoDecodingTextureSample* InSample, FTexture2DRHIRef InDstTexture)
{
	LLM_SCOPE(ELLMTag::VideoStreaming);
	if (InSample == nullptr || !InDstTexture.IsValid())
	{
		return false;
	}

	check(IsInRenderingThread());
	check(InSample);

	TComPtr<ID3D11Texture2D> SampleTexture = InSample->GetSourceTexture();

	ID3D11Device* D3D11Device = static_cast<ID3D11Device*>(GDynamicRHI->RHIGetNativeDevice());
	ID3D11DeviceContext* D3D11DeviceContext = nullptr;
			
	// Must access rendering device context to copy shared resource.
	D3D11Device->GetImmediateContext(&D3D11DeviceContext);
	if (D3D11DeviceContext)
	{
		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

		FRHIRenderPassInfo RPInfo(InDstTexture, ERenderTargetActions::DontLoad_Store);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("ConvertTextureFormat"));

		// Update viewport.
		RHICmdList.SetViewport(0, 0, 0.f, InSample->GetDim().X, InSample->GetDim().Y, 1.f);

		// Set the graphic pipeline state.
		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();

		FTexture2DRHIRef SampleDestinationTexture = InSample->GetOrCreateDestinationTexture();

		if (RHICmdList.Bypass())
		{
			FRHICommandCopyResource Cmd(SampleTexture, SampleDestinationTexture);
			Cmd.Execute(RHICmdList);
		}
		else
		{
			new (RHICmdList.AllocCommand<FRHICommandCopyResource>()) FRHICommandCopyResource(SampleTexture, SampleDestinationTexture);
		}

		TShaderMap<FGlobalShaderType>* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef< FHardwareVideoDecodingPS > PixelShaderNV12(GlobalShaderMap);
		TShaderMapRef< FHardwareVideoDecodingVS > VertexShaderNV12(GlobalShaderMap);

		TShaderMapRef< FHardwareVideoDecodingPassThroughPS > PixelShaderBGRA(GlobalShaderMap);
		TShaderMapRef< FHardwareVideoDecodingVS > VertexShaderBGRA(GlobalShaderMap);

		if (InSample->GetFormat() == EMediaTextureSampleFormat::CharNV12)
		{
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShaderNV12);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShaderNV12);
		}
		else if (InSample->GetFormat() == EMediaTextureSampleFormat::CharBGRA)
		{
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShaderBGRA);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShaderBGRA);
		}

		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		if (InSample->GetFormat() == EMediaTextureSampleFormat::CharNV12)
		{
			FShaderResourceViewRHIRef Y_SRV = RHICreateShaderResourceView(SampleDestinationTexture, 0, 1, PF_G8);
			FShaderResourceViewRHIRef UV_SRV = RHICreateShaderResourceView(SampleDestinationTexture, 0, 1, PF_R8G8);
			VertexShaderNV12->SetParameters(RHICmdList, VertexShaderNV12->GetVertexShader(), Y_SRV, UV_SRV, InSample->IsOutputSrgb());
			PixelShaderNV12->SetParameters(RHICmdList, PixelShaderNV12->GetPixelShader(), Y_SRV, UV_SRV, InSample->IsOutputSrgb());
		}
		else if (InSample->GetFormat() == EMediaTextureSampleFormat::CharBGRA)
		{
			FShaderResourceViewRHIRef SRV = RHICreateShaderResourceView(SampleDestinationTexture, 0, 1, PF_B8G8R8A8);
			VertexShaderBGRA->SetParameters(RHICmdList, VertexShaderBGRA->GetVertexShader(), SRV, InSample->IsOutputSrgb());
			PixelShaderBGRA->SetParameters(RHICmdList, PixelShaderBGRA->GetPixelShader(), SRV, InSample->IsOutputSrgb());
		}

		RHICmdList.DrawPrimitive(0, 2, 1);
		RHICmdList.EndRenderPass();

		D3D11DeviceContext->Release();
	}

	return true;
}

#endif
