// Copyright Epic Games, Inc. All Rights Reserved.

#include "HoloLensCameraImageTexture.h"

#include "GlobalShader.h"
#include "RenderUtils.h"
#include "RHIStaticStates.h"
#include "PipelineStateCache.h"
#include "ShaderParameterUtils.h"
#include "SceneUtils.h"

#if SUPPORTS_WINDOWS_MIXED_REALITY_AR
	#include "Windows/AllowWindowsPlatformTypes.h"
	#include <Windows.h>
	#include <d3d11.h>
	#include "Windows/HideWindowsPlatformTypes.h"
#endif

struct FHoloLensCameraImageConversionVertex
{
	FVector4 Position;
	FVector2D TextureCoordinate;

	FHoloLensCameraImageConversionVertex() { }

	FHoloLensCameraImageConversionVertex(const FVector4& InPosition, const FVector2D& InTextureCoordinate)
		: Position(InPosition)
		, TextureCoordinate(InTextureCoordinate)
	{ }
};

class FHoloLensCameraImageConversionVertexDeclaration :
	public FRenderResource
{
public:
	FVertexDeclarationRHIRef VertexDeclarationRHI;

	virtual void InitRHI() override
	{
		FVertexDeclarationElementList Elements;
		Elements.Add(FVertexElement(0, 0, VET_Float4, 0, sizeof(FVector4)));
		VertexDeclarationRHI = PipelineStateCache::GetOrCreateVertexDeclaration(Elements);
	}

	virtual void ReleaseRHI() override
	{
		VertexDeclarationRHI.SafeRelease();
	}
};

TGlobalResource<FHoloLensCameraImageConversionVertexDeclaration> GHoloLensCameraImageConversionVertexDeclaration;

/**
 * A dummy vertex buffer to bind when rendering. This prevents some D3D debug warnings
 * about zero-element input layouts but is not strictly required.
 */
class FDummyIndexBuffer :
	public FIndexBuffer
{
public:

	virtual void InitRHI() override
	{
		// Setup index buffer
		int NumIndices = 6;
		FRHIResourceCreateInfo CreateInfo;
		IndexBufferRHI = RHICreateIndexBuffer(sizeof(uint16), sizeof(uint16) * NumIndices, BUF_Static, CreateInfo);
		void* VoidPtr = RHILockIndexBuffer(IndexBufferRHI, 0, sizeof(uint16) * NumIndices, RLM_WriteOnly);
		uint16* pIndices = reinterpret_cast<uint16*>(VoidPtr);

		pIndices[0] = 0;
		pIndices[1] = 1;
		pIndices[2] = 2;
		pIndices[3] = 0;
		pIndices[4] = 2;
		pIndices[5] = 3;

		RHIUnlockIndexBuffer(IndexBufferRHI);
	}
};
TGlobalResource<FDummyIndexBuffer> GHoloLensCameraImageConversionIndexBuffer;

class FDummyVertexBuffer :
	public FVertexBuffer
{
public:

	virtual void InitRHI() override
	{
		FRHIResourceCreateInfo CreateInfo;
		void* BufferData = nullptr;
		VertexBufferRHI = RHICreateAndLockVertexBuffer(sizeof(FVector4) * 4, BUF_Static, CreateInfo, BufferData);
		FVector4* DummyContents = (FVector4*)BufferData;
		DummyContents[0] = FVector4(0.f, 0.f, 0.f, 0.f);
		DummyContents[1] = FVector4(1.f, 0.f, 0.f, 0.f);
		DummyContents[2] = FVector4(0.f, 1.f, 0.f, 0.f);
		DummyContents[3] = FVector4(1.f, 1.f, 0.f, 0.f);
		RHIUnlockVertexBuffer(VertexBufferRHI);
	}
};
TGlobalResource<FDummyVertexBuffer> GHoloLensCameraImageConversionVertexBuffer;

/** Shaders to render our post process material */
class FHoloLensCameraImageConversionVS :
	public FGlobalShader 
{
	DECLARE_SHADER_TYPE(FHoloLensCameraImageConversionVS, Global);

public:

	FHoloLensCameraImageConversionVS() { }
	FHoloLensCameraImageConversionVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::ES3_1);
	}
};

