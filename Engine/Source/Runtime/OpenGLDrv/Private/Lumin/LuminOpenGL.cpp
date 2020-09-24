// Copyright Epic Games, Inc. All Rights Reserved.


#include "CoreMinimal.h"

#if !PLATFORM_LUMINGL4

#include "OpenGLDrvPrivate.h"
#include "OpenGLES.h"
#include "Android/AndroidApplication.h"

namespace GL_EXT
{
	PFNEGLGETSYSTEMTIMENVPROC eglGetSystemTimeNV = NULL;
	PFNEGLCREATESYNCKHRPROC eglCreateSyncKHR = NULL;
	PFNEGLDESTROYSYNCKHRPROC eglDestroySyncKHR = NULL;
	PFNEGLCLIENTWAITSYNCKHRPROC eglClientWaitSyncKHR = NULL;
	PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR = NULL;
	PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR = NULL;

	PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES = NULL;
}

struct FPlatformOpenGLDevice
{

	void SetCurrentSharedContext();
	void SetCurrentRenderingContext();
	void SetCurrentNULLContext();

	FPlatformOpenGLDevice();
	~FPlatformOpenGLDevice();
	void Init();
	void LoadEXT();
	void Terminate();
	void ReInit();
};


FPlatformOpenGLDevice::~FPlatformOpenGLDevice()
{
	LuminEGL::GetInstance()->DestroyBackBuffer();
	LuminEGL::GetInstance()->Terminate();
}

FPlatformOpenGLDevice::FPlatformOpenGLDevice()
{
}

void FPlatformOpenGLDevice::Init()
{
	extern void InitDebugContext();

	PlatformRenderingContextSetup(this);

	LoadEXT();

	InitDefaultGLContextState();
	InitDebugContext();

	PlatformSharedContextSetup(this);
	InitDefaultGLContextState();
	InitDebugContext();

	LuminEGL::GetInstance()->InitBackBuffer(); //can be done only after context is made current.
}

FPlatformOpenGLDevice* PlatformCreateOpenGLDevice()
{
	FPlatformOpenGLDevice* Device = new FPlatformOpenGLDevice();
	Device->Init();
	return Device;
}

bool PlatformCanEnableGPUCapture()
{
	return false;
}

void PlatformReleaseOpenGLContext(FPlatformOpenGLDevice* Device, FPlatformOpenGLContext* Context)
{
}

void* PlatformGetWindow(FPlatformOpenGLContext* Context, void** AddParam)
{
	check(Context);

	return (void*)&Context->eglContext;
}

bool PlatformBlitToViewport(FPlatformOpenGLDevice* Device, const FOpenGLViewport& Viewport, uint32 BackbufferSizeX, uint32 BackbufferSizeY, bool bPresent, bool bLockToVsync)
{
	int32 SyncInterval = RHIGetSyncInterval();
	FPlatformOpenGLContext* const Context = Viewport.GetGLContext();
	check(Context && Context->eglContext);
	FScopeContext ScopeContext(Context);
	if (bPresent && Viewport.GetCustomPresent())
	{
		glBindFramebuffer(GL_FRAMEBUFFER, Context->ViewportFramebuffer);
		bPresent = Viewport.GetCustomPresent()->Present(SyncInterval);
	}
	if (bPresent)
	{
		// SwapBuffers not supported on Lumin EGL; surfaceless.
		// LuminEGL::GetInstance()->SwapBuffers();
	}
	return bPresent;
}

void PlatformRenderingContextSetup(FPlatformOpenGLDevice* Device)
{
	Device->SetCurrentRenderingContext();
}

void PlatformFlushIfNeeded()
{
}

void PlatformRebindResources(FPlatformOpenGLDevice* Device)
{
}

void PlatformSharedContextSetup(FPlatformOpenGLDevice* Device)
{
	Device->SetCurrentSharedContext();
}

void PlatformNULLContextSetup()
{
	LuminEGL::GetInstance()->SetCurrentContext(EGL_NO_CONTEXT, EGL_NO_SURFACE);
}

