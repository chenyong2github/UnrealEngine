// Copyright Epic Games, Inc. All Rights Reserved.


#include "CoreMinimal.h"
#include "RenderUtils.h"
#include "OpenGLDrv.h"
#include "OpenGLDrvPrivate.h"
#include "ClearReplacementShaders.h"

FShaderResourceViewRHIRef FOpenGLDynamicRHI::RHICreateShaderResourceView(FRHIVertexBuffer* VertexBufferRHI, uint32 Stride, uint8 Format)
{
	ensureMsgf(Stride == GPixelFormats[Format].BlockBytes, TEXT("provided stride: %i was not consitent with Pixelformat: %s"), Stride, GPixelFormats[Format].Name);
	return FOpenGLDynamicRHI::RHICreateShaderResourceView(FShaderResourceViewInitializer(VertexBufferRHI, EPixelFormat(Format)));
}

// Binds the specified buffer range to a texture resource and selects glTexBuffer or glTexBufferRange 
static void BindGLTexBufferRange(GLenum Target, GLenum InternalFormat, GLuint Buffer, uint32 StartOffsetBytes, uint32 NumElements, uint32 Stride)
{
	if (StartOffsetBytes == 0 && NumElements == UINT32_MAX)
	{
		FOpenGL::TexBuffer(Target, InternalFormat, Buffer);
	}
	else
	{
		// Validate buffer offset is a multiple of buffer offset alignment
		GLintptr Offset = StartOffsetBytes;
		GLsizeiptr Size = NumElements * Stride;

#if DO_CHECK
		GLint Alignment = FOpenGLBase::GetTextureBufferAlignment();
		check(Stride > 0 && Offset % Alignment == 0);
#endif

		FOpenGL::TexBufferRange(Target, InternalFormat, Buffer, Offset, Size);
	}
}

