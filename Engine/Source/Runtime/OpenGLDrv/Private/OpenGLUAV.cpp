// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "CoreMinimal.h"
#include "RenderUtils.h"
#include "OpenGLDrv.h"
#include "OpenGLDrvPrivate.h"

FShaderResourceViewRHIRef FOpenGLDynamicRHI::RHICreateShaderResourceView(FRHIVertexBuffer* VertexBufferRHI, uint32 Stride, uint8 Format)
{
	FShaderResourceViewRHIRef Result = new FOpenGLShaderResourceViewProxy([=, OGLRHI = this](FRHIShaderResourceView* OwnerRHI)
	{ 
		VERIFY_GL_SCOPE();
		GLuint TextureID = 0;
		if (FOpenGL::SupportsResourceView())
		{
			UE_CLOG(!GPixelFormats[Format].Supported, LogRHI, Error, TEXT("Unsupported EPixelFormat %d"), Format);

			FOpenGLVertexBuffer* VertexBuffer = FOpenGLDynamicRHI::ResourceCast(VertexBufferRHI);

			const uint32 FormatBPP = GPixelFormats[Format].BlockBytes;

			const FOpenGLTextureFormat& GLFormat = GOpenGLTextureFormats[Format];
			FOpenGL::GenTextures(1, &TextureID);
			
			// Use a texture stage that's not likely to be used for draws, to avoid waiting
			if (VertexBuffer)
			{
				OGLRHI->CachedSetupTextureStage(OGLRHI->GetContextStateForCurrentContext(), FOpenGL::GetMaxCombinedTextureImageUnits() - 1, GL_TEXTURE_BUFFER, TextureID, -1, 1);
				FOpenGL::TexBuffer(GL_TEXTURE_BUFFER, GLFormat.InternalFormat[0], VertexBuffer->Resource);
			}
		}

		// No need to restore texture stage; leave it like this,
		// and the next draw will take care of cleaning it up; or
		// next operation that needs the stage will switch something else in on it.

		return new FOpenGLShaderResourceView(OGLRHI, TextureID, GL_TEXTURE_BUFFER, VertexBufferRHI, Format);
	});

	return Result;
}

void FOpenGLDynamicRHI::RHIUpdateShaderResourceView(FRHIShaderResourceView* SRV, FRHIVertexBuffer* VertexBuffer, uint32 Stride, uint8 Format)
{
	if (!FOpenGL::SupportsResourceView())
	{
		return;
	}
	
	FOpenGLShaderResourceView* SRVGL = FOpenGLDynamicRHI::ResourceCast(SRV);
	FOpenGLVertexBuffer* VBGL = FOpenGLDynamicRHI::ResourceCast(VertexBuffer);
	const FOpenGLTextureFormat& GLFormat = GOpenGLTextureFormats[Format];
	
	check(SRVGL);
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
	return new FOpenGLShaderResourceViewProxy([=](FRHIShaderResourceView* OwnerRHI)
	{
		VERIFY_GL_SCOPE();
		GLuint TextureID = 0;
		if (FOpenGL::SupportsResourceView())
		{
			FOpenGLIndexBuffer* IndexBuffer = ResourceCast(BufferRHI);
			FOpenGL::GenTextures(1, &TextureID);
			CachedSetupTextureStage(GetContextStateForCurrentContext(), FOpenGL::GetMaxCombinedTextureImageUnits() - 1, GL_TEXTURE_BUFFER, TextureID, -1, 1);
			uint32 Stride = BufferRHI->GetStride();
			GLenum Format = (Stride == 2) ? GL_R16UI : GL_R32UI;
			FOpenGL::TexBuffer(GL_TEXTURE_BUFFER, Format, IndexBuffer->Resource);
		}

		return new FOpenGLShaderResourceView(this, TextureID, GL_TEXTURE_BUFFER);
	});
}

FOpenGLShaderResourceView::~FOpenGLShaderResourceView()
{
	if (Resource && OwnsResource)
	{
		VERIFY_GL_SCOPE();
		OpenGLRHI->InvalidateTextureResourceInCache(Resource);
		FOpenGL::DeleteTextures(1, &Resource);
	}
}

