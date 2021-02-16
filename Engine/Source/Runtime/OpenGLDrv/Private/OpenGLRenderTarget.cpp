// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	OpenGLRenderTarget.cpp: OpenGL render target implementation.
=============================================================================*/

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "RHI.h"
#include "OpenGLDrv.h"
#include "OpenGLDrvPrivate.h"

// gDEBugger is currently very buggy. For example, it cannot show render buffers correctly and doesn't
// know what combined depth/stencil is. This define makes OpenGL render directly to textures and disables
// stencil. It results in broken post process effects, but allows to debug the rendering in gDEBugger.
//#define GDEBUGGER_MODE

#define ALL_SLICES uint32(0xffffffff)

// GL_MAX_DRAW_BUFFERS value
GLint GMaxOpenGLDrawBuffers = 0;

/**
* Key used to map a set of unique render/depth stencil target combinations to
* a framebuffer resource
*/
class FOpenGLFramebufferKey
{
	struct RenderTargetInfo
	{
		FOpenGLTextureBase* Texture;
		GLuint				Resource;
		uint32				MipmapLevel;
		uint32				ArrayIndex;
	};

public:

	FOpenGLFramebufferKey(
		uint32 InNumRenderTargets,
		FOpenGLTextureBase** InRenderTargets,
		const uint32* InRenderTargetArrayIndices,
		const uint32* InRenderTargetMipmapLevels,
		FOpenGLTextureBase* InDepthStencilTarget,
		EOpenGLCurrentContext InContext
		)
		:	DepthStencilTarget(InDepthStencilTarget)
		,	Context(InContext)
	{
		uint32 RenderTargetIndex;
		for( RenderTargetIndex = 0; RenderTargetIndex < InNumRenderTargets; ++RenderTargetIndex )
		{
			FMemory::Memzero(RenderTargets[RenderTargetIndex]); // since memcmp is used, we need to zero memory
			RenderTargets[RenderTargetIndex].Texture = InRenderTargets[RenderTargetIndex];
			RenderTargets[RenderTargetIndex].Resource = (InRenderTargets[RenderTargetIndex]) ? InRenderTargets[RenderTargetIndex]->GetResource() : 0;
			RenderTargets[RenderTargetIndex].MipmapLevel = InRenderTargetMipmapLevels[RenderTargetIndex];
			RenderTargets[RenderTargetIndex].ArrayIndex = (InRenderTargetArrayIndices == NULL || InRenderTargetArrayIndices[RenderTargetIndex] == -1) ? ALL_SLICES : InRenderTargetArrayIndices[RenderTargetIndex];
		}
		for( ; RenderTargetIndex < MaxSimultaneousRenderTargets; ++RenderTargetIndex )
		{
			FMemory::Memzero(RenderTargets[RenderTargetIndex]); // since memcmp is used, we need to zero memory
			RenderTargets[RenderTargetIndex].ArrayIndex = ALL_SLICES;
		}
	}

	/**
	* Equality is based on render and depth stencil targets 
	* @param Other - instance to compare against
	* @return true if equal
	*/
	friend bool operator ==(const FOpenGLFramebufferKey& A,const FOpenGLFramebufferKey& B)
	{
		return	!FMemory::Memcmp(A.RenderTargets, B.RenderTargets, sizeof(A.RenderTargets) ) && A.DepthStencilTarget == B.DepthStencilTarget && A.Context == B.Context;
	}

	/**
	* Get the hash for this type. 
	* @param Key - struct to hash
	* @return uint32 hash based on type
	*/
	friend uint32 GetTypeHash(const FOpenGLFramebufferKey &Key)
	{
		return FCrc::MemCrc_DEPRECATED(Key.RenderTargets, sizeof(Key.RenderTargets)) ^ GetTypeHash(Key.DepthStencilTarget) ^ GetTypeHash(Key.Context);
	}

	const FOpenGLTextureBase* GetRenderTarget( int32 Index ) const { return RenderTargets[Index].Texture; }
	const FOpenGLTextureBase* GetDepthStencilTarget( void ) const { return DepthStencilTarget; }

private:

	RenderTargetInfo RenderTargets[MaxSimultaneousRenderTargets];
	FOpenGLTextureBase* DepthStencilTarget;
	EOpenGLCurrentContext Context;
};

typedef TMap<FOpenGLFramebufferKey,GLuint> FOpenGLFramebufferCache;

/** Lazily initialized framebuffer cache singleton. */
static FOpenGLFramebufferCache& GetOpenGLFramebufferCache()
{
	static FOpenGLFramebufferCache OpenGLFramebufferCache;
	return OpenGLFramebufferCache;
}

GLuint FOpenGLDynamicRHI::GetOpenGLFramebuffer(uint32 NumSimultaneousRenderTargets, FOpenGLTextureBase** RenderTargets, const uint32* ArrayIndices, const uint32* MipmapLevels, FOpenGLTextureBase* DepthStencilTarget)
{
	VERIFY_GL_SCOPE();

	check( NumSimultaneousRenderTargets <= MaxSimultaneousRenderTargets );

	uint32 FramebufferRet = GetOpenGLFramebufferCache().FindRef(FOpenGLFramebufferKey(NumSimultaneousRenderTargets, RenderTargets, ArrayIndices, MipmapLevels, DepthStencilTarget, PlatformOpenGLCurrentContext(PlatformDevice)));
	if( FramebufferRet > 0 )
	{
		// Found and is valid. We never store zero as a result, increasing all results by 1 to avoid range overlap.
		return FramebufferRet-1;
	}

	// Check for rendering to screen back buffer.
	if(0 < NumSimultaneousRenderTargets && RenderTargets[0] && RenderTargets[0]->GetResource() == GL_NONE)
	{
		// Use the default framebuffer (screen back/depth buffer)
		return GL_NONE;
	}

	// Not found. Preparing new one.
	GLuint Framebuffer;
	glGenFramebuffers(1, &Framebuffer);
	VERIFY_GL(glGenFramebuffer)
	glBindFramebuffer(GL_FRAMEBUFFER, Framebuffer);
	VERIFY_GL(glBindFramebuffer)

#if PLATFORM_ANDROID && !PLATFORM_LUMINGL4
	static const auto CVarMobileMultiView = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("vr.MobileMultiView"));

	// Allocate mobile multi-view frame buffer if enabled and supported.
	// Multi-view doesn't support read buffers, explicitly disable and only bind GL_DRAW_FRAMEBUFFER
	// TODO: We can't reliably use packed depth stencil?
	const bool bRenderTargetsDefined = (RenderTargets != nullptr) && RenderTargets[0];
	const bool bValidMultiViewDepthTarget = !DepthStencilTarget || DepthStencilTarget->Target == GL_TEXTURE_2D_ARRAY;
	const bool bUsingArrayTextures = (bRenderTargetsDefined) ? (RenderTargets[0]->Target == GL_TEXTURE_2D_ARRAY && bValidMultiViewDepthTarget) : false;
	const bool bMultiViewCVar = CVarMobileMultiView && CVarMobileMultiView->GetValueOnAnyThread() != 0;

	if (bUsingArrayTextures && FOpenGL::SupportsMobileMultiView() && bMultiViewCVar)
	{
		FOpenGLTextureBase* const RenderTarget = RenderTargets[0];
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, Framebuffer);

		FOpenGLTexture2D* RenderTarget2D = (FOpenGLTexture2D*)RenderTarget;
		const uint32 NumSamplesTileMem = RenderTarget2D->GetNumSamplesTileMem();
		if (NumSamplesTileMem > 1)
		{
			glFramebufferTextureMultisampleMultiviewOVR(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, RenderTarget->GetResource(), 0, NumSamplesTileMem, 0, 2);
			VERIFY_GL(glFramebufferTextureMultisampleMultiviewOVR);

			if (DepthStencilTarget)
			{
				glFramebufferTextureMultisampleMultiviewOVR(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, DepthStencilTarget->GetResource(), 0, NumSamplesTileMem, 0, 2);
				VERIFY_GL(glFramebufferTextureMultisampleMultiviewOVR);
			}
		}
		else
		{
			glFramebufferTextureMultiviewOVR(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, RenderTarget->GetResource(), 0, 0, 2);
			VERIFY_GL(glFramebufferTextureMultiviewOVR);

			if (DepthStencilTarget)
			{
				glFramebufferTextureMultiviewOVR(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, DepthStencilTarget->GetResource(), 0, 0, 2);
				VERIFY_GL(glFramebufferTextureMultiviewOVR);
			}
		}

		FOpenGL::CheckFrameBuffer();

		FOpenGL::ReadBuffer(GL_NONE);
		FOpenGL::DrawBuffer(GL_COLOR_ATTACHMENT0);

		GetOpenGLFramebufferCache().Add(FOpenGLFramebufferKey(NumSimultaneousRenderTargets, RenderTargets, ArrayIndices, MipmapLevels, DepthStencilTarget, PlatformOpenGLCurrentContext(PlatformDevice)), Framebuffer + 1);
		
		return Framebuffer;
	}