FShaderResourceViewRHIRef FOpenGLDynamicRHI::RHICreateShaderResourceView(const FShaderResourceViewInitializer& Initializer)
{
	switch (Initializer.GetType())
	{
		case FShaderResourceViewInitializer::EType::VertexBufferSRV:
		{
			FShaderResourceViewInitializer::FVertexBufferShaderResourceViewInitializer Desc = Initializer.AsVertexBufferSRV();
			FRHIVertexBuffer* VertexBufferRHI = Desc.VertexBuffer;
			const uint8 Format = Desc.Format;

			FShaderResourceViewRHIRef Result = new FOpenGLShaderResourceViewProxy([=, OGLRHI = this](FRHIShaderResourceView* OwnerRHI)
			{
				VERIFY_GL_SCOPE();
				GLuint TextureID = 0;
				if (FOpenGL::SupportsResourceView())
				{
					FOpenGL::GenTextures(1, &TextureID);
					UE_CLOG(!GPixelFormats[Format].Supported, LogRHI, Error, TEXT("Unsupported EPixelFormat %d"), Format);
					if (VertexBufferRHI)
					{
						FOpenGLVertexBuffer* VertexBuffer = FOpenGLDynamicRHI::ResourceCast(VertexBufferRHI);

						const uint32 FormatBPP = GPixelFormats[Format].BlockBytes;

						const FOpenGLTextureFormat& GLFormat = GOpenGLTextureFormats[Format];

						// Use a texture stage that's not likely to be used for draws, to avoid waiting
						OGLRHI->CachedSetupTextureStage(OGLRHI->GetContextStateForCurrentContext(), FOpenGL::GetMaxCombinedTextureImageUnits() - 1, GL_TEXTURE_BUFFER, TextureID, -1, 1);
						BindGLTexBufferRange(GL_TEXTURE_BUFFER, GLFormat.InternalFormat[0], VertexBuffer->Resource, Desc.StartOffsetBytes, Desc.NumElements, FormatBPP);
					}
				}
				// No need to restore texture stage; leave it like this,
				// and the next draw will take care of cleaning it up; or
				// next operation that needs the stage will switch something else in on it.

				return new FOpenGLShaderResourceView(OGLRHI, TextureID, GL_TEXTURE_BUFFER, VertexBufferRHI, Format);
			});

			return Result;
		}

		case FShaderResourceViewInitializer::EType::StructuredBufferSRV:
		{
			FShaderResourceViewInitializer::FStructuredBufferShaderResourceViewInitializer Desc = Initializer.AsStructuredBufferSRV();
			FRHIStructuredBuffer* StructuredBufferRHI = Desc.StructuredBuffer;

			return new FOpenGLShaderResourceViewProxy([=](FRHIShaderResourceView* OwnerRHI)
			{
				VERIFY_GL_SCOPE();
				GLuint TextureID = 0;
				if (FOpenGL::SupportsResourceView())
				{
					FOpenGLStructuredBuffer* StructuredBuffer = ResourceCast(StructuredBufferRHI);
					FOpenGL::GenTextures(1, &TextureID);
					CachedSetupTextureStage(GetContextStateForCurrentContext(), FOpenGL::GetMaxCombinedTextureImageUnits() - 1, GL_TEXTURE_BUFFER, TextureID, -1, 1);
					uint32 Stride = StructuredBuffer->GetStride();
					GLenum Format = (Stride == 4) ? GL_R32F : GL_RGBA32F;
					BindGLTexBufferRange(GL_TEXTURE_BUFFER, Format, StructuredBuffer->Resource, Desc.StartOffsetBytes, Desc.NumElements, Stride);
				}

				return new FOpenGLShaderResourceView(this, TextureID, GL_TEXTURE_BUFFER);
			});
		}

		case FShaderResourceViewInitializer::EType::IndexBufferSRV:
		{
			FShaderResourceViewInitializer::FIndexBufferShaderResourceViewInitializer Desc = Initializer.AsIndexBufferSRV();
			FRHIIndexBuffer* IndexBufferRHI = Desc.IndexBuffer;

			return new FOpenGLShaderResourceViewProxy([=](FRHIShaderResourceView* OwnerRHI)
			{
				VERIFY_GL_SCOPE();
				GLuint TextureID = 0;
				if (FOpenGL::SupportsResourceView())
				{
					FOpenGL::GenTextures(1, &TextureID);
					if (IndexBufferRHI)
					{
						FOpenGLIndexBuffer* IndexBuffer = ResourceCast(IndexBufferRHI);
						CachedSetupTextureStage(GetContextStateForCurrentContext(), FOpenGL::GetMaxCombinedTextureImageUnits() - 1, GL_TEXTURE_BUFFER, TextureID, -1, 1);
						uint32 Stride = IndexBufferRHI->GetStride();
						GLenum Format = (Stride == 2) ? GL_R16UI : GL_R32UI;
						BindGLTexBufferRange(GL_TEXTURE_BUFFER, Format, IndexBuffer->Resource, Desc.StartOffsetBytes, Desc.NumElements, Stride);
					}
				}
				return new FOpenGLShaderResourceView(this, TextureID, GL_TEXTURE_BUFFER, IndexBufferRHI);
			});
		}

		default:
		{
			checkNoEntry();
			return nullptr;
		}
	}
}

void FOpenGLDynamicRHI::RHIUpdateShaderResourceView(FRHIShaderResourceView* SRV, FRHIIndexBuffer* IndexBuffer)
{
	if (!FOpenGL::SupportsResourceView())
	{
		return;
	}
	VERIFY_GL_SCOPE();

	FOpenGLShaderResourceView* SRVGL = FOpenGLDynamicRHI::ResourceCast(SRV);
	FOpenGLIndexBuffer* IBGL = FOpenGLDynamicRHI::ResourceCast(IndexBuffer);

	check(SRVGL);
	check(!SRVGL->VertexBuffer);
	GLuint TextureID = SRVGL->Resource;
	CachedSetupTextureStage(GetContextStateForCurrentContext(), FOpenGL::GetMaxCombinedTextureImageUnits() - 1, GL_TEXTURE_BUFFER, TextureID, -1, 1);

	if (!IBGL)
	{
		FOpenGL::TexBuffer(GL_TEXTURE_BUFFER, GL_R16UI, 0); // format ignored here since we're detaching.
		SRVGL->IndexBuffer = nullptr;
		SRVGL->ModificationVersion = 0;
	}
	else
	{
		uint32 Stride = IndexBuffer->GetStride();
		GLenum Format = (Stride == 2) ? GL_R16UI : GL_R32UI;
		check(SRVGL->Target == GL_TEXTURE_BUFFER);

		uint32 NumElements = IndexBuffer->GetSize() / Stride;
		BindGLTexBufferRange(GL_TEXTURE_BUFFER, Format, IBGL->Resource, 0, NumElements, Stride);
		SRVGL->IndexBuffer = IndexBuffer;
		SRVGL->ModificationVersion = IBGL->ModificationCount;
	}
}

