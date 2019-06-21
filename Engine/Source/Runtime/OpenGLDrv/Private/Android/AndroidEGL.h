// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AndroidEGL.h: Private EGL definitions for Android-specific functionality
=============================================================================*/
#pragma once

#include "Android/AndroidPlatform.h"

#if USE_ANDROID_OPENGL

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

struct AndroidESPImpl;
struct ANativeWindow;

#ifndef USE_ANDROID_EGL_NO_ERROR_CONTEXT
#if UE_BUILD_SHIPPING
#define USE_ANDROID_EGL_NO_ERROR_CONTEXT 1
#else
#define USE_ANDROID_EGL_NO_ERROR_CONTEXT 0
#endif
#endif // USE_ANDROID_EGL_NO_ERROR_CONTEXT

DECLARE_LOG_CATEGORY_EXTERN(LogEGL, Log, All);

struct FPlatformOpenGLContext
{
	EGLContext	eglContext;
	GLuint		ViewportFramebuffer;
	EGLSurface	eglSurface;
	GLuint		DefaultVertexArrayObject;

	FPlatformOpenGLContext()
	{
		Reset();
	}

	void Reset()
	{
		eglContext = EGL_NO_CONTEXT;
		eglSurface = EGL_NO_SURFACE;
		ViewportFramebuffer = 0;
		DefaultVertexArrayObject = 0;
	}
};


class AndroidEGL
{
public:
	enum APIVariant
	{
		AV_OpenGLES,
		AV_OpenGLCore
	};

	static AndroidEGL* GetInstance();
	~AndroidEGL();	

	bool IsInitialized();
	void InitBackBuffer();
	void DestroyBackBuffer();
	void Init( APIVariant API, uint32 MajorVersion, uint32 MinorVersion, bool bDebug);
	void ReInit();
	void UnBind();
	bool SwapBuffers(int32 SyncInterval);
	void Terminate();
	void InitSurface(bool bUseSmallSurface, bool bCreateWndSurface);

	void GetDimensions(uint32& OutWidth, uint32& OutHeight);
	
	EGLDisplay GetDisplay() const;
	ANativeWindow* GetNativeWindow() const;

	EGLContext CreateContext(EGLContext InSharedContext = EGL_NO_CONTEXT);
	int32 GetError();
	EGLBoolean SetCurrentContext(EGLContext InContext, EGLSurface InSurface);

	void AcquireCurrentRenderingContext();
	void ReleaseContextOwnership();

	GLuint GetOnScreenColorRenderBuffer();
	GLuint GetResolveFrameBuffer();
	bool IsCurrentContextValid();
	EGLContext  GetCurrentContext(  );
	void SetCurrentSharedContext();
	void SetCurrentRenderingContext();
	uint32_t GetCurrentContextType();
	FPlatformOpenGLContext* GetRenderingContext();

	// recreate the EGL surface for the current hardware window.
	void SetRenderContextWindowSurface();

	// Called from game thread when a window is reinited.
	void RefreshWindowSize();

protected:
	AndroidEGL();
	static AndroidEGL* Singleton ;

private:
	void InitEGL(APIVariant API);
	void TerminateEGL();

	void CreateEGLSurface(ANativeWindow* InWindow, bool bCreateWndSurface);
	void DestroySurface();

	bool InitContexts();
	void DestroyContext(EGLContext InContext);

	void ResetDisplay();

	AndroidESPImpl* PImplData;

	void ResetInternal();
	void LogConfigInfo(EGLConfig  EGLConfigInfo);

	// Actual Update to the egl surface to match the GT's requested size.
	void ResizeRenderContextSurface();

	bool bSupportsKHRCreateContext;
	bool bSupportsKHRSurfacelessContext;
	bool bSupportsKHRNoErrorContext;

	int *ContextAttributes;
};

#endif
