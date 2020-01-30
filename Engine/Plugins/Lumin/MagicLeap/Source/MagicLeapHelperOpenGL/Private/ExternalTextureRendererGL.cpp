// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExternalTextureRendererGL.h"
#include "IMagicLeapHelperOpenGLPlugin.h"

#if PLATFORM_LUMIN
#include "OpenGLDrv.h"
#endif // PLATFORM_LUMIN

#if PLATFORM_LUMIN
class FMagicLeapExternalTextureRendererGLImpl
{
public:
	FMagicLeapExternalTextureRendererGLImpl()
	: Image(EGL_NO_IMAGE_KHR)
	{}

	EGLImageKHR Image;
};
#endif // PLATFORM_LUMIN

FExternalTextureRendererGL::FExternalTextureRendererGL()
#if PLATFORM_LUMIN
: Impl(new FMagicLeapExternalTextureRendererGLImpl())
#endif // PLATFORM_LUMIN
{

}

FExternalTextureRendererGL::~FExternalTextureRendererGL()
{
#if PLATFORM_LUMIN
	delete Impl;
	Impl = nullptr;
#endif // PLATFORM_LUMIN
}

bool FExternalTextureRendererGL::CreateImageKHR(void* NativeBufferHandle)
{
#if PLATFORM_LUMIN
	Impl->Image = eglCreateImageKHR(LuminEGL::GetInstance()->GetDisplay(), EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, (EGLClientBuffer)NativeBufferHandle, NULL);
	if (Impl->Image == EGL_NO_IMAGE_KHR)
	{
		EGLint errorcode = eglGetError();
		UE_LOG(LogMagicLeapHelperOpenGL, Error, TEXT("Failed to create EGLImage from the buffer. %d"), errorcode);
		return false;
	}

	return true;
#else
	return false;
#endif // PLATFORM_LUMIN
}

void FExternalTextureRendererGL::DestroyImageKHR()
{
#if PLATFORM_LUMIN
	if (Impl->Image != EGL_NO_IMAGE_KHR)
	{
		eglDestroyImageKHR(eglGetCurrentDisplay(), Impl->Image);
		Impl->Image = EGL_NO_IMAGE_KHR;
	}
#endif // PLATFORM_LUMIN
}

void FExternalTextureRendererGL::BindImageKHRToTexture(int32 TextureID)
{
#if PLATFORM_LUMIN
	if (Impl->Image != EGL_NO_IMAGE_KHR)
	{
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_EXTERNAL_OES, TextureID);
		glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, Impl->Image);
		glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
	}
#endif // PLATFORM_LUMIN
}