EOpenGLCurrentContext PlatformOpenGLCurrentContext(FPlatformOpenGLDevice* Device)
{
	return (EOpenGLCurrentContext)LuminEGL::GetInstance()->GetCurrentContextType();
}

void* PlatformOpenGLCurrentContextHandle(FPlatformOpenGLDevice* Device)
{
	return LuminEGL::GetInstance()->GetCurrentContext();
}

void PlatformRestoreDesktopDisplayMode()
{
}

bool PlatformInitOpenGL()
{
	FOpenGLES::CurrentFeatureLevelSupport = FOpenGLES::EFeatureLevelSupport::ES31;

	return true;
}

bool PlatformOpenGLContextValid()
{
	return LuminEGL::GetInstance()->IsCurrentContextValid();
}

void PlatformGetBackbufferDimensions(uint32& OutWidth, uint32& OutHeight)
{
	LuminEGL::GetInstance()->GetDimensions(OutWidth, OutHeight);
}

// =============================================================

void PlatformGetNewOcclusionQuery(GLuint* OutQuery, uint64* OutQueryContext)
{
}

bool PlatformContextIsCurrent(uint64 QueryContext)
{
	return true;
}

void FPlatformOpenGLDevice::LoadEXT()
{
	eglGetSystemTimeNV = (PFNEGLGETSYSTEMTIMENVPROC)((void*)eglGetProcAddress("eglGetSystemTimeNV"));
	eglCreateSyncKHR = (PFNEGLCREATESYNCKHRPROC)((void*)eglGetProcAddress("eglCreateSyncKHR"));
	eglDestroySyncKHR = (PFNEGLDESTROYSYNCKHRPROC)((void*)eglGetProcAddress("eglDestroySyncKHR"));
	eglClientWaitSyncKHR = (PFNEGLCLIENTWAITSYNCKHRPROC)((void*)eglGetProcAddress("eglClientWaitSyncKHR"));

	eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)((void*)eglGetProcAddress("eglCreateImageKHR"));
	eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC)((void*)eglGetProcAddress("eglDestroyImageKHR"));

	glEGLImageTargetTexture2DOES = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)((void*)eglGetProcAddress("glEGLImageTargetTexture2DOES"));

	glDebugMessageControlKHR = (PFNGLDEBUGMESSAGECONTROLKHRPROC)((void*)eglGetProcAddress("glDebugMessageControlKHR"));
	glDebugMessageInsertKHR = (PFNGLDEBUGMESSAGEINSERTKHRPROC)((void*)eglGetProcAddress("glDebugMessageInsertKHR"));
	glDebugMessageCallbackKHR = (PFNGLDEBUGMESSAGECALLBACKKHRPROC)((void*)eglGetProcAddress("glDebugMessageCallbackKHR"));
	glDebugMessageLogKHR = (PFNGLGETDEBUGMESSAGELOGKHRPROC)((void*)eglGetProcAddress("glDebugMessageLogKHR"));
	glGetPointervKHR = (PFNGLGETPOINTERVKHRPROC)((void*)eglGetProcAddress("glGetPointervKHR"));
	glPushDebugGroupKHR = (PFNGLPUSHDEBUGGROUPKHRPROC)((void*)eglGetProcAddress("glPushDebugGroupKHR"));
	glPopDebugGroupKHR = (PFNGLPOPDEBUGGROUPKHRPROC)((void*)eglGetProcAddress("glPopDebugGroupKHR"));
	glObjectLabelKHR = (PFNGLOBJECTLABELKHRPROC)((void*)eglGetProcAddress("glObjectLabelKHR"));
	glGetObjectLabelKHR = (PFNGLGETOBJECTLABELKHRPROC)((void*)eglGetProcAddress("glGetObjectLabelKHR"));
	glObjectPtrLabelKHR = (PFNGLOBJECTPTRLABELKHRPROC)((void*)eglGetProcAddress("glObjectPtrLabelKHR"));
	glGetObjectPtrLabelKHR = (PFNGLGETOBJECTPTRLABELKHRPROC)((void*)eglGetProcAddress("glGetObjectPtrLabelKHR"));
}