void FOpenGLDynamicRHI::RHIUpdateShaderResourceView(FRHIShaderResourceView* SRV, FRHIVertexBuffer* VertexBuffer, uint32 Stride, uint8 Format)
{
	if (!FOpenGL::SupportsResourceView())
	{
		return;
	}
	VERIFY_GL_SCOPE();

	FOpenGLShaderResourceView* SRVGL = FOpenGLDynamicRHI::ResourceCast(SRV);
	FOpenGLVertexBuffer* VBGL = FOpenGLDynamicRHI::ResourceCast(VertexBuffer);
	const FOpenGLTextureFormat& GLFormat = GOpenGLTextureFormats[Format];
	
	check(SRVGL);
	check(!SRVGL->IndexBuffer);
	GLuint TextureID = SRVGL->Resource;
	CachedSetupTextureStage(GetContextStateForCurrentContext(), FOpenGL::GetMaxCombinedTextureImageUnits() - 1, GL_TEXTURE_BUFFER, TextureID, -1, 1);
	
	if (!VBGL)
	{
		FOpenGL::TexBuffer(GL_TEXTURE_BUFFER, GLFormat.InternalFormat[0], 0);
		SRVGL->VertexBuffer = nullptr;
		SRVGL->ModificationVersion = 0;
	}
	else
	{
		check(SRVGL->Format == Format && SRVGL->Target == GL_TEXTURE_BUFFER);
		FOpenGL::TexBuffer(GL_TEXTURE_BUFFER, GLFormat.InternalFormat[0], VBGL->Resource);
		SRVGL->VertexBuffer = VertexBuffer;
		SRVGL->ModificationVersion = VBGL->ModificationCount;
	}
}

FShaderResourceViewRHIRef FOpenGLDynamicRHI::RHICreateShaderResourceView(FRHIIndexBuffer* BufferRHI)
{
	return FOpenGLDynamicRHI::RHICreateShaderResourceView(FShaderResourceViewInitializer(BufferRHI));
}

FOpenGLShaderResourceView::~FOpenGLShaderResourceView()
{
	if (Resource && OwnsResource)
	{
		RunOnGLRenderContextThread([OpenGLRHI= OpenGLRHI, Resource = Resource]()
		{
			VERIFY_GL_SCOPE();
			OpenGLRHI->InvalidateTextureResourceInCache(Resource);
			FOpenGL::DeleteTextures(1, &Resource);
		});
	}
}

FUnorderedAccessViewRHIRef FOpenGLDynamicRHI::RHICreateUnorderedAccessView(FRHIStructuredBuffer* StructuredBufferRHI, bool bUseUAVCounter, bool bAppendBuffer)
{
	FOpenGLStructuredBuffer* StructuredBuffer = ResourceCast(StructuredBufferRHI);
	// emulate structured buffer of specific size as typed buffer
	if (GMaxRHIFeatureLevel == ERHIFeatureLevel::ES3_1)
	{
		// ES3.1 cross-compiler converts StructuredBuffer<type4> into Buffer<type4> and StructuredBuffer<type> into Buffer<type>
		// type can be float, int and uint
		check(StructuredBuffer->GetStride() == 16 || StructuredBuffer->GetStride() == 4);
		if (StructuredBuffer->GetStride() == 16)
		{
			return new FOpenGLStructuredBufferUnorderedAccessView(this, StructuredBufferRHI, PF_A32B32G32R32F);
		}
		else if (StructuredBuffer->GetStride() == 4)
		{
			return new FOpenGLStructuredBufferUnorderedAccessView(this, StructuredBufferRHI, PF_R32_FLOAT);
		}
	}
	
	UE_LOG(LogRHI, Fatal,TEXT("%s not implemented yet"),ANSI_TO_TCHAR(__FUNCTION__)); 
	return new FOpenGLUnorderedAccessView();
}

FUnorderedAccessViewRHIRef FOpenGLDynamicRHI::RHICreateUnorderedAccessView(FRHITexture* TextureRHI, uint32 MipLevel)
{
	FOpenGLTexture* Texture = ResourceCast(TextureRHI);
	check(Texture->GetFlags() & TexCreate_UAV);
	return new FOpenGLTextureUnorderedAccessView(TextureRHI);
}