IMPLEMENT_SHADER_TYPE(, FHoloLensCameraImageConversionVS, TEXT("/Plugin/WindowsMixedReality/Private/HoloLensCameraImageConversion.usf"), TEXT("MainVS"), SF_Vertex)

class FHoloLensCameraImageConversionPS :
	public FGlobalShader
{
	DECLARE_SHADER_TYPE(FHoloLensCameraImageConversionPS, Global);

public:

	FHoloLensCameraImageConversionPS() {}
	FHoloLensCameraImageConversionPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		TextureY.Bind(Initializer.ParameterMap, TEXT("TextureY"));
		TextureUV.Bind(Initializer.ParameterMap, TEXT("TextureUV"));

		PointClampedSamplerY.Bind(Initializer.ParameterMap, TEXT("PointClampedSamplerY"));
		BilinearClampedSamplerUV.Bind(Initializer.ParameterMap, TEXT("BilinearClampedSamplerUV"));
	}

	void SetParameters(FRHICommandListImmediate& RHICmdList, const FShaderResourceViewRHIRef& InTextureY, const FShaderResourceViewRHIRef& InTextureUV)
	{
		SetSRVParameter(RHICmdList, RHICmdList.GetBoundPixelShader(), TextureY, InTextureY);
		SetSRVParameter(RHICmdList, RHICmdList.GetBoundPixelShader(), TextureUV, InTextureUV);

		SetSamplerParameter(RHICmdList, RHICmdList.GetBoundPixelShader(), PointClampedSamplerY, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
		SetSamplerParameter(RHICmdList, RHICmdList.GetBoundPixelShader(), BilinearClampedSamplerUV, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::ES3_1);
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, TextureY);
	LAYOUT_FIELD(FShaderResourceParameter, TextureUV);
	LAYOUT_FIELD(FShaderResourceParameter, PointClampedSamplerY);
	LAYOUT_FIELD(FShaderResourceParameter, BilinearClampedSamplerUV);
};

IMPLEMENT_SHADER_TYPE(, FHoloLensCameraImageConversionPS, TEXT("/Plugin/WindowsMixedReality/Private/HoloLensCameraImageConversion.usf"), TEXT("MainPS"), SF_Pixel)

#if SUPPORTS_WINDOWS_MIXED_REALITY_AR