#endif

	int32 FirstNonzeroRenderTarget = -1;
	for( int32 RenderTargetIndex = NumSimultaneousRenderTargets - 1; RenderTargetIndex >= 0 ; --RenderTargetIndex )
	{
		FOpenGLTextureBase* RenderTarget = RenderTargets[RenderTargetIndex];
		if (!RenderTarget)
		{
			continue;
		}

		if (ArrayIndices == NULL || ArrayIndices[RenderTargetIndex] == -1)
		{
			// If no index was specified, bind the entire object, rather than a slice
			switch (RenderTarget->Target)
			{
			case GL_TEXTURE_2D:
#if PLATFORM_ANDROID && !PLATFORM_LUMINGL4
			case GL_TEXTURE_EXTERNAL_OES:
#endif
			case GL_TEXTURE_2D_MULTISAMPLE:
			{
#if PLATFORM_ANDROID && !PLATFORM_LUMINGL4
				FOpenGLTexture2D* RenderTarget2D = (FOpenGLTexture2D*)RenderTarget;
				const uint32 NumSamplesTileMem = RenderTarget2D->GetNumSamplesTileMem();
				if (NumSamplesTileMem > 1 && glFramebufferTexture2DMultisampleEXT)
				{
					// GL_EXT_multisampled_render_to_texture requires GL_COLOR_ATTACHMENT0
					check(RenderTargetIndex == 0);
					glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + RenderTargetIndex, RenderTarget->Target, RenderTarget->GetResource(), MipmapLevels[RenderTargetIndex], NumSamplesTileMem);
					VERIFY_GL(glFramebufferTexture2DMultisampleEXT);
				}
				else
#endif
				{
					FOpenGL::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + RenderTargetIndex, RenderTarget->Target, RenderTarget->GetResource(), MipmapLevels[RenderTargetIndex]);
				}
				break;
			}
			case GL_TEXTURE_3D:
			case GL_TEXTURE_2D_ARRAY:
			case GL_TEXTURE_CUBE_MAP:
			case GL_TEXTURE_CUBE_MAP_ARRAY:
				FOpenGL::FramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + RenderTargetIndex, RenderTarget->GetResource(), MipmapLevels[RenderTargetIndex]);
				break;
			default:
				FOpenGL::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + RenderTargetIndex, GL_RENDERBUFFER, RenderTarget->GetResource());
				break;
			}
		}
		else
		{
			// Bind just one slice of the object
			switch( RenderTarget->Target )
			{
			case GL_TEXTURE_2D:
#if PLATFORM_ANDROID && !PLATFORM_LUMINGL4
			case GL_TEXTURE_EXTERNAL_OES:
#endif
			case GL_TEXTURE_2D_MULTISAMPLE:
			{
				check(ArrayIndices[RenderTargetIndex] == 0);
#if PLATFORM_ANDROID && !PLATFORM_LUMINGL4
				FOpenGLTexture2D* RenderTarget2D = (FOpenGLTexture2D*)RenderTarget;
				const uint32 NumSamplesTileMem = RenderTarget2D->GetNumSamplesTileMem();
				if (NumSamplesTileMem > 1 && glFramebufferTexture2DMultisampleEXT)
				{
					// GL_EXT_multisampled_render_to_texture requires GL_COLOR_ATTACHMENT0
					check(RenderTargetIndex == 0);
					glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + RenderTargetIndex, RenderTarget->Target, RenderTarget->GetResource(), MipmapLevels[RenderTargetIndex], NumSamplesTileMem);
					VERIFY_GL(glFramebufferTexture2DMultisampleEXT);
				}
				else
#endif
				{
					FOpenGL::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + RenderTargetIndex, RenderTarget->Target, RenderTarget->GetResource(), MipmapLevels[RenderTargetIndex]);
				}
				break;
			}
			case GL_TEXTURE_3D:
				FOpenGL::FramebufferTexture3D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0 + RenderTargetIndex, RenderTarget->Target, RenderTarget->GetResource(), MipmapLevels[RenderTargetIndex], ArrayIndices[RenderTargetIndex]);
				break;
			case GL_TEXTURE_CUBE_MAP:
				check( ArrayIndices[RenderTargetIndex] < 6);
				FOpenGL::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + RenderTargetIndex, GL_TEXTURE_CUBE_MAP_POSITIVE_X + ArrayIndices[RenderTargetIndex], RenderTarget->GetResource(), MipmapLevels[RenderTargetIndex]);
				break;
			case GL_TEXTURE_2D_ARRAY:
			case GL_TEXTURE_CUBE_MAP_ARRAY:
				FOpenGL::FramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + RenderTargetIndex, RenderTarget->GetResource(), MipmapLevels[RenderTargetIndex], ArrayIndices[RenderTargetIndex]);
				break;
			default:
				check( ArrayIndices[RenderTargetIndex] == 0);
				FOpenGL::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + RenderTargetIndex, GL_RENDERBUFFER, RenderTarget->GetResource());
				break;
			}
		}
		FirstNonzeroRenderTarget = RenderTargetIndex;
	}

	if (DepthStencilTarget)
	{
		switch (DepthStencilTarget->Target)
		{
		case GL_TEXTURE_2D:
#if PLATFORM_ANDROID && !PLATFORM_LUMINGL4
		case GL_TEXTURE_EXTERNAL_OES:
#endif
		case GL_TEXTURE_2D_MULTISAMPLE:
		{
				FOpenGL::FramebufferTexture2D(GL_FRAMEBUFFER, DepthStencilTarget->Attachment, DepthStencilTarget->Target, DepthStencilTarget->GetResource(), 0);
			break;
		}
		case GL_RENDERBUFFER:
		{
			FOpenGLTexture2D* DepthStencilTarget2D = (FOpenGLTexture2D*)DepthStencilTarget;
			const uint32 NumSamplesTileMem = DepthStencilTarget2D->GetNumSamplesTileMem();
			if (NumSamplesTileMem > 1)
			{	
				glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, DepthStencilTarget->GetResource());
					glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, DepthStencilTarget->GetResource());
				VERIFY_GL(glFramebufferRenderbuffer);
			}
			break;
		}
		case GL_TEXTURE_3D:
		case GL_TEXTURE_2D_ARRAY:
		case GL_TEXTURE_CUBE_MAP:
		case GL_TEXTURE_CUBE_MAP_ARRAY:
			FOpenGL::FramebufferTexture(GL_FRAMEBUFFER, DepthStencilTarget->Attachment, DepthStencilTarget->GetResource(), 0);
			break;
		default:
				FOpenGL::FramebufferRenderbuffer(GL_FRAMEBUFFER, DepthStencilTarget->Attachment, GL_RENDERBUFFER, DepthStencilTarget->GetResource());
			break;
		}
	}

	if (FirstNonzeroRenderTarget != -1)
	{
		FOpenGL::ReadBuffer(GL_COLOR_ATTACHMENT0 + FirstNonzeroRenderTarget);
		FOpenGL::DrawBuffer(GL_COLOR_ATTACHMENT0 + FirstNonzeroRenderTarget);
	}
	else
	{
		FOpenGL::ReadBuffer(GL_NONE);
		FOpenGL::DrawBuffer(GL_NONE);
	}

	//  End frame can bind NULL / NULL 
	//  An FBO with no attachments is framebuffer incomplete (INCOMPLETE_MISSING_ATTACHMENT)
	//  In this case just delete the FBO and map in the default
	//  In GL 4.x, NULL/NULL is valid and can be done =by specifying a default width/height
	if ( FirstNonzeroRenderTarget == -1 && !DepthStencilTarget )
	{
		glDeleteFramebuffers( 1, &Framebuffer);
		Framebuffer = 0;
		glBindFramebuffer(GL_FRAMEBUFFER, Framebuffer);
	}
	
	FOpenGL::CheckFrameBuffer();

	GetOpenGLFramebufferCache().Add(FOpenGLFramebufferKey(NumSimultaneousRenderTargets, RenderTargets, ArrayIndices, MipmapLevels, DepthStencilTarget, PlatformOpenGLCurrentContext(PlatformDevice)), Framebuffer+1);

	return Framebuffer;
}