FUnorderedAccessViewRHIRef FOpenGLDynamicRHI::RHICreateUnorderedAccessView(FRHIStructuredBuffer* StructuredBufferRHI, bool bUseUAVCounter, bool bAppendBuffer)
{
	FOpenGLStructuredBuffer* StructuredBuffer = ResourceCast(StructuredBufferRHI);
	// emulate structured buffer of specific size as typed buffer
	if (GMaxRHIFeatureLevel == ERHIFeatureLevel::ES3_1)
	{
		// ES3.1 cross-compiler converts StructuredBuffer<float4> into Buffer<float4> 
		// only float4 atm
		check(StructuredBuffer->GetStride() == 16);
		if (StructuredBuffer->GetStride() == 16)
		{
			return new FOpenGLStructuredBufferUnorderedAccessView(this, StructuredBufferRHI, PF_A32B32G32R32F);
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

	this->Resource = Texture->Resource;
	this->Format = GLFormat.InternalFormat[0];
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
		OpenGLRHI->InvalidateTextureResourceInCache( Resource );
		FOpenGL::DeleteTextures(1, &Resource);
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
	return new FOpenGLShaderResourceViewProxy([=](FRHIShaderResourceView* OwnerRHI)
	{
		VERIFY_GL_SCOPE();
		GLuint TextureID = 0;
		if (FOpenGL::SupportsResourceView())
		{
			FOpenGLStructuredBuffer* StructuredBuffer = ResourceCast(StructuredBufferRHI);
			FOpenGL::GenTextures(1, &TextureID);
			CachedSetupTextureStage(GetContextStateForCurrentContext(), FOpenGL::GetMaxCombinedTextureImageUnits() - 1, GL_TEXTURE_BUFFER, TextureID, -1, 1);
			FOpenGL::TexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, StructuredBuffer->Resource);
		}

		return new FOpenGLShaderResourceView(this, TextureID, GL_TEXTURE_BUFFER);
	});
}

void FOpenGLDynamicRHI::RHIClearUAVFloat(FRHIUnorderedAccessView* UnorderedAccessViewRHI, const FVector4& Values)
{
	FOpenGLUnorderedAccessView* Texture = ResourceCast(UnorderedAccessViewRHI);

#if OPENGL_GL4 || PLATFORM_LUMINGL4
	glBindBuffer(GL_TEXTURE_BUFFER, Texture->BufferResource);
	FOpenGL::ClearBufferData(GL_TEXTURE_BUFFER, Texture->Format, GL_RGBA_INTEGER, GL_FLOAT, reinterpret_cast<const uint32*>(&Values));
	GPUProfilingData.RegisterGPUWork(1);

#elif OPENGL_ESDEFERRED
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
#else
	UE_LOG(LogRHI, Fatal, TEXT("Only OpenGL4 supports RHIClearUAVFloat."));
#endif
}

void FOpenGLDynamicRHI::RHIClearUAVUint(FRHIUnorderedAccessView* UnorderedAccessViewRHI, const FUintVector4& Values)
{
	FOpenGLUnorderedAccessView* Texture = ResourceCast(UnorderedAccessViewRHI);

#if OPENGL_GL4 || PLATFORM_LUMINGL4
	glBindBuffer(GL_TEXTURE_BUFFER, Texture->BufferResource);
	FOpenGL::ClearBufferData(GL_TEXTURE_BUFFER, Texture->Format, GL_RGBA_INTEGER, GL_UNSIGNED_INT, reinterpret_cast<const uint32*>(&Values));
	GPUProfilingData.RegisterGPUWork(1);

#elif OPENGL_ESDEFERRED
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
#else
	UE_LOG(LogRHI, Fatal, TEXT("Only OpenGL4 supports RHIClearUAVUint."));
#endif
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
		OpenGLRHI->InvalidateTextureResourceInCache( Resource );
		FOpenGL::DeleteTextures(1, &Resource);
	}
}