/** Resource class to do all of the setup work on the render thread */
class FHoloLensCameraImageResource :
	public FTextureResource
{
public:
	FHoloLensCameraImageResource(UHoloLensCameraImageTexture* InOwner)
		: LastFrameNumber(0)
		, Owner(InOwner)
	{
	}

	virtual ~FHoloLensCameraImageResource()
	{
	}

	/**
	 * Called when the resource is initialized. This is only called by the rendering thread.
	 */
	virtual void InitRHI() override
	{
		check(IsInRenderingThread());

		FSamplerStateInitializerRHI SamplerStateInitializer(SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp);
		SamplerStateRHI = RHICreateSamplerState(SamplerStateInitializer);

		bool bDidConvert = false;
		if (CameraImageHandle != INVALID_HANDLE_VALUE)
		{
			// Open the shared texture from the HoloLens camera on Unreal's d3d device.
			ID3D11Device* D3D11Device = static_cast<ID3D11Device*>(GDynamicRHI->RHIGetNativeDevice());
			TComPtr<ID3D11DeviceContext> D3D11DeviceContext = nullptr;
			D3D11Device->GetImmediateContext(&D3D11DeviceContext);
			if (D3D11DeviceContext == nullptr)
			{
				CloseHandle(CameraImageHandle);
				CameraImageHandle = INVALID_HANDLE_VALUE;

				return;
			}

			TComPtr<ID3D11Texture2D> cameraImageTexture;
			TComPtr<IDXGIResource1> cameraImageResource(NULL);
			((ID3D11Device1*)D3D11Device)->OpenSharedResource1(CameraImageHandle, __uuidof(IDXGIResource1), (void**)&cameraImageResource);
			cameraImageResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)(&cameraImageTexture));

			D3D11_TEXTURE2D_DESC Desc;
			cameraImageTexture->GetDesc(&Desc);

			Size.X = Desc.Width;
			Size.Y = Desc.Height;

			// Create the copy target
			{
				FRHIResourceCreateInfo CreateInfo;
				CopyTextureRef = RHICreateTexture2D(Size.X, Size.Y, PF_NV12, 1, 1, TexCreate_Dynamic | TexCreate_ShaderResource, CreateInfo);
			}
			// Create the render target
			{
				FRHIResourceCreateInfo CreateInfo;
				TRefCountPtr<FRHITexture2D> DummyTexture2DRHI;
				// Create our render target that we'll convert to
				RHICreateTargetableShaderResource2D(Size.X, Size.Y, PF_B8G8R8A8, 1, TexCreate_Dynamic, TexCreate_RenderTargetable, false, CreateInfo, DecodedTextureRef, DummyTexture2DRHI);
			}

			if (PerformCopy(cameraImageTexture, D3D11DeviceContext))
			{
				PerformConversion();
				bDidConvert = true;
			}
			// Now that the conversion is done, we can get rid of our refs
			CloseHandle(CameraImageHandle);
			CameraImageHandle = INVALID_HANDLE_VALUE;
			CopyTextureRef.SafeRelease();
		}

		// Default to an empty 1x1 texture if we don't have a camera image or failed to convert
		if (!bDidConvert)
		{
			FRHIResourceCreateInfo CreateInfo;
			Size.X = Size.Y = 1;
			DecodedTextureRef = RHICreateTexture2D(Size.X, Size.Y, PF_B8G8R8A8, 1, 1, TexCreate_ShaderResource, CreateInfo);
		}

		TextureRHI = DecodedTextureRef;
		TextureRHI->SetName(Owner->GetFName());
		RHIBindDebugLabelName(TextureRHI, *Owner->GetName());
		RHIUpdateTextureReference(Owner->TextureReference.TextureReferenceRHI, TextureRHI);
	}

	virtual void ReleaseRHI() override
	{
		RHIUpdateTextureReference(Owner->TextureReference.TextureReferenceRHI, nullptr);
		if (CameraImageHandle != INVALID_HANDLE_VALUE)
		{
			CloseHandle(CameraImageHandle);
			CameraImageHandle = INVALID_HANDLE_VALUE;
		}
		CopyTextureRef.SafeRelease();
		DecodedTextureRef.SafeRelease();
		FTextureResource::ReleaseRHI();
	}

	/** Returns the width of the texture in pixels. */
	virtual uint32 GetSizeX() const override
	{
		return Size.X;
	}

	/** Returns the height of the texture in pixels. */
	virtual uint32 GetSizeY() const override
	{
		return Size.Y;
	}

	/** Render thread update of the texture so we don't get 2 updates per frame on the render thread */
	void Init_RenderThread(HANDLE handle)
	{
		check(IsInRenderingThread());
		if (LastFrameNumber != GFrameNumber)
		{
			LastFrameNumber = GFrameNumber;
			ReleaseRHI();
			CameraImageHandle = handle;
			InitRHI();
		}
	}

private:
	/** Copy CameraImage to our CopyTextureRef using the GPU */
	bool PerformCopy(const TComPtr<ID3D11Texture2D>& texture, const TComPtr<ID3D11DeviceContext>& context)
	{
		// These must already be prepped
		if (texture == nullptr 
			|| context == nullptr
			|| !CopyTextureRef.IsValid())
		{
			return false;
		}
		// Get the underlying interface for the texture we are copying to
		TComPtr<ID3D11Resource> CopyTexture = reinterpret_cast<ID3D11Resource*>(CopyTextureRef->GetNativeResource());
		if (CopyTexture == nullptr)
		{
			return false;
		}

		context->CopyResource(CopyTexture.Get(), texture);

		return true;
	}

	/** Runs a shader to convert YUV to RGB */
	void PerformConversion()
	{
		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
		SCOPED_DRAW_EVENT(RHICmdList, HoloLensCameraImageConversion);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		FRHITexture* RenderTarget = DecodedTextureRef.GetReference();

		FRHIRenderPassInfo RPInfo(RenderTarget, ERenderTargetActions::Load_Store);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("HoloLensCameraImageConversion"));
		{
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			RHICmdList.SetViewport(0, 0, 0.f, Size.X, Size.Y, 1.f);

			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.BlendState = TStaticBlendStateWriteMask<CW_RGBA, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE>::GetRHI();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
			TShaderMapRef<FHoloLensCameraImageConversionVS> VertexShader(GlobalShaderMap);
			TShaderMapRef<FHoloLensCameraImageConversionPS> PixelShader(GlobalShaderMap);

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GHoloLensCameraImageConversionVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			FShaderResourceViewRHIRef Y_SRV = RHICreateShaderResourceView(CopyTextureRef, 0, 1, PF_G8);
			FShaderResourceViewRHIRef UV_SRV = RHICreateShaderResourceView(CopyTextureRef, 0, 1, PF_R8G8);

			PixelShader->SetParameters(RHICmdList, Y_SRV, UV_SRV);

			RHICmdList.SetStreamSource(0, GHoloLensCameraImageConversionVertexBuffer.VertexBufferRHI, 0);
			RHICmdList.SetViewport(0, 0, 0.f, Size.X, Size.Y, 1.f);
			RHICmdList.DrawIndexedPrimitive(GHoloLensCameraImageConversionIndexBuffer.IndexBufferRHI, 0, 0, 4, 0, 2, 1);
		}
		RHICmdList.EndRenderPass();
		RHICmdList.Transition(FRHITransitionInfo(DecodedTextureRef, ERHIAccess::Unknown, ERHIAccess::SRVGraphics));
	}

	/** The size we get from the incoming camera image */
	FIntPoint Size;

	/** The raw camera image from the HoloLens which we copy to our texture to allow it to be quickly released */
	HANDLE CameraImageHandle = INVALID_HANDLE_VALUE;
	/** The nv12 texture that we copy into so we don't block the camera from being able to send frames */
	FTexture2DRHIRef CopyTextureRef;
	/** The texture that we actually render with which is populated via a shader that converts nv12 to rgba */
	FTexture2DRHIRef DecodedTextureRef;
	/** The last frame we were updated on */
	uint32 LastFrameNumber;

	const UHoloLensCameraImageTexture* Owner;
};

#endif


UHoloLensCameraImageTexture::UHoloLensCameraImageTexture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if SUPPORTS_WINDOWS_MIXED_REALITY_AR
	, LastUpdateFrame(0)
#endif
{
}

void UHoloLensCameraImageTexture::BeginDestroy()
{
	Super::BeginDestroy();
}

FTextureResource* UHoloLensCameraImageTexture::CreateResource()
{
#if SUPPORTS_WINDOWS_MIXED_REALITY_AR
	return new FHoloLensCameraImageResource(this);
#else
	return nullptr;
#endif
}

#if SUPPORTS_WINDOWS_MIXED_REALITY_AR
/** Forces the reconstruction of the texture data and conversion from Nv12 to RGB */
void UHoloLensCameraImageTexture::Init(void* handle)
{
	// It's possible that we get more than one queued thread update per game frame
	// Skip any additional frames because it will cause the recursive flush rendering commands ensure
	if (LastUpdateFrame != GFrameCounter)
	{
		LastUpdateFrame = GFrameCounter;
		if (Resource != nullptr)
		{
			FHoloLensCameraImageResource* LambdaResource = static_cast<FHoloLensCameraImageResource*>(Resource);
			ENQUEUE_RENDER_COMMAND(Init_RenderThread)(
				[LambdaResource, handle](FRHICommandListImmediate&)
			{
				LambdaResource->Init_RenderThread(handle);
			});
		}
		else
		{
			// This should end up only being called once, the first time we get a texture update
			UpdateResource();
			if (handle != INVALID_HANDLE_VALUE)
			{
				CloseHandle((HANDLE)handle);
				handle = INVALID_HANDLE_VALUE;
			}
		}
	}
}
#endif