void ReleaseOpenGLFramebuffers(FOpenGLDynamicRHI* Device, FRHITexture* TextureRHI)
{
	const FOpenGLTextureBase* Texture = GetOpenGLTextureFromRHITexture(TextureRHI);

	if (Texture)
	{
		for (FOpenGLFramebufferCache::TIterator It(GetOpenGLFramebufferCache()); It; ++It)
		{
			bool bPurgeFramebuffer = false;
			FOpenGLFramebufferKey Key = It.Key();

			const FOpenGLTextureBase* DepthStencilTarget = Key.GetDepthStencilTarget();
			if( DepthStencilTarget && DepthStencilTarget->Target == Texture->Target && DepthStencilTarget->GetRawResourceName() == Texture->GetRawResourceName() )
			{
				bPurgeFramebuffer = true;
			}
			else
			{
				for( uint32 RenderTargetIndex = 0; RenderTargetIndex < MaxSimultaneousRenderTargets; ++RenderTargetIndex )
				{
					const FOpenGLTextureBase* RenderTarget = Key.GetRenderTarget(RenderTargetIndex);
					if( RenderTarget && RenderTarget->Target == Texture->Target && RenderTarget->GetRawResourceName() == Texture->GetRawResourceName() )
					{
						bPurgeFramebuffer = true;
						break;
					}
				}
			}

			if( bPurgeFramebuffer )
			{
				GLuint FramebufferToDelete = It.Value()-1;
				check(FramebufferToDelete > 0);

				RunOnGLRenderContextThread( [=]() 
					{
						VERIFY_GL_SCOPE();
						Device->PurgeFramebufferFromCaches( FramebufferToDelete );
						glDeleteFramebuffers( 1, &FramebufferToDelete );
					});

				It.RemoveCurrent();
			}
		}
	}
}

void FOpenGLDynamicRHI::PurgeFramebufferFromCaches( GLuint Framebuffer )
{
	VERIFY_GL_SCOPE();

	if( Framebuffer == PendingState.Framebuffer )
	{
		PendingState.Framebuffer = 0;
		FMemory::Memset(PendingState.RenderTargets, 0, sizeof(PendingState.RenderTargets));
		FMemory::Memset(PendingState.RenderTargetMipmapLevels, 0, sizeof(PendingState.RenderTargetMipmapLevels));
		FMemory::Memset(PendingState.RenderTargetArrayIndex, 0, sizeof(PendingState.RenderTargetArrayIndex));
		PendingState.DepthStencil = 0;
		PendingState.bFramebufferSetupInvalid = true;
	}

	if( Framebuffer == SharedContextState.Framebuffer )
	{
		SharedContextState.Framebuffer = -1;
	}

	if( Framebuffer == RenderingContextState.Framebuffer )
	{
		RenderingContextState.Framebuffer = -1;
	}
}

