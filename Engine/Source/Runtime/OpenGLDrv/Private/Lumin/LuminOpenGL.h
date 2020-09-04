// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if !PLATFORM_LUMINGL4

#include "LuminEGL.h"
#include "RenderingThread.h"

#include <EGL/eglext.h>
#include <EGL/eglplatform.h>
#include <GLES3/gl31.h>
#include <GLES2/gl2ext.h>

typedef EGLSyncKHR		UGLsync;
#define GLdouble		GLfloat
typedef khronos_int64_t GLint64;
typedef khronos_uint64_t GLuint64;
#define GL_CLAMP		GL_CLAMP_TO_EDGE

#include "OpenGLES.h"

namespace GL_EXT
{
	extern PFNEGLGETSYSTEMTIMENVPROC eglGetSystemTimeNV;
	extern PFNEGLCREATESYNCKHRPROC eglCreateSyncKHR;
	extern PFNEGLDESTROYSYNCKHRPROC eglDestroySyncKHR;
	extern PFNEGLCLIENTWAITSYNCKHRPROC eglClientWaitSyncKHR;

	extern PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
	extern PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;

	extern PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;
}

using namespace GL_EXT;

struct FLuminOpenGL : public FOpenGLES
{
	static FORCEINLINE EShaderPlatform GetShaderPlatform()
	{
		return SP_OPENGL_ES3_1_ANDROID;
	}
	static FORCEINLINE bool HasHardwareHiddenSurfaceRemoval() { return bHasHardwareHiddenSurfaceRemoval; };

	// Optional:
	static FORCEINLINE void QueryTimestampCounter(GLuint QueryID)
	{
	}

	static FORCEINLINE void GetQueryObject(GLuint QueryId, EQueryMode QueryMode, GLuint *OutResult)
	{
		GLenum QueryName = (QueryMode == QM_Result) ? GL_QUERY_RESULT_EXT : GL_QUERY_RESULT_AVAILABLE_EXT;
		glGetQueryObjectuiv(QueryId, QueryName, OutResult);
	}

	static FORCEINLINE void GetQueryObject(GLuint QueryId, EQueryMode QueryMode, GLuint64* OutResult)
	{
		GLuint Result = 0;
		GetQueryObject(QueryId, QueryMode, &Result);
		*OutResult = Result;
	}

	static FORCEINLINE void DeleteSync(UGLsync Sync)
	{
		if (GUseThreadedRendering)
		{
			//handle error here
			EGLBoolean Result = eglDestroySyncKHR(LuminEGL::GetInstance()->GetDisplay(), Sync);
			if (Result == EGL_FALSE)
			{
				//handle error here
			}
		}
	}

	static FORCEINLINE UGLsync FenceSync(GLenum Condition, GLbitfield Flags)
	{
		check(Condition == GL_SYNC_GPU_COMMANDS_COMPLETE && Flags == 0);
		return GUseThreadedRendering ? eglCreateSyncKHR(LuminEGL::GetInstance()->GetDisplay(), EGL_SYNC_FENCE_KHR, NULL) : 0;
	}

	static FORCEINLINE bool IsSync(UGLsync Sync)
	{
		if (GUseThreadedRendering)
		{
			return (Sync != EGL_NO_SYNC_KHR) ? true : false;
		}
		else
		{
			return true;
		}
	}

	static FORCEINLINE EFenceResult ClientWaitSync(UGLsync Sync, GLbitfield Flags, GLuint64 Timeout)
	{
		if (GUseThreadedRendering)
		{
			// check( Flags == GL_SYNC_FLUSH_COMMANDS_BIT );
			GLenum Result = eglClientWaitSyncKHR(LuminEGL::GetInstance()->GetDisplay(), Sync, EGL_SYNC_FLUSH_COMMANDS_BIT_KHR, Timeout);
			switch (Result)
			{
			case EGL_TIMEOUT_EXPIRED_KHR:		return FR_TimeoutExpired;
			case EGL_CONDITION_SATISFIED_KHR:	return FR_ConditionSatisfied;
			}
			return FR_WaitFailed;
		}
		else
		{
			return FR_ConditionSatisfied;
		}
		return FR_WaitFailed;
	}

	// MRT triggers black rendering for the SensoryWare plugin. Turn it off for now.
	static FORCEINLINE bool SupportsMultipleRenderTargets()				{ return false; }
	static FORCEINLINE bool SupportsImageExternal()						{ return bSupportsImageExternal; }

	enum class EImageExternalType : uint8
	{
		None,
		ImageExternal100,
		ImageExternal300,
		ImageExternalESSL300
	};

	static FORCEINLINE EImageExternalType GetImageExternalType() { return ImageExternalType; }

	static void ProcessExtensions(const FString& ExtensionsString);

	/** Whether device supports image external */
	static bool bSupportsImageExternal;

	/** Type of image external supported */
	static EImageExternalType ImageExternalType;
};

typedef FLuminOpenGL FOpenGL;


/** Unreal tokens that maps to different OpenGL tokens by platform. */
#undef UGL_DRAW_FRAMEBUFFER
#define UGL_DRAW_FRAMEBUFFER	GL_DRAW_FRAMEBUFFER_NV
#undef UGL_READ_FRAMEBUFFER
#define UGL_READ_FRAMEBUFFER	GL_READ_FRAMEBUFFER_NV

#endif