FPlatformOpenGLContext* PlatformCreateOpenGLContext(FPlatformOpenGLDevice* Device, void* InWindowHandle)
{
	//Assumes Device is already initialized and context already created.
	return LuminEGL::GetInstance()->GetRenderingContext();
}

void PlatformDestroyOpenGLContext(FPlatformOpenGLDevice* Device, FPlatformOpenGLContext* Context)
{
	delete Device; //created here, destroyed here, but held by RHI.
}

FRHITexture* PlatformCreateBuiltinBackBuffer(FOpenGLDynamicRHI* OpenGLRHI, uint32 SizeX, uint32 SizeY)
{
	ETextureCreateFlags Flags = TexCreate_RenderTargetable;
	FOpenGLTexture2D* Texture2D = new FOpenGLTexture2D(OpenGLRHI, LuminEGL::GetInstance()->GetOnScreenColorRenderBuffer(), GL_RENDERBUFFER, GL_COLOR_ATTACHMENT0, SizeX, SizeY, 0, 1, 1, 1, 0, PF_B8G8R8A8, false, false, Flags, FClearValueBinding::Transparent);
	OpenGLTextureAllocated(Texture2D, Flags);

	return Texture2D;
}

void PlatformResizeGLContext(FPlatformOpenGLDevice* Device, FPlatformOpenGLContext* Context, uint32 SizeX, uint32 SizeY, bool bFullscreen, bool bWasFullscreen, GLenum BackBufferTarget, GLuint BackBufferResource)
{
	check(Context);

	glViewport(0, 0, SizeX, SizeY);
	VERIFY_GL(glViewport);
}

void PlatformGetSupportedResolution(uint32 &Width, uint32 &Height)
{
}

bool PlatformGetAvailableResolutions(FScreenResolutionArray& Resolutions, bool bIgnoreRefreshRate)
{
	return true;
}

int32 PlatformGlGetError()
{
	return glGetError();
}

// =============================================================

void PlatformReleaseOcclusionQuery(GLuint Query, uint64 QueryContext)
{
}

void FPlatformOpenGLDevice::SetCurrentSharedContext()
{
	LuminEGL::GetInstance()->SetCurrentSharedContext();
}

void PlatformDestroyOpenGLDevice(FPlatformOpenGLDevice* Device)
{
	delete Device;
}

void FPlatformOpenGLDevice::SetCurrentRenderingContext()
{
	LuminEGL::GetInstance()->SetCurrentRenderingContext();
}

void PlatformLabelObjects()
{
	// @todo: Check that there is a valid id (non-zero) as LabelObject will fail otherwise
	GLuint RenderBuffer = LuminEGL::GetInstance()->GetOnScreenColorRenderBuffer();
	if (RenderBuffer != 0)
	{
		FOpenGL::LabelObject(GL_RENDERBUFFER, RenderBuffer, "OnScreenColorRB");
	}

	GLuint FrameBuffer = LuminEGL::GetInstance()->GetResolveFrameBuffer();
	if (FrameBuffer != 0)
	{
		FOpenGL::LabelObject(GL_FRAMEBUFFER, FrameBuffer, "ResolveFB");
	}
}

//--------------------------------

void PlatformGetNewRenderQuery(GLuint* OutQuery, uint64* OutQueryContext)
{
	GLuint NewQuery = 0;
	FOpenGL::GenQueries(1, &NewQuery);
	*OutQuery = NewQuery;
	*OutQueryContext = 0;
}

void PlatformReleaseRenderQuery(GLuint Query, uint64 QueryContext)
{
	FOpenGL::DeleteQueries(1, &Query);
}

FLuminOpenGL::EImageExternalType FLuminOpenGL::ImageExternalType = FLuminOpenGL::EImageExternalType::None;
bool FLuminOpenGL::bSupportsImageExternal = false;