void FOpenGLDynamicRHI::RHICopyToResolveTarget(FRHITexture* SourceTextureRHI, FRHITexture* DestTextureRHI, const FResolveParams& ResolveParams)
{
	if (!SourceTextureRHI || !DestTextureRHI)
	{
		// no need to do anything (silently ignored)
		return;
	}

	FOpenGLTextureBase* SourceTexture = GetOpenGLTextureFromRHITexture(SourceTextureRHI);
	FOpenGLTextureBase* DestTexture = GetOpenGLTextureFromRHITexture(DestTextureRHI);

	if (SourceTexture != DestTexture)
	{
		VERIFY_GL_SCOPE();

		check(GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM5 || ResolveParams.SourceArrayIndex == 0);
		check(GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM5 || ResolveParams.DestArrayIndex == 0);

		const bool bSrcCubemap  = SourceTextureRHI->GetTextureCube() != NULL;
		const bool bDestCubemap = DestTextureRHI->GetTextureCube() != NULL;

		uint32 DestIndex = ResolveParams.DestArrayIndex * (bDestCubemap ? 6 : 1) + (bDestCubemap ? uint32(ResolveParams.CubeFace) : 0);
		uint32 SrcIndex  = ResolveParams.SourceArrayIndex * (bSrcCubemap ? 6 : 1) + (bSrcCubemap ? uint32(ResolveParams.CubeFace) : 0);

		FIntRect SrcRect(ResolveParams.Rect.X1, ResolveParams.Rect.Y1, ResolveParams.Rect.X2, ResolveParams.Rect.Y2);
		if (SrcRect.IsEmpty())
		{
			// Empty rect mans that the entire source is to be copied. Note that we can't use ResolveParams.Rect.IsValid(), because it
			// returns false if the rectangle is "inside out" (e.g. X1 > X2), and we want to perform flipping when that's the case.
			SrcRect.Min.X = 0;
			SrcRect.Min.Y = 0;
			SrcRect.Max.X = GetOpenGLTextureSizeXFromRHITexture(SourceTextureRHI);
			SrcRect.Max.Y = GetOpenGLTextureSizeYFromRHITexture(SourceTextureRHI);
			SrcRect.Max.X = FMath::Max<int32>(1, SrcRect.Max.X >> ResolveParams.MipIndex);
			SrcRect.Max.Y = FMath::Max<int32>(1, SrcRect.Max.Y >> ResolveParams.MipIndex);
		}

		FIntRect DestRect(ResolveParams.DestRect.X1, ResolveParams.DestRect.Y1, ResolveParams.DestRect.X2, ResolveParams.DestRect.Y2);
		if(DestRect.IsEmpty())
		{
			DestRect.Min.X = 0;
			DestRect.Min.Y = 0;
			DestRect.Max.X = GetOpenGLTextureSizeXFromRHITexture(DestTextureRHI);
			DestRect.Max.Y = GetOpenGLTextureSizeYFromRHITexture(DestTextureRHI);
			DestRect.Max.X = FMath::Max<int32>(1, DestRect.Max.X >> ResolveParams.MipIndex);
			DestRect.Max.Y = FMath::Max<int32>(1, DestRect.Max.Y >> ResolveParams.MipIndex);
		}

		GPUProfilingData.RegisterGPUWork();
		const uint32 MipmapLevel = ResolveParams.MipIndex;

		const bool bTrueBlit = !SourceTextureRHI->IsMultisampled()
			&& !DestTextureRHI->IsMultisampled()
			&& SourceTextureRHI->GetFormat() == DestTextureRHI->GetFormat()
			&& SrcRect.Size() == DestRect.Size()
			&& SrcRect.Width() > 0
			&& SrcRect.Height() > 0
#if PLATFORM_ANDROID
			&& SourceTexture->Target == DestTexture->Target // glCopyImageSubData() doesn't like copying from a texture to a renderbuffer on Android
#endif
			;
		
		if ( !bTrueBlit || !FOpenGL::SupportsCopyImage() )
		{
			// Color buffers can be GL_NONE for attachment purposes if they aren't used as render targets
			const bool bIsColorBuffer = SourceTexture->Attachment != GL_DEPTH_STENCIL_ATTACHMENT
																	&& SourceTexture->Attachment != GL_DEPTH_ATTACHMENT;
			check( bIsColorBuffer || (SrcIndex == 0 && DestIndex == 0));
			check( bIsColorBuffer || MipmapLevel == 0);
			GLuint SrcFramebuffer = bIsColorBuffer ?
				GetOpenGLFramebuffer(1, &SourceTexture, &SrcIndex, &MipmapLevel, NULL) :
				GetOpenGLFramebuffer(0, NULL, NULL, NULL, SourceTexture);
			GLuint DestFramebuffer = bIsColorBuffer ?
				GetOpenGLFramebuffer(1, &DestTexture, &DestIndex, &MipmapLevel, NULL) :
				GetOpenGLFramebuffer(0, NULL, NULL, NULL, DestTexture);

			glBindFramebuffer(UGL_DRAW_FRAMEBUFFER, DestFramebuffer);
			FOpenGL::DrawBuffer(bIsColorBuffer ? GL_COLOR_ATTACHMENT0 : GL_NONE);
			glBindFramebuffer(UGL_READ_FRAMEBUFFER, SrcFramebuffer);
			FOpenGL::ReadBuffer(bIsColorBuffer ? GL_COLOR_ATTACHMENT0 : GL_NONE);

			// ToDo - Scissor and possibly color mask can impact blits
			//  These should be disabled

			GLbitfield Mask = GL_NONE;
			if (bIsColorBuffer)
			{
				Mask = GL_COLOR_BUFFER_BIT;
			}
			else if (SourceTexture->Attachment == GL_DEPTH_ATTACHMENT)
			{
				Mask = GL_DEPTH_BUFFER_BIT;
			}
			else
			{
				Mask = GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
			}

			FOpenGL::BlitFramebuffer(
				SrcRect.Min.X, SrcRect.Min.Y, SrcRect.Max.X, SrcRect.Max.Y,
				DestRect.Min.X, DestRect.Min.Y, DestRect.Max.X, DestRect.Max.Y,
				Mask,
				GL_NEAREST
				);
		}
		else
		{
			// CopyImageSubData seems like a better analog to what UE wants in most cases
			// It has no interactions with any other state, and there is no filtering/conversion
			// It does not support MSAA resolves though
			FOpenGL::CopyImageSubData(	SourceTexture->GetResource(),
										SourceTexture->Target,
										MipmapLevel,
										SrcRect.Min.X,
										SrcRect.Min.Y,
										SrcIndex,
										DestTexture->GetResource(),
										DestTexture->Target,
										MipmapLevel,
										DestRect.Min.X,
										DestRect.Min.Y,
										DestIndex,
										SrcRect.Width(),
										SrcRect.Height(),
										1);
		}

	
		// For CPU readback resolve targets we should issue the resolve to the internal PBO immediately.
		// This makes any subsequent locking of that texture much cheaper as it won't have to stall on a pixel pack op.
		bool bLockableTarget = DestTextureRHI->GetTexture2D() && (DestTextureRHI->GetFlags() & TexCreate_CPUReadback) && !(DestTextureRHI->GetFlags() & (TexCreate_RenderTargetable|TexCreate_DepthStencilTargetable)) && !DestTextureRHI->IsMultisampled();
		if(bLockableTarget && !ResolveParams.Rect.IsValid())
		{
			FOpenGLTexture2D* DestTex = (FOpenGLTexture2D*)DestTexture;
			DestTex->Resolve(MipmapLevel, DestIndex);
		}

		GetContextStateForCurrentContext().Framebuffer = (GLuint)-1;
	}
	else
	{
		// no need to do anything (silently ignored)
	}
}