FOpenGLTextureUnorderedAccessView::FOpenGLTextureUnorderedAccessView(FRHITexture* InTextureRHI):
	TextureRHI(InTextureRHI)
{
	VERIFY_GL_SCOPE();
	
	FOpenGLTextureBase* Texture = GetOpenGLTextureFromRHITexture(TextureRHI);
	const FOpenGLTextureFormat& GLFormat = GOpenGLTextureFormats[TextureRHI->GetFormat()];

	check(!Texture->CanBeEvicted() && !Texture->IsEvicted());
	this->Resource = Texture->GetResource();
	this->Format = GLFormat.InternalFormat[0];
	this->UnrealFormat = TextureRHI->GetFormat();
	this->bLayered = (Texture->Target == GL_TEXTURE_3D);
}


FOpenGLVertexBufferUnorderedAccessView::FOpenGLVertexBufferUnorderedAccessView(	FOpenGLDynamicRHI* InOpenGLRHI, FRHIVertexBuffer* InVertexBufferRHI, uint8 Format):
	VertexBufferRHI(InVertexBufferRHI),
	OpenGLRHI(InOpenGLRHI)
{
	VERIFY_GL_SCOPE();
	FOpenGLVertexBuffer* InVertexBuffer = FOpenGLDynamicRHI::ResourceCast(InVertexBufferRHI);


	const FOpenGLTextureFormat& GLFormat = GOpenGLTextureFormats[Format];

	GLuint TextureID = 0;
	FOpenGL::GenTextures(1,&TextureID);

	// Use a texture stage that's not likely to be used for draws, to avoid waiting
	OpenGLRHI->CachedSetupTextureStage(OpenGLRHI->GetContextStateForCurrentContext(), FOpenGL::GetMaxCombinedTextureImageUnits() - 1, GL_TEXTURE_BUFFER, TextureID, -1, 1);
	FOpenGL::TexBuffer(GL_TEXTURE_BUFFER, GLFormat.InternalFormat[0], InVertexBuffer->Resource);

	// No need to restore texture stage; leave it like this,
	// and the next draw will take care of cleaning it up; or
	// next operation that needs the stage will switch something else in on it.

	this->Resource = TextureID;
	this->BufferResource = InVertexBuffer->Resource;
	this->Format = GLFormat.InternalFormat[0];
	this->UnrealFormat = Format;
	
}

uint32 FOpenGLVertexBufferUnorderedAccessView::GetBufferSize()
{
	FOpenGLVertexBuffer* VertexBuffer = FOpenGLDynamicRHI::ResourceCast(VertexBufferRHI.GetReference());
	return VertexBufferRHI->GetSize();
}

FOpenGLVertexBufferUnorderedAccessView::~FOpenGLVertexBufferUnorderedAccessView()
{
	if (Resource)
	{
		RunOnGLRenderContextThread([OpenGLRHI= OpenGLRHI, Resource = Resource]()
		{
			VERIFY_GL_SCOPE();
			OpenGLRHI->InvalidateTextureResourceInCache(Resource);
			FOpenGL::DeleteTextures(1, &Resource);
		});
	}
}


FUnorderedAccessViewRHIRef FOpenGLDynamicRHI::RHICreateUnorderedAccessView(FRHIVertexBuffer* VertexBufferRHI,uint8 Format)
{
	FOpenGLVertexBuffer* VertexBuffer = ResourceCast(VertexBufferRHI);
	return new FOpenGLVertexBufferUnorderedAccessView(this, VertexBufferRHI, Format);
}

FUnorderedAccessViewRHIRef FOpenGLDynamicRHI::RHICreateUnorderedAccessView(FRHIIndexBuffer* IndexBufferRHI, uint8 Format)
{
	checkf(0, TEXT("Not implemented!"));
	return nullptr;
}

FShaderResourceViewRHIRef FOpenGLDynamicRHI::RHICreateShaderResourceView(FRHIStructuredBuffer* StructuredBufferRHI)
{
	return FOpenGLDynamicRHI::RHICreateShaderResourceView(FShaderResourceViewInitializer(StructuredBufferRHI));
}