void FLuminOpenGL::ProcessExtensions(const FString& ExtensionsString)
{	
	FString VersionString = FString(ANSI_TO_TCHAR((const ANSICHAR*)glGetString(GL_VERSION)));

	FOpenGLES::CurrentFeatureLevelSupport = VersionString.Contains(TEXT("OpenGL ES 3.2")) ? FOpenGLES::EFeatureLevelSupport::ES32 : FOpenGLES::EFeatureLevelSupport::ES31;
	
	FOpenGLES::ProcessExtensions(ExtensionsString);

	FString RendererString = FString(ANSI_TO_TCHAR((const ANSICHAR*)glGetString(GL_RENDERER)));

	// Check for external image support for different ES versions
	ImageExternalType = EImageExternalType::None;

	static const auto CVarOverrideExternalTextureSupport = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Lumin.OverrideExternalTextureSupport"));
	const int32 OverrideExternalTextureSupport = CVarOverrideExternalTextureSupport->GetValueOnAnyThread();
	switch (OverrideExternalTextureSupport)
	{
		case 1:
			ImageExternalType = EImageExternalType::None;
			break;

		case 2:
			ImageExternalType = EImageExternalType::ImageExternal100;
			break;

		case 3:
			ImageExternalType = EImageExternalType::ImageExternal300;
			break;	

		case 4:
			ImageExternalType = EImageExternalType::ImageExternalESSL300;
			break;

		case 0:
		default:
			// auto-detect by extensions (default)
			bool bHasImageExternal = ExtensionsString.Contains(TEXT("GL_OES_EGL_image_external ")) || ExtensionsString.EndsWith(TEXT("GL_OES_EGL_image_external"));
			bool bHasImageExternalESSL3 = ExtensionsString.Contains(TEXT("OES_EGL_image_external_essl3"));
			if (bHasImageExternal || bHasImageExternalESSL3)
			{
				ImageExternalType = EImageExternalType::ImageExternal100;
				if (bHasImageExternalESSL3)
				{
					ImageExternalType = EImageExternalType::ImageExternalESSL300;
				}
			}
			break;
	}
	switch (ImageExternalType)
	{
		case EImageExternalType::None:
			UE_LOG(LogRHI, Log, TEXT("Image external disabled"));
			break;

		case EImageExternalType::ImageExternal100:
			UE_LOG(LogRHI, Log, TEXT("Image external enabled: ImageExternal100"));
			break;

		case EImageExternalType::ImageExternal300:
			UE_LOG(LogRHI, Log, TEXT("Image external enabled: ImageExternal300"));
			break;

		case EImageExternalType::ImageExternalESSL300:
			UE_LOG(LogRHI, Log, TEXT("Image external enabled: ImageExternalESSL300"));
			break;

		default:
			ImageExternalType = EImageExternalType::None;
			UE_LOG(LogRHI, Log, TEXT("Image external disabled; unknown type"));
	}
	bSupportsImageExternal = ImageExternalType != EImageExternalType::None;

	// No longer defined in GLES3.0 - maps to glMapBufferRange and glUnmapBuffer; these entrypoints are not used so commenting out for now.
	//glMapBufferOESa = (PFNGLMAPBUFFEROESPROC)((void*)eglGetProcAddress("glMapBufferOES"));
	//glUnmapBufferOESa = (PFNGLUNMAPBUFFEROESPROC)((void*)eglGetProcAddress("glUnmapBufferOES"));
}

void FAndroidAppEntry::PlatformInit()
{
	LuminEGL::GetInstance()->Init(LuminEGL::AV_OpenGLES, 2, 0, false);
}


void FAndroidAppEntry::ReleaseEGL()
{
	// @todo Lumin: If we switch to Vk, we may need this when we build for both
}

FString FAndroidMisc::GetGPUFamily()
{
	return FString((const ANSICHAR*)glGetString(GL_RENDERER));
}

FString FAndroidMisc::GetGLVersion()
{
	return FString((const ANSICHAR*)glGetString(GL_VERSION));
}

bool FAndroidMisc::SupportsFloatingPointRenderTargets()
{
	// @todo Lumin: True?
	return true;
}

bool FAndroidMisc::SupportsShaderFramebufferFetch()
{
	// @todo Lumin: True?
	return true;
}

#endif