void FOpenGLDynamicRHI::ReadSurfaceDataRaw(FOpenGLContextState& ContextState, FRHITexture* TextureRHI,FIntRect Rect,TArray<uint8>& OutData, FReadSurfaceDataFlags InFlags)
{
	VERIFY_GL_SCOPE();

	FOpenGLTexture2D* Texture2D = (FOpenGLTexture2D*)TextureRHI->GetTexture2D();
	if( !Texture2D )
	{
		return;	// just like in D3D11
	}

	FOpenGLTextureBase* Texture = (FOpenGLTextureBase*)Texture2D;

	GLuint FramebufferToDelete = 0;
	GLuint RenderbufferToDelete = 0;
	const FOpenGLTextureFormat& GLFormat = GOpenGLTextureFormats[TextureRHI->GetFormat()];

	bool bFloatFormat = false;
	bool bUnsupportedFormat = false;
	bool bDepthFormat = false;
	bool bDepthStencilFormat = false;

	switch( TextureRHI->GetFormat() )
	{
	case PF_DepthStencil:
		bDepthStencilFormat = true;
		// pass-through
	case PF_ShadowDepth:
	case PF_D24:
		bDepthFormat = true;
		break;

	case PF_A32B32G32R32F:
	case PF_FloatRGBA:
	case PF_FloatRGB:
	case PF_R32_FLOAT:
	case PF_G16R16F:
	case PF_G16R16F_FILTER:
	case PF_G32R32F:
	case PF_R16F:
	case PF_R16F_FILTER:
	case PF_FloatR11G11B10:
		bFloatFormat = true;
		break;

	case PF_DXT1:
	case PF_DXT3:
	case PF_DXT5:
	case PF_UYVY:
	case PF_BC5:
	case PF_PVRTC2:
	case PF_PVRTC4:
	case PF_ATC_RGB:
	case PF_ATC_RGBA_E:
	case PF_ATC_RGBA_I:
		bUnsupportedFormat = true;
		break;

	default:	// the rest is assumed to be integer formats with one or more of ARG and B components in OpenGL
		break;
	}

	if( bUnsupportedFormat )
	{
#if UE_BUILD_DEBUG
		check(0);
#endif
		return;
	}

	check( !bDepthFormat || FOpenGL::SupportsDepthStencilReadSurface() );
	check( !bFloatFormat || FOpenGL::SupportsFloatReadSurface() );
	const GLenum Attachment = bDepthFormat ? (bDepthStencilFormat ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT) : GL_COLOR_ATTACHMENT0;
	const bool bIsColorBuffer = (Texture->Attachment == GL_COLOR_ATTACHMENT0) || (Texture->Attachment == 0);

	const uint32 MipmapLevel = InFlags.GetMip();
	GLuint SourceFramebuffer = bIsColorBuffer ? GetOpenGLFramebuffer(1, &Texture, NULL, &MipmapLevel, NULL) : GetOpenGLFramebuffer(0, NULL, NULL, NULL, Texture);
	if (TextureRHI->IsMultisampled())
	{
		// OpenGL doesn't allow to read pixels from multisample framebuffers, we need a single sample copy
		glGenFramebuffers(1, &FramebufferToDelete);
		glBindFramebuffer(GL_FRAMEBUFFER, FramebufferToDelete);

		GLuint Renderbuffer = 0;
		glGenRenderbuffers(1, &RenderbufferToDelete);
		glBindRenderbuffer(GL_RENDERBUFFER, RenderbufferToDelete);
		glRenderbufferStorage(GL_RENDERBUFFER, GLFormat.InternalFormat[false], Texture2D->GetSizeX(), Texture2D->GetSizeY());
		glBindRenderbuffer(GL_RENDERBUFFER, 0);

		glFramebufferRenderbuffer(GL_FRAMEBUFFER, Attachment, GL_RENDERBUFFER, RenderbufferToDelete);
		FOpenGL::CheckFrameBuffer();
		glBindFramebuffer(UGL_READ_FRAMEBUFFER, SourceFramebuffer);
		FOpenGL::BlitFramebuffer(
			0, 0, Texture2D->GetSizeX(), Texture2D->GetSizeY(),
			0, 0, Texture2D->GetSizeX(), Texture2D->GetSizeY(),
			(bDepthFormat ? (bDepthStencilFormat ? (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT) : GL_DEPTH_BUFFER_BIT) : GL_COLOR_BUFFER_BIT),
			GL_NEAREST
			);

		SourceFramebuffer = FramebufferToDelete;
	}

	uint32 SizeX = Rect.Width();
	uint32 SizeY = Rect.Height();

	OutData.Empty( SizeX * SizeY * sizeof(FColor) );
	uint8* TargetBuffer = (uint8*)&OutData[OutData.AddUninitialized(SizeX * SizeY * sizeof(FColor))];

	glBindFramebuffer(UGL_READ_FRAMEBUFFER, SourceFramebuffer);
	FOpenGL::ReadBuffer( (!bDepthFormat && !bDepthStencilFormat && !SourceFramebuffer) ? GL_BACK : Attachment);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);

	if( bDepthFormat )
	{
		// Get the depth as luminosity, with non-transparent alpha.
		// If depth values are between 0 and 1, keep them, otherwise rescale them linearly so they fit within 0-1 range

		int32 DepthValueCount = SizeX * SizeY;
		int32 FloatDepthDataSize = sizeof(float) * DepthValueCount;
		float* FloatDepthData = (float*)FMemory::Malloc( FloatDepthDataSize );
		glReadPixels(Rect.Min.X, Rect.Min.Y, SizeX, SizeY, GL_DEPTH_COMPONENT, GL_FLOAT, FloatDepthData );

		// Determine minimal and maximal float value present in received data
		float MinValue = FLT_MAX;
		float MaxValue = FLT_MIN;
		float* DataPtr = FloatDepthData;
		for( int32 DepthValueIndex = 0; DepthValueIndex < DepthValueCount; ++DepthValueIndex, ++DataPtr )
		{
			if( *DataPtr < MinValue )
			{
				MinValue = *DataPtr;
			}

			if( *DataPtr > MaxValue )
			{
				MaxValue = *DataPtr;
			}
		}

		// If necessary, rescale the data.
		if( ( MinValue < 0.f ) || ( MaxValue > 1.f ) )
		{
			DataPtr = FloatDepthData;
			float RescaleFactor = MaxValue - MinValue;
			for( int32 DepthValueIndex = 0; DepthValueIndex < DepthValueCount; ++DepthValueIndex, ++DataPtr )
			{
				*DataPtr = ( *DataPtr - MinValue ) / RescaleFactor;
			}
		}

		// Convert the data into rgba8 buffer
		DataPtr = FloatDepthData;
		uint8* TargetPtr = TargetBuffer;
		for( int32 DepthValueIndex = 0; DepthValueIndex < DepthValueCount; ++DepthValueIndex )
		{
			uint8 Value = (uint8)( *DataPtr++ * 255.0f );
			*TargetPtr++ = Value;
			*TargetPtr++ = Value;
			*TargetPtr++ = Value;
			*TargetPtr++ = 255;
		}

		FMemory::Free( FloatDepthData );
	}
	else if( bFloatFormat )
	{
		bool bLinearToGamma = InFlags.GetLinearToGamma();

		// Determine minimal and maximal float value present in received data. Treat alpha separately.

		int32 PixelComponentCount = 4 * SizeX * SizeY;
		int32 FloatBGRADataSize = sizeof(float) * PixelComponentCount;
		float* FloatBGRAData = (float*)FMemory::Malloc( FloatBGRADataSize );
		if ( FOpenGL::SupportsBGRA8888() )
		{
			glReadPixels(Rect.Min.X, Rect.Min.Y, SizeX, SizeY, GL_BGRA, GL_FLOAT, FloatBGRAData );
			GLenum Error = glGetError();
			if (Error != GL_NO_ERROR)
			{
				glReadPixels(Rect.Min.X, Rect.Min.Y, SizeX, SizeY, GL_RGBA, GL_FLOAT, FloatBGRAData );
				Error = glGetError();
				if (Error == GL_NO_ERROR)
				{
					float* FloatDataPtr = FloatBGRAData;
					float* FloatDataPtrEnd = FloatBGRAData + PixelComponentCount;
					while (FloatDataPtr != FloatDataPtrEnd)
					{
						float Temp = FloatDataPtr[0];
						FloatDataPtr[0] = FloatDataPtr[2];
						FloatDataPtr[2] = Temp;
						FloatDataPtr += 4;
					}
				}
			}
		}
		else 
		{
			glReadPixels(Rect.Min.X, Rect.Min.Y, SizeX, SizeY, GL_RGBA, GL_FLOAT, FloatBGRAData );
		}
		// Determine minimal and maximal float values present in received data. Treat each component separately.
		float MinValue[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		float MaxValue[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		float* DataPtr = FloatBGRAData;
		for( int32 PixelComponentIndex = 0; PixelComponentIndex < PixelComponentCount; ++PixelComponentIndex, ++DataPtr )
		{
			int32 ComponentIndex = PixelComponentIndex % 4;
			MinValue[ComponentIndex] = FMath::Min<float>(*DataPtr,MinValue[ComponentIndex]);
			MaxValue[ComponentIndex] = FMath::Max<float>(*DataPtr,MaxValue[ComponentIndex]);
		}

		// Convert the data into BGRA8 buffer
		DataPtr = FloatBGRAData;
		uint8* TargetPtr = TargetBuffer;
		float RescaleFactor[4] = { MaxValue[0] - MinValue[0], MaxValue[1] - MinValue[1], MaxValue[2] - MinValue[2], MaxValue[3] - MinValue[3] };
		for( int32 PixelIndex = 0; PixelIndex < PixelComponentCount / 4; ++PixelIndex )
		{
			float R = (DataPtr[2] - MinValue[2]) / RescaleFactor[2]; 
			float G = (DataPtr[1] - MinValue[1]) / RescaleFactor[1];
			float B = (DataPtr[0] - MinValue[0]) / RescaleFactor[0]; 
			float A = (DataPtr[3] - MinValue[3]) / RescaleFactor[3]; 
			
			if ( !FOpenGL::SupportsBGRA8888() )
			{
				Swap<float>( R,B );
			}		   
			FColor NormalizedColor = FLinearColor( R,G,B,A ).ToFColor(bLinearToGamma);
			FMemory::Memcpy(TargetPtr,&NormalizedColor,sizeof(FColor));
			DataPtr += 4;
			TargetPtr += 4;
		}

		FMemory::Free( FloatBGRAData );
	}
#if PLATFORM_ANDROID && !PLATFORM_LUMINGL4
	else
	{
		// Flip texture data only for render targets, textures loaded from disk have Attachment set to 0 and don't need flipping.
		const bool bFlipTextureData = Texture->Attachment != 0;
		GLubyte* RGBAData = TargetBuffer;
		if (bFlipTextureData)
		{
			// OpenGL ES is limited in what it can do with ReadPixels
			const int32 PixelComponentCount = 4 * SizeX * SizeY;
			const int32 RGBADataSize = sizeof(GLubyte) * PixelComponentCount;
			RGBAData = (GLubyte*)FMemory::Malloc(RGBADataSize);
		}

		glReadPixels(Rect.Min.X, Rect.Min.Y, SizeX, SizeY, GL_RGBA, GL_UNSIGNED_BYTE, RGBAData);

		if (bFlipTextureData)
		{
			//OpenGL ES reads the pixels "upside down" from what we're expecting (flipped vertically), so we need to transfer the data from the bottom up.
			uint8* TargetPtr = TargetBuffer;
			int32 Stride = SizeX * 4;
			int32 FlipHeight = SizeY;
			GLubyte* LinePtr = RGBAData + (SizeY - 1) * Stride;

			while (FlipHeight--)
			{
				GLubyte* DataPtr = LinePtr;
				int32 Pixels = SizeX;
				while (Pixels--)
				{
					TargetPtr[0] = DataPtr[2];
					TargetPtr[1] = DataPtr[1];
					TargetPtr[2] = DataPtr[0];
					TargetPtr[3] = DataPtr[3];
					DataPtr += 4;
					TargetPtr += 4;
				}
				LinePtr -= Stride;
			}

			FMemory::Free(RGBAData);
		}
	}
#else
	else
	{
		// It's a simple int format. OpenGL converts them internally to what we want.
		glReadPixels(Rect.Min.X, Rect.Min.Y, SizeX, SizeY, GL_BGRA, UGL_ABGR8, TargetBuffer );
	}
#endif

	glPixelStorei(GL_PACK_ALIGNMENT, 4);

	if( FramebufferToDelete )
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glDeleteFramebuffers( 1, &FramebufferToDelete );
	}

	if( RenderbufferToDelete )
	{
		glDeleteRenderbuffers( 1, &RenderbufferToDelete );
	}

	ContextState.Framebuffer = (GLuint)-1;
}