void FOpenGLDynamicRHI::RHIClearUAVFloat(FRHIUnorderedAccessView* UnorderedAccessViewRHI, const FVector4& Values)
{
	FOpenGLUnorderedAccessView* Texture = ResourceCast(UnorderedAccessViewRHI);

#if OPENGL_GL4 || PLATFORM_LUMINGL4
	if (GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM5)
	{
		glBindBuffer(GL_TEXTURE_BUFFER, Texture->BufferResource);
		FOpenGL::ClearBufferData(GL_TEXTURE_BUFFER, Texture->Format, GL_RGBA_INTEGER, GL_FLOAT, reinterpret_cast<const uint32*>(&Values));
		GPUProfilingData.RegisterGPUWork(1);
		return;
	}
#elif defined(OPENGL_ESDEFERRED)
	glBindBuffer(GL_TEXTURE_BUFFER, Texture->BufferResource);
	uint32 BufferSize = Texture->GetBufferSize();
	if (BufferSize > 0)
	{
		void* BufferData = FOpenGL::MapBufferRange(GL_TEXTURE_BUFFER, 0, BufferSize, FOpenGLBase::RLM_WriteOnly);
		uint8 ClearValue = uint8(Values[0] & 0xff);
		FPlatformMemory::Memset(BufferData, ClearValue, BufferSize);
		FOpenGL::UnmapBufferRange(GL_TEXTURE_BUFFER, 0, BufferSize);
		GPUProfilingData.RegisterGPUWork(1);
	}
#endif
	// Use compute on ES3.1
	TRHICommandList_RecursiveHazardous<FOpenGLDynamicRHI> RHICmdList(this);

	if (Texture->GetBufferSize() == 0)
	{
		FOpenGLTextureUnorderedAccessView* Texture2D = static_cast<FOpenGLTextureUnorderedAccessView*>(Texture);

		FIntVector Size = Texture2D->TextureRHI->GetSizeXYZ();

		if (Texture->IsLayered())
		{
			ClearUAVShader_T<EClearReplacementResourceType::Texture3D, EClearReplacementValueType::Float, 4, false>(RHICmdList, UnorderedAccessViewRHI, Size.X, Size.Y, Size.Z, *reinterpret_cast<const float(*)[4]>(&Values));
		}
		else
		{
			ClearUAVShader_T<EClearReplacementResourceType::Texture2D, EClearReplacementValueType::Float, 4, false>(RHICmdList, UnorderedAccessViewRHI, Size.X, Size.Y, Size.Z, *reinterpret_cast<const float(*)[4]>(&Values));
		}
	}
	else
	{
		check(Texture->BufferResource);
		{
			int32 NumComponents = 0;
			uint32 NumElements = 0;

			if (Texture->UnrealFormat != 0)
			{
				NumComponents = GPixelFormats[Texture->UnrealFormat].NumComponents;
				NumElements = Texture->GetBufferSize() / GPixelFormats[Texture->UnrealFormat].BlockBytes;
			}
			else
			{
				NumElements = Texture->GetBufferSize() / sizeof(float);
				NumComponents = 1;
			}
					
			switch (NumComponents)
			{
			case 1:
				ClearUAVShader_T<EClearReplacementResourceType::Buffer, EClearReplacementValueType::Float, 1, false>(RHICmdList, UnorderedAccessViewRHI, NumElements, 1, 1, *reinterpret_cast<const float(*)[1]>(&Values));
				break;
			case 4:
				ClearUAVShader_T<EClearReplacementResourceType::Buffer, EClearReplacementValueType::Float, 4, false>(RHICmdList, UnorderedAccessViewRHI, NumElements, 1, 1, *reinterpret_cast<const float(*)[4]>(&Values));
				break;
			default:
				check(false);
			};
		}
	}
}