void FOpenGLDynamicRHI::RHIReadSurfaceData(FRHITexture* TextureRHI,FIntRect Rect,TArray<FColor>& OutData, FReadSurfaceDataFlags InFlags)
{
	if (!ensure(TextureRHI))
	{
		OutData.Empty();
		OutData.AddZeroed(Rect.Width() * Rect.Height());
		return;
	}

	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
	
	RHITHREAD_GLCOMMAND_PROLOGUE();
	TArray<uint8> Temp;

	FOpenGLContextState& ContextState = GetContextStateForCurrentContext();
	OutData.Empty();
	if (&ContextState != &InvalidContextState)
	{
		ReadSurfaceDataRaw(ContextState, TextureRHI, Rect, Temp, InFlags);

		uint32 Size = Rect.Width() * Rect.Height();

		OutData.AddUninitialized(Size);

		FMemory::Memcpy(OutData.GetData(), Temp.GetData(), Size * sizeof(FColor));
	}
	RHITHREAD_GLCOMMAND_EPILOGUE();
}

void FOpenGLDynamicRHI::RHIReadSurfaceData(FRHITexture* TextureRHI, FIntRect Rect, TArray<FLinearColor>& OutData, FReadSurfaceDataFlags InFlags)
{
	// Verify requirements, but don't crash
	// Ignore texture format here, GL will convert it for us in glReadPixels
	if (!ensure(FOpenGL::SupportsFloatReadSurface()) || !ensure(TextureRHI))
	{
		return;
	}
	
	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
	
	RHITHREAD_GLCOMMAND_PROLOGUE();
	VERIFY_GL_SCOPE();

	FOpenGLTextureBase* Texture = GetOpenGLTextureFromRHITexture(TextureRHI);
	if (!ensure(Texture))
	{
		return;
	}

	// Get framebuffer for texture
	const uint32 MipmapLevel = InFlags.GetMip();
	GLuint SourceFramebuffer = GetOpenGLFramebuffer(1, &Texture, NULL, &MipmapLevel, NULL);

	uint32 SizeX = Rect.Width();
	uint32 SizeY = Rect.Height();

	// Initialize output
	OutData.Empty(SizeX * SizeY);
	OutData.AddUninitialized(SizeX * SizeY);

	// Bind the framebuffer
	// @TODO: Do we need to worry about multisampling?
	glBindFramebuffer(UGL_READ_FRAMEBUFFER, SourceFramebuffer);
	FOpenGL::ReadBuffer(SourceFramebuffer == 0 ? GL_BACK : GL_COLOR_ATTACHMENT0);

	// Read the float data from the buffer directly into the output data
	// @TODO: Do we need to support BGRA?
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(Rect.Min.X, Rect.Min.Y, SizeX, SizeY, GL_RGBA, GL_FLOAT, OutData.GetData());
	glPixelStorei(GL_PACK_ALIGNMENT, 4);

	GetContextStateForCurrentContext().Framebuffer = (GLuint)-1;
	
	RHITHREAD_GLCOMMAND_EPILOGUE();
}

void FOpenGLDynamicRHI::RHIMapStagingSurface(FRHITexture* TextureRHI, FRHIGPUFence* FenceRHI, void*& OutData, int32& OutWidth, int32& OutHeight, uint32 GPUIndex)
{
	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

	RHITHREAD_GLCOMMAND_PROLOGUE();

	VERIFY_GL_SCOPE();
	
	FOpenGLTexture2D* Texture2D = (FOpenGLTexture2D*)TextureRHI->GetTexture2D();
	check(Texture2D);
	check(Texture2D->IsStaging());

	OutWidth = Texture2D->GetSizeX();
	OutHeight = Texture2D->GetSizeY();
	
	uint32 Stride = 0;
	OutData = Texture2D->Lock( 0, 0, RLM_ReadOnly, Stride );
	RHITHREAD_GLCOMMAND_EPILOGUE();
}

void FOpenGLDynamicRHI::RHIUnmapStagingSurface(FRHITexture* TextureRHI, uint32 GPUIndex)
{
	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

	RHITHREAD_GLCOMMAND_PROLOGUE();

	VERIFY_GL_SCOPE();
	
	FOpenGLTexture2D* Texture2D = (FOpenGLTexture2D*)TextureRHI->GetTexture2D();
	check(Texture2D);

	Texture2D->Unlock( 0, 0 );
	RHITHREAD_GLCOMMAND_EPILOGUE();
}

void FOpenGLDynamicRHI::RHIReadSurfaceFloatData(FRHITexture* TextureRHI,FIntRect Rect,TArray<FFloat16Color>& OutData,ECubeFace CubeFace,int32 ArrayIndex,int32 MipIndex)
{
	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

	RHITHREAD_GLCOMMAND_PROLOGUE();

	VERIFY_GL_SCOPE();	

	//reading from arrays only supported on SM5 and up.
	check(FOpenGL::SupportsFloatReadSurface() && (ArrayIndex == 0 || GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM5));	
	FOpenGLTextureBase* Texture = GetOpenGLTextureFromRHITexture(TextureRHI);
	check(TextureRHI->GetFormat() == PF_FloatRGBA);

	const uint32 MipmapLevel = MipIndex;

	// Temp FBO is introduced to prevent a ballooning of FBO objects, which can have a detrimental
	// impact on object management performance in the driver, only for CubeMapArray presently
	// as it is the target that really drives  FBO permutations
	const bool bTempFBO = Texture->Target == GL_TEXTURE_CUBE_MAP_ARRAY;
	uint32 Index = uint32(CubeFace) + ( (Texture->Target == GL_TEXTURE_CUBE_MAP_ARRAY) ? 6 : 1) * ArrayIndex;

	GLuint SourceFramebuffer = 0;

	if (bTempFBO)
	{
		glGenFramebuffers( 1, &SourceFramebuffer);

		glBindFramebuffer(UGL_READ_FRAMEBUFFER, SourceFramebuffer);

		FOpenGL::FramebufferTextureLayer(UGL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, Texture->GetResource(), MipmapLevel, Index);
	}
	else
	{
		SourceFramebuffer = GetOpenGLFramebuffer(1, &Texture, &Index, &MipmapLevel, NULL);
	}

	uint32 SizeX = Rect.Width();
	uint32 SizeY = Rect.Height();

	OutData.Empty(SizeX * SizeY);
	OutData.AddUninitialized(SizeX * SizeY);

	glBindFramebuffer(UGL_READ_FRAMEBUFFER, SourceFramebuffer);
	FOpenGL::ReadBuffer(SourceFramebuffer == 0 ? GL_BACK : GL_COLOR_ATTACHMENT0);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);	

	if (FOpenGL::GetReadHalfFloatPixelsEnum() == GL_FLOAT)
	{
		// Slow path: Some Adreno devices won't work with HALF_FLOAT ReadPixels
		TArray<FLinearColor> FloatData;
		// 4 float components per texel (RGBA)
		FloatData.AddUninitialized(SizeX * SizeY);
		FMemory::Memzero(FloatData.GetData(),SizeX * SizeY*sizeof(FLinearColor));
		glReadPixels(Rect.Min.X, Rect.Min.Y, SizeX, SizeY, GL_RGBA, GL_FLOAT, FloatData.GetData());
		FLinearColor* FloatDataPtr = FloatData.GetData();
		for (uint32 DataIndex = 0; DataIndex < SizeX * SizeY; ++DataIndex, ++FloatDataPtr)
		{
			OutData[DataIndex] = FFloat16Color(*FloatDataPtr);
		}
	}
	else
	{
		glReadPixels(Rect.Min.X, Rect.Min.Y, SizeX, SizeY, GL_RGBA, FOpenGL::GetReadHalfFloatPixelsEnum(), OutData.GetData());
	}

	glPixelStorei(GL_PACK_ALIGNMENT, 4);

	if (bTempFBO)
	{
		glDeleteFramebuffers( 1, &SourceFramebuffer);
	}

	GetContextStateForCurrentContext().Framebuffer = (GLuint)-1;
	RHITHREAD_GLCOMMAND_EPILOGUE();
}