void FOpenGLDynamicRHI::RHIClearUAVUint(FRHIUnorderedAccessView* UnorderedAccessViewRHI, const FUintVector4& Values)
{
	FOpenGLUnorderedAccessView* Texture = ResourceCast(UnorderedAccessViewRHI);
#if OPENGL_GL4 || PLATFORM_LUMINGL4
	if (GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM5)
	{
		glBindBuffer(GL_TEXTURE_BUFFER, Texture->BufferResource);
		FOpenGL::ClearBufferData(GL_TEXTURE_BUFFER, Texture->Format, GL_RGBA_INTEGER, GL_UNSIGNED_INT, reinterpret_cast<const uint32*>(&Values));
		GPUProfilingData.RegisterGPUWork(1);
		return;
	}
#endif
	// Use compute on ES3.1
	TRHICommandList_RecursiveHazardous<FOpenGLDynamicRHI> RHICmdList(this);

	if (Texture->GetBufferSize() == 0)
	{
		FOpenGLTextureUnorderedAccessView* Texture2D = static_cast<FOpenGLTextureUnorderedAccessView*>(Texture);

		FIntVector Size = Texture2D->TextureRHI->GetSizeXYZ();
		
		if (Texture->IsLayered())
		{
			ClearUAVShader_T<EClearReplacementResourceType::Texture3D, EClearReplacementValueType::Uint32, 4, false>(RHICmdList, UnorderedAccessViewRHI, Size.X, Size.Y, Size.Z, *reinterpret_cast<const uint32(*)[4]>(&Values));
		}
		else
		{
			ClearUAVShader_T<EClearReplacementResourceType::Texture2D, EClearReplacementValueType::Uint32, 4, false>(RHICmdList, UnorderedAccessViewRHI, Size.X, Size.Y, Size.Z, *reinterpret_cast<const uint32(*)[4]>(&Values));
		}
	}
	else
	{
		check(Texture->BufferResource);
		{
			int32 NumComponents = 0;
			uint32 NumElements = 0;

			if (Texture->UnrealFormat != 0)
			{
				NumComponents = GPixelFormats[Texture->UnrealFormat].NumComponents;
				NumElements = Texture->GetBufferSize() / GPixelFormats[Texture->UnrealFormat].BlockBytes;
			}
			else
			{
				NumElements = Texture->GetBufferSize() / sizeof(uint32);
				NumComponents = 1;
			}

			switch (NumComponents)
			{
			case 1:
				ClearUAVShader_T<EClearReplacementResourceType::Buffer, EClearReplacementValueType::Uint32, 1, false>(RHICmdList, UnorderedAccessViewRHI, NumElements, 1, 1, *reinterpret_cast<const uint32(*)[1]>(&Values));
				break;
			case 4:
				ClearUAVShader_T<EClearReplacementResourceType::Buffer, EClearReplacementValueType::Uint32, 4, false>(RHICmdList, UnorderedAccessViewRHI, NumElements, 1, 1, *reinterpret_cast<const uint32(*)[4]>(&Values));
				break;
			default:
				check(false);
			};
		}
	}
}

FOpenGLStructuredBufferUnorderedAccessView::FOpenGLStructuredBufferUnorderedAccessView(FOpenGLDynamicRHI* InOpenGLRHI, FRHIStructuredBuffer* InStructuredBufferRHI, uint8 InFormat)
	: StructuredBufferRHI(InStructuredBufferRHI)
	, OpenGLRHI(InOpenGLRHI)
{
	VERIFY_GL_SCOPE();
	FOpenGLStructuredBuffer* InStructuredBuffer = FOpenGLDynamicRHI::ResourceCast(InStructuredBufferRHI);
	const FOpenGLTextureFormat& GLFormat = GOpenGLTextureFormats[InFormat];

	GLuint TextureID = 0;
	FOpenGL::GenTextures(1,&TextureID);

	// Use a texture stage that's not likely to be used for draws, to avoid waiting
	OpenGLRHI->CachedSetupTextureStage(OpenGLRHI->GetContextStateForCurrentContext(), FOpenGL::GetMaxCombinedTextureImageUnits() - 1, GL_TEXTURE_BUFFER, TextureID, -1, 1);
	FOpenGL::TexBuffer(GL_TEXTURE_BUFFER, GLFormat.InternalFormat[0], InStructuredBuffer->Resource);

	// No need to restore texture stage; leave it like this,
	// and the next draw will take care of cleaning it up; or
	// next operation that needs the stage will switch something else in on it.

	this->Resource = TextureID;
	this->BufferResource = InStructuredBuffer->Resource;
	this->Format = GLFormat.InternalFormat[0];
	this->UnrealFormat = InFormat;
}

uint32 FOpenGLStructuredBufferUnorderedAccessView::GetBufferSize()
{
	FOpenGLStructuredBuffer* StructuredBuffer = FOpenGLDynamicRHI::ResourceCast(StructuredBufferRHI.GetReference());
	return StructuredBuffer->GetSize();
}

FOpenGLStructuredBufferUnorderedAccessView::~FOpenGLStructuredBufferUnorderedAccessView()
{
	if (Resource)
	{
		RunOnGLRenderContextThread([OpenGLRHI= OpenGLRHI, Resource = Resource]()
		{
			VERIFY_GL_SCOPE();
			OpenGLRHI->InvalidateTextureResourceInCache(Resource);
			FOpenGL::DeleteTextures(1, &Resource);
		});
	}
}