void FOpenGLDynamicRHI::RHIRead3DSurfaceFloatData(FRHITexture* TextureRHI,FIntRect Rect,FIntPoint ZMinMax,TArray<FFloat16Color>& OutData)
{
	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

	RHITHREAD_GLCOMMAND_PROLOGUE();

	VERIFY_GL_SCOPE();

	check( FOpenGL::SupportsFloatReadSurface() );
	check( FOpenGL::SupportsTexture3D() );
	check( TextureRHI->GetFormat() == PF_FloatRGBA );

	FRHITexture3D* Texture3DRHI = TextureRHI->GetTexture3D();
	FOpenGLTextureBase* Texture = GetOpenGLTextureFromRHITexture(TextureRHI);

	uint32 SizeX = Rect.Width();
	uint32 SizeY = Rect.Height();
	uint32 SizeZ = ZMinMax.Y - ZMinMax.X;

	// Allocate the output buffer.
	OutData.Empty(SizeX * SizeY * SizeZ * sizeof(FFloat16Color));
	OutData.AddZeroed(SizeX * SizeY * SizeZ);

	// Set up the source as a temporary FBO
	uint32 MipmapLevel = 0;
	uint32 Index = 0;
	GLuint SourceFramebuffer = 0;
	glGenFramebuffers( 1, &SourceFramebuffer);
	glBindFramebuffer(UGL_READ_FRAMEBUFFER, SourceFramebuffer);

	// Set up the destination as a temporary texture
	GLuint TempTexture = 0;
	FOpenGL::GenTextures(1, &TempTexture);
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_3D, TempTexture );
	FOpenGL::TexImage3D( GL_TEXTURE_3D, 0, GL_RGBA16F, SizeX, SizeY, SizeZ, 0, GL_RGBA, GL_HALF_FLOAT, NULL );

	// Copy the pixels within the specified region, minimizing the amount of data that needs to be transferred from GPU to CPU memory
	for ( uint32 Z=0; Z < SizeZ; ++Z )
	{
		FOpenGL::FramebufferTextureLayer(UGL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, Texture->GetResource(), MipmapLevel, ZMinMax.X + Z);
		FOpenGL::ReadBuffer(SourceFramebuffer == 0 ? GL_BACK : GL_COLOR_ATTACHMENT0);
		FOpenGL::CopyTexSubImage3D( GL_TEXTURE_3D, 0, 0, 0, Z, Rect.Min.X, Rect.Min.Y, SizeX, SizeY );
	}

	// Grab the raw data from the temp texture.
	glPixelStorei( GL_PACK_ALIGNMENT, 1 );
	FOpenGL::GetTexImage( GL_TEXTURE_3D, 0, GL_RGBA, GL_HALF_FLOAT, OutData.GetData() );
	glPixelStorei( GL_PACK_ALIGNMENT, 4 );

	// Clean up
	FOpenGLContextState& ContextState = GetContextStateForCurrentContext();
	auto& TextureState = ContextState.Textures[0];
	glBindTexture(GL_TEXTURE_3D, (TextureState.Target == GL_TEXTURE_3D) ? TextureState.Resource : 0);
	glActiveTexture( GL_TEXTURE0 + ContextState.ActiveTexture );
	glDeleteFramebuffers( 1, &SourceFramebuffer);
	FOpenGL::DeleteTextures( 1, &TempTexture );
	ContextState.Framebuffer = (GLuint)-1;

	RHITHREAD_GLCOMMAND_EPILOGUE();
}



void FOpenGLDynamicRHI::BindPendingFramebuffer( FOpenGLContextState& ContextState )
{
	VERIFY_GL_SCOPE();

	check((GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM5) || !PendingState.bFramebufferSetupInvalid);

	if (ContextState.Framebuffer != PendingState.Framebuffer)
	{
		if (PendingState.Framebuffer)
		{
			glBindFramebuffer(GL_FRAMEBUFFER, PendingState.Framebuffer);

			FOpenGL::ReadBuffer( PendingState.FirstNonzeroRenderTarget >= 0 ? GL_COLOR_ATTACHMENT0 + PendingState.FirstNonzeroRenderTarget : GL_NONE);
			GLenum DrawFramebuffers[MaxSimultaneousRenderTargets];
			const GLint MaxDrawBuffers = GMaxOpenGLDrawBuffers;

			for (int32 RenderTargetIndex = 0; RenderTargetIndex < MaxDrawBuffers; ++RenderTargetIndex)
			{
				DrawFramebuffers[RenderTargetIndex] = PendingState.RenderTargets[RenderTargetIndex] ? GL_COLOR_ATTACHMENT0 + RenderTargetIndex : GL_NONE;
			}
			FOpenGL::DrawBuffers(MaxDrawBuffers, DrawFramebuffers);
		}
		else
		{
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			FOpenGL::ReadBuffer(GL_BACK);
			FOpenGL::DrawBuffer(GL_BACK);
		}

		ContextState.Framebuffer = PendingState.Framebuffer;
	}
}


void FOpenGLDynamicRHI::RHIBeginRenderPass(const FRHIRenderPassInfo& InInfo, const TCHAR* InName)
{
	FRHISetRenderTargetsInfo RTInfo;
	InInfo.ConvertToRenderTargetsInfo(RTInfo);
	SetRenderTargetsAndClear(RTInfo);

	RenderPassInfo = InInfo;

	if (InInfo.bOcclusionQueries)
	{
		extern void BeginOcclusionQueryBatch(uint32);
		BeginOcclusionQueryBatch(InInfo.NumOcclusionQueries);
	}

#if PLATFORM_ANDROID && !PLATFORM_LUMIN && !PLATFORM_LUMINGL4
	if (FAndroidOpenGL::RequiresAdrenoTilingModeHint())
	{
		FAndroidOpenGL::EnableAdrenoTilingModeHint(FCString::Strcmp(InName, TEXT("SceneColorRendering")) == 0);
	}
#endif
}

void FOpenGLDynamicRHI::RHIEndRenderPass()
{
	if (RenderPassInfo.bOcclusionQueries)
	{
		extern void EndOcclusionQueryBatch();
		EndOcclusionQueryBatch();
	}

	for (int32 Index = 0; Index < MaxSimultaneousRenderTargets; ++Index)
	{
		if (!RenderPassInfo.ColorRenderTargets[Index].RenderTarget)
		{
			break;
		}
		if (RenderPassInfo.ColorRenderTargets[Index].ResolveTarget)
		{
			RHICopyToResolveTarget(RenderPassInfo.ColorRenderTargets[Index].RenderTarget, RenderPassInfo.ColorRenderTargets[Index].ResolveTarget, RenderPassInfo.ResolveParameters);
		}
	}

	if (RenderPassInfo.DepthStencilRenderTarget.DepthStencilTarget && RenderPassInfo.DepthStencilRenderTarget.ResolveTarget)
	{
		RHICopyToResolveTarget(RenderPassInfo.DepthStencilRenderTarget.DepthStencilTarget, RenderPassInfo.DepthStencilRenderTarget.ResolveTarget, RenderPassInfo.ResolveParameters);
	}

	// Drop depth and stencil to avoid export
	if (RenderPassInfo.DepthStencilRenderTarget.DepthStencilTarget)
	{
		ERenderTargetActions DepthActions = GetDepthActions(RenderPassInfo.DepthStencilRenderTarget.Action);
		ERenderTargetActions StencilActions = GetStencilActions(RenderPassInfo.DepthStencilRenderTarget.Action);
		bool bDiscardDepth = GetStoreAction(DepthActions) == ERenderTargetStoreAction::ENoAction;
		bool bDiscardStencil = GetStoreAction(StencilActions) == ERenderTargetStoreAction::ENoAction;
		if (bDiscardDepth || bDiscardStencil)
		{
			RHIDiscardRenderTargets(bDiscardDepth, bDiscardStencil, 0);
		}
	}

	FRHIRenderTargetView RTV(nullptr, ERenderTargetLoadAction::ENoAction);
	FRHIDepthRenderTargetView DepthRTV(nullptr, ERenderTargetLoadAction::ENoAction, ERenderTargetStoreAction::ENoAction);
	SetRenderTargets(1, &RTV, &DepthRTV);
}

void FOpenGLDynamicRHI::RHINextSubpass()
{
	IRHICommandContext::RHINextSubpass();
	
	if (RenderPassInfo.SubpassHint == ESubpassHint::DepthReadSubpass)
	{
		FOpenGL::FrameBufferFetchBarrier();
	}
}

void FOpenGLDynamicRHI::RHIBeginTransitions(TArrayView<const FRHITransition*> Transitions)
{
}

void FOpenGLDynamicRHI::RHIEndTransitions(TArrayView<const FRHITransition*> Transitions)
{
}
