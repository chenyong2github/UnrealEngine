// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	OpenGLDevice.cpp: OpenGL device RHI implementation.
=============================================================================*/

#include "OpenGLDrvPrivate.h"

#include "HardwareInfo.h"
#include "ShaderCache.h"

#ifndef GL_STEREO
#define GL_STEREO			0x0C33
#endif

extern GLint GMaxOpenGLColorSamples;
extern GLint GMaxOpenGLDepthSamples;
extern GLint GMaxOpenGLIntegerSamples;
extern GLint GMaxOpenGLTextureFilterAnisotropic;

/** OpenGL texture format table. */
FOpenGLTextureFormat GOpenGLTextureFormats[PF_MAX];

/** Device is necessary for vertex buffers, so they can reach the global device on destruction, and tell it to reset vertex array caches */
static FOpenGLDynamicRHI* PrivateOpenGLDevicePtr = NULL;

/** true if we're not using UBOs. (ES2) */
bool GUseEmulatedUniformBuffers;

void OnQueryCreation( FOpenGLRenderQuery* Query )
{
	check(PrivateOpenGLDevicePtr);
	PrivateOpenGLDevicePtr->RegisterQuery( Query );
}

void OnQueryDeletion( FOpenGLRenderQuery* Query )
{
	if(PrivateOpenGLDevicePtr)
	{
		PrivateOpenGLDevicePtr->UnregisterQuery( Query );
	}
}

void OnQueryInvalidation( void )
{
	if(PrivateOpenGLDevicePtr)
	{
		PrivateOpenGLDevicePtr->InvalidateQueries();
	}
}

void OnProgramDeletion( GLint ProgramResource )
{
	check(PrivateOpenGLDevicePtr);
	PrivateOpenGLDevicePtr->OnProgramDeletion( ProgramResource );
}

void OnVertexBufferDeletion( GLuint VertexBufferResource )
{
	check(PrivateOpenGLDevicePtr);
	PrivateOpenGLDevicePtr->OnVertexBufferDeletion( VertexBufferResource );
}

void OnIndexBufferDeletion( GLuint IndexBufferResource )
{
	check(PrivateOpenGLDevicePtr);
	PrivateOpenGLDevicePtr->OnIndexBufferDeletion( IndexBufferResource );
}

void OnPixelBufferDeletion( GLuint PixelBufferResource )
{
	check(PrivateOpenGLDevicePtr);
	PrivateOpenGLDevicePtr->OnPixelBufferDeletion( PixelBufferResource );
}

void OnUniformBufferDeletion( GLuint UniformBufferResource, uint32 AllocatedSize, bool bStreamDraw, uint32 , uint8*  )
{
	check(PrivateOpenGLDevicePtr);
	PrivateOpenGLDevicePtr->OnUniformBufferDeletion( UniformBufferResource, AllocatedSize, bStreamDraw );
}

void CachedBindArrayBuffer( GLuint Buffer )
{
	check(PrivateOpenGLDevicePtr);
	PrivateOpenGLDevicePtr->CachedBindArrayBuffer(PrivateOpenGLDevicePtr->GetContextStateForCurrentContext(),Buffer);
}

void CachedBindElementArrayBuffer( GLuint Buffer )
{
	check(PrivateOpenGLDevicePtr);
	PrivateOpenGLDevicePtr->CachedBindElementArrayBuffer(PrivateOpenGLDevicePtr->GetContextStateForCurrentContext(),Buffer);
}

void CachedBindPixelUnpackBuffer( GLuint Buffer )
{
	check(PrivateOpenGLDevicePtr);
	if ( FOpenGL::SupportsPixelBuffers() )
	{
		PrivateOpenGLDevicePtr->CachedBindPixelUnpackBuffer(PrivateOpenGLDevicePtr->GetContextStateForCurrentContext(),Buffer);
	}
}

void CachedBindUniformBuffer( GLuint Buffer )
{
	check(PrivateOpenGLDevicePtr);
	if (FOpenGL::SupportsUniformBuffers())
	{
		PrivateOpenGLDevicePtr->CachedBindUniformBuffer(PrivateOpenGLDevicePtr->GetContextStateForCurrentContext(),Buffer);
	}
}

bool IsUniformBufferBound( GLuint Buffer )
{
	check(PrivateOpenGLDevicePtr);
	return PrivateOpenGLDevicePtr->IsUniformBufferBound(PrivateOpenGLDevicePtr->GetContextStateForCurrentContext(),Buffer);
}

extern void BeginFrame_UniformBufferPoolCleanup();
extern void BeginFrame_VertexBufferCleanup();

FOpenGLContextState& FOpenGLDynamicRHI::GetContextStateForCurrentContext()
{
	int32 ContextType = (int32)PlatformOpenGLCurrentContext(PlatformDevice);
	check(ContextType >= 0);
	if (ContextType == CONTEXT_Rendering)
	{
		return RenderingContextState;
	}
	else
	{
		return SharedContextState;
	}
}

void FOpenGLDynamicRHI::RHIBeginFrame()
{
	RHIPrivateBeginFrame();
	BeginFrame_UniformBufferPoolCleanup();
	BeginFrame_VertexBufferCleanup();
	GPUProfilingData.BeginFrame(this);

#if PLATFORM_ANDROID //adding #if since not sure if this is required for any other platform.
	//we need to differential between 0 (backbuffer) and lastcolorRT.
	FOpenGLContextState& ContextState = GetContextStateForCurrentContext();
	ContextState.LastES2ColorRTResource = 0xFFFFFFFF;
	PendingState.DepthStencil = 0 ;
#endif
}

void FOpenGLDynamicRHI::RHIEndFrame()
{
	GPUProfilingData.EndFrame();
}

void FOpenGLDynamicRHI::RHIBeginScene()
{
	// Increment the frame counter. INDEX_NONE is a special value meaning "uninitialized", so if
	// we hit it just wrap around to zero.
	SceneFrameCounter++;
	if (SceneFrameCounter == INDEX_NONE)
	{
		SceneFrameCounter++;
	}

	static auto* ResourceTableCachingCvar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("rhi.ResourceTableCaching"));
	if (ResourceTableCachingCvar == NULL || ResourceTableCachingCvar->GetValueOnAnyThread() == 1)
	{
		ResourceTableFrameCounter = SceneFrameCounter;
	}
}

void FOpenGLDynamicRHI::RHIEndScene()
{
	ResourceTableFrameCounter = INDEX_NONE;
}



bool GDisableOpenGLDebugOutput = false;

// workaround for HTML5. 
#if PLATFORM_HTML5
#undef GL_ARB_debug_output
#undef GL_KHR_debug
#endif 

#if defined(GL_ARB_debug_output) || defined(GL_KHR_debug)
/**
 * Map GL_DEBUG_SOURCE_*_ARB to a human-readable string.
 */
static const TCHAR* GetOpenGLDebugSourceStringARB(GLenum Source)
{
	static const TCHAR* SourceStrings[] =
	{
		TEXT("API"),
		TEXT("System"),
		TEXT("ShaderCompiler"),
		TEXT("ThirdParty"),
		TEXT("Application"),
		TEXT("Other")
	};

	if (Source >= GL_DEBUG_SOURCE_API_ARB && Source <= GL_DEBUG_SOURCE_OTHER_ARB)
	{
		return SourceStrings[Source - GL_DEBUG_SOURCE_API_ARB];
	}
	return TEXT("Unknown");
}

/**
 * Map GL_DEBUG_TYPE_*_ARB to a human-readable string.
 */
static const TCHAR* GetOpenGLDebugTypeStringARB(GLenum Type)
{
	static const TCHAR* TypeStrings[] =
	{
		TEXT("Error"),
		TEXT("Deprecated"),
		TEXT("UndefinedBehavior"),
		TEXT("Portability"),
		TEXT("Performance"),
		TEXT("Other")
	};

	if (Type >= GL_DEBUG_TYPE_ERROR_ARB && Type <= GL_DEBUG_TYPE_OTHER_ARB)
	{
		return TypeStrings[Type - GL_DEBUG_TYPE_ERROR_ARB];
	}
#ifdef GL_KHR_debug	
	{
		static const TCHAR* TypeStrings[] =
		{
			TEXT("Marker"),
			TEXT("PushGroup"),
			TEXT("PopGroup"),
		};

		if (Type >= GL_DEBUG_TYPE_MARKER && Type <= GL_DEBUG_TYPE_POP_GROUP)
		{
			return TypeStrings[Type - GL_DEBUG_TYPE_MARKER];
		}
	}
#endif
	return TEXT("Unknown");
}

/**
 * Map GL_DEBUG_SEVERITY_*_ARB to a human-readable string.
 */
static const TCHAR* GetOpenGLDebugSeverityStringARB(GLenum Severity)
{
	static const TCHAR* SeverityStrings[] =
	{
		TEXT("High"),
		TEXT("Medium"),
		TEXT("Low")
	};

	if (Severity >= GL_DEBUG_SEVERITY_HIGH_ARB && Severity <= GL_DEBUG_SEVERITY_LOW_ARB)
	{
		return SeverityStrings[Severity - GL_DEBUG_SEVERITY_HIGH_ARB];
	}
#ifdef GL_KHR_debug
	if(Severity == GL_DEBUG_SEVERITY_NOTIFICATION)
		return TEXT("Notification");
#endif
	return TEXT("Unknown");
}

/**
 * OpenGL debug message callback. Conforms to GLDEBUGPROCARB.
 */
#if (PLATFORM_ANDROID && !PLATFORM_ANDROIDGL4) || PLATFORM_HTML5
	#ifndef GL_APIENTRY
	#define GL_APIENTRY APIENTRY
	#endif
static void GL_APIENTRY OpenGLDebugMessageCallbackARB(
#else
static void APIENTRY OpenGLDebugMessageCallbackARB(
#endif
	GLenum Source,
	GLenum Type,
	GLuint Id,
	GLenum Severity,
	GLsizei Length,
	const GLchar* Message,
	GLvoid* UserParam)
{
	if (GDisableOpenGLDebugOutput)
		return;
#if !NO_LOGGING
	const TCHAR* SourceStr = GetOpenGLDebugSourceStringARB(Source);
	const TCHAR* TypeStr = GetOpenGLDebugTypeStringARB(Type);
	const TCHAR* SeverityStr = GetOpenGLDebugSeverityStringARB(Severity);

	ELogVerbosity::Type Verbosity = ELogVerbosity::Warning;
	if (Type == GL_DEBUG_TYPE_ERROR_ARB && Severity == GL_DEBUG_SEVERITY_HIGH_ARB)
	{
		Verbosity = ELogVerbosity::Fatal;
	}

	if ((Verbosity & ELogVerbosity::VerbosityMask) <= FLogCategoryLogRHI::CompileTimeVerbosity)
	{
		if (!LogRHI.IsSuppressed(Verbosity))
		{
			FMsg::Logf(__FILE__, __LINE__, LogRHI.GetCategoryName(), Verbosity,
				TEXT("[%s][%s][%s][%u] %s"),
				SourceStr,
				TypeStr,
				SeverityStr,
				Id,
				ANSI_TO_TCHAR(Message)
				);
		}

		// this is a debugging code to catch VIDEO->HOST copying
		if (Id == 131186)
		{
			int A = 5;
		}
	}
#endif
}

#endif // GL_ARB_debug_output

#ifdef GL_AMD_debug_output

/**
 * Map GL_DEBUG_CATEGORY_*_AMD to a human-readable string.
 */
static const TCHAR* GetOpenGLDebugCategoryStringAMD(GLenum Category)
{
	static const TCHAR* CategoryStrings[] =
	{
		TEXT("API"),
		TEXT("System"),
		TEXT("Deprecation"),
		TEXT("UndefinedBehavior"),
		TEXT("Performance"),
		TEXT("ShaderCompiler"),
		TEXT("Application"),
		TEXT("Other")
	};

	if (Category >= GL_DEBUG_CATEGORY_API_ERROR_AMD && Category <= GL_DEBUG_CATEGORY_OTHER_AMD)
	{
		return CategoryStrings[Category - GL_DEBUG_CATEGORY_API_ERROR_AMD];
	}
	return TEXT("Unknown");
}

/**
 * Map GL_DEBUG_SEVERITY_*_AMD to a human-readable string.
 */
static const TCHAR* GetOpenGLDebugSeverityStringAMD(GLenum Severity)
{
	static const TCHAR* SeverityStrings[] =
	{
		TEXT("High"),
		TEXT("Medium"),
		TEXT("Low")
	};

	if (Severity >= GL_DEBUG_SEVERITY_HIGH_AMD && Severity <= GL_DEBUG_SEVERITY_LOW_AMD)
	{
		return SeverityStrings[Severity - GL_DEBUG_SEVERITY_HIGH_AMD];
	}
	return TEXT("Unknown");
}

/**
 * OpenGL debug message callback. Conforms to GLDEBUGPROCAMD.
 */
static void APIENTRY OpenGLDebugMessageCallbackAMD(
	GLuint Id,
	GLenum Category,
	GLenum Severity,
	GLsizei Length,
	const GLchar* Message,
	GLvoid* UserParam)
{
#if !NO_LOGGING
	const TCHAR* CategoryStr = GetOpenGLDebugCategoryStringAMD(Category);
	const TCHAR* SeverityStr = GetOpenGLDebugSeverityStringAMD(Severity);
	
	ELogVerbosity::Type Verbosity = ELogVerbosity::Warning;
	if (Severity == GL_DEBUG_SEVERITY_HIGH_AMD)
	{
		Verbosity = ELogVerbosity::Fatal;
	}

	if ((Verbosity & ELogVerbosity::VerbosityMask) <= FLogCategoryLogRHI::CompileTimeVerbosity)
	{
		if (!LogRHI.IsSuppressed(Verbosity))
		{
			FMsg::Logf(__FILE__, __LINE__, LogRHI.GetCategoryName(), Verbosity,
				TEXT("[%s][%s][%u] %s"),
				CategoryStr,
				SeverityStr,
				Id,
				ANSI_TO_TCHAR(Message)
				);
		}
	}
#endif
}

#endif // GL_AMD_debug_output

#if PLATFORM_WINDOWS
PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT_ProcAddress = NULL;
#endif


static inline void SetupTextureFormat( EPixelFormat Format, const FOpenGLTextureFormat& GLFormat)
{
	GOpenGLTextureFormats[Format] = GLFormat;
	GPixelFormats[Format].Supported = (GLFormat.Format != GL_NONE && (GLFormat.InternalFormat[0] != GL_NONE || GLFormat.InternalFormat[1] != GL_NONE));
}


void InitDebugContext() 
{
	// Set the debug output callback if the driver supports it.
	VERIFY_GL(__FUNCTION__);
	bool bDebugOutputInitialized = false;
#if !ENABLE_VERIFY_GL
	#if defined(GL_ARB_debug_output)
		if (glDebugMessageCallbackARB)
		{
			// Synchronous output can slow things down, but we'll get better callstack if breaking in or crashing in the callback. This is debug only after all.
			glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
			glDebugMessageCallbackARB(GLDEBUGPROCARB(OpenGLDebugMessageCallbackARB), /*UserParam=*/ NULL);
			bDebugOutputInitialized = (glGetError() == GL_NO_ERROR);
		}
	#elif defined(GL_KHR_debug)
		// OpenGLES names the debug functions differently, but they behave the same
		if (glDebugMessageCallbackKHR)
		{
			glDebugMessageCallbackKHR(GLDEBUGPROCKHR(OpenGLDebugMessageCallbackARB), /*UserParam=*/ NULL);
			bDebugOutputInitialized = (glGetError() == GL_NO_ERROR);
		}
	#endif // GL_ARB_debug_output / GL_KHR_debug
	#if defined(GL_AMD_debug_output)
		if (glDebugMessageCallbackAMD && !bDebugOutputInitialized)
		{
			glDebugMessageCallbackAMD(OpenGLDebugMessageCallbackAMD, /*UserParam=*/ NULL);
			bDebugOutputInitialized = (glGetError() == GL_NO_ERROR);
		}
	#endif // GL_AMD_debug_output
#endif // !ENABLE_VERIFY_GL
	if (!bDebugOutputInitialized && !PLATFORM_MAC)
	{
		UE_LOG(LogRHI,Warning,TEXT("OpenGL debug output extension not supported!"));
	}

	// this is to suppress feeding back of the debug markers and groups to the log, since those originate in the app anyways...
#if ENABLE_OPENGL_DEBUG_GROUPS && GL_ARB_debug_output && GL_KHR_debug && !OPENGL_ES31
	if(glDebugMessageControlARB && bDebugOutputInitialized)
	{
		glDebugMessageControlARB(GL_DEBUG_SOURCE_APPLICATION_ARB, GL_DEBUG_TYPE_MARKER, GL_DONT_CARE, 0, NULL, GL_FALSE);
		glDebugMessageControlARB(GL_DEBUG_SOURCE_APPLICATION_ARB, GL_DEBUG_TYPE_PUSH_GROUP, GL_DONT_CARE, 0, NULL, GL_FALSE);
		glDebugMessageControlARB(GL_DEBUG_SOURCE_APPLICATION_ARB, GL_DEBUG_TYPE_POP_GROUP, GL_DONT_CARE, 0, NULL, GL_FALSE);
#ifdef GL_KHR_debug
		glDebugMessageControlARB(GL_DEBUG_SOURCE_API_ARB, GL_DEBUG_TYPE_OTHER_ARB, GL_DEBUG_SEVERITY_NOTIFICATION, 0, NULL, GL_FALSE);
#endif
		UE_LOG(LogRHI,Verbose,TEXT("disabling reporting back of debug groups and markers to the OpenGL debug output callback"));
	}
#elif ENABLE_OPENGL_DEBUG_GROUPS && !defined(GL_ARB_debug_output) && GL_KHR_debug
	if(glDebugMessageControlKHR)
	{
		glDebugMessageControlKHR(GL_DEBUG_SOURCE_APPLICATION_KHR, GL_DEBUG_TYPE_MARKER, GL_DONT_CARE, 0, NULL, GL_FALSE);
		glDebugMessageControlKHR(GL_DEBUG_SOURCE_APPLICATION_KHR, GL_DEBUG_TYPE_PUSH_GROUP, GL_DONT_CARE, 0, NULL, GL_FALSE);
		glDebugMessageControlKHR(GL_DEBUG_SOURCE_APPLICATION_KHR, GL_DEBUG_TYPE_POP_GROUP, GL_DONT_CARE, 0, NULL, GL_FALSE);
		glDebugMessageControlKHR(GL_DEBUG_SOURCE_API_KHR, GL_DEBUG_TYPE_OTHER_KHR, GL_DEBUG_SEVERITY_NOTIFICATION, 0, NULL, GL_FALSE);
		UE_LOG(LogRHI,Verbose,TEXT("disabling reporting back of debug groups and markers to the OpenGL debug output callback"));
	}
#endif
}

/**
 * Initialize RHI capabilities for the current OpenGL context.
 */
static void InitRHICapabilitiesForGL()
{
	VERIFY_GL_SCOPE();

	GTexturePoolSize = 0;
	GPoolSizeVRAMPercentage = 0;
#if PLATFORM_WINDOWS || PLATFORM_LINUX
	GConfig->GetInt( TEXT( "TextureStreaming" ), TEXT( "PoolSizeVRAMPercentage" ), GPoolSizeVRAMPercentage, GEngineIni );	
#endif

	// GL vendor and version information.
#if !defined(__GNUC__) && !defined(__clang__)
	#define LOG_GL_STRING(StringEnum) UE_LOG(LogRHI, Log, TEXT("  ") ## TEXT(#StringEnum) ## TEXT(": %s"), ANSI_TO_TCHAR((const ANSICHAR*)glGetString(StringEnum)))
#else
	#define LOG_GL_STRING(StringEnum) UE_LOG(LogRHI, Log, TEXT("  " #StringEnum ": %s"), ANSI_TO_TCHAR((const ANSICHAR*)glGetString(StringEnum)))
#endif
	UE_LOG(LogRHI, Log, TEXT("Initializing OpenGL RHI"));
	LOG_GL_STRING(GL_VENDOR);
	LOG_GL_STRING(GL_RENDERER);
	LOG_GL_STRING(GL_VERSION);
	LOG_GL_STRING(GL_SHADING_LANGUAGE_VERSION);
	#undef LOG_GL_STRING

	GRHIAdapterName = FOpenGL::GetAdapterName();

	// Log all supported extensions.
#if PLATFORM_WINDOWS
	bool bWindowsSwapControlExtensionPresent = false;
#endif
	{
		extern void GetExtensionsString( FString& ExtensionsString);
		FString ExtensionsString;

		GetExtensionsString(ExtensionsString);

#if PLATFORM_WINDOWS
		if (ExtensionsString.Contains(TEXT("WGL_EXT_swap_control")))
				{
					bWindowsSwapControlExtensionPresent = true;
				}
#endif

		// Log supported GL extensions
		UE_LOG(LogRHI, Log, TEXT("OpenGL Extensions:"));
		TArray<FString> GLExtensionArray;
		ExtensionsString.ParseIntoArray(GLExtensionArray, TEXT(" "), true);
		for (int ExtIndex = 0; ExtIndex < GLExtensionArray.Num(); ExtIndex++)
		{
			UE_LOG(LogRHI, Log, TEXT("  %s"), *GLExtensionArray[ExtIndex]);
		}

		FOpenGL::ProcessExtensions(ExtensionsString);
	}

#if PLATFORM_WINDOWS
	#pragma warning(push)
	#pragma warning(disable:4191)
	if (!bWindowsSwapControlExtensionPresent)
	{
		// Disable warning C4191: 'type cast' : unsafe conversion from 'PROC' to 'XXX' while getting GL entry points.
		PFNWGLGETEXTENSIONSSTRINGEXTPROC wglGetExtensionsStringEXT_ProcAddress =
			(PFNWGLGETEXTENSIONSSTRINGEXTPROC) wglGetProcAddress("wglGetExtensionsStringEXT");

		if (strstr(wglGetExtensionsStringEXT_ProcAddress(), "WGL_EXT_swap_control") != NULL)
		{
			bWindowsSwapControlExtensionPresent = true;
		}
	}

	if (bWindowsSwapControlExtensionPresent)
	{
		wglSwapIntervalEXT_ProcAddress = (PFNWGLSWAPINTERVALEXTPROC) wglGetProcAddress("wglSwapIntervalEXT");
	}
	#pragma warning(pop)
#endif

	// Set debug flag if context was setup with debugging
	FOpenGL::InitDebugContext();

	// Log and get various limits.
#if !defined(__GNUC__) && !defined(__clang__)
#define LOG_AND_GET_GL_INT_TEMP(IntEnum,Default) GLint Value_##IntEnum = Default; if (IntEnum) {glGetIntegerv(IntEnum, &Value_##IntEnum); glGetError();} else {Value_##IntEnum = Default;} UE_LOG(LogRHI, Log, TEXT("  ") ## TEXT(#IntEnum) ## TEXT(": %d"), Value_##IntEnum)
#else
#define LOG_AND_GET_GL_INT_TEMP(IntEnum,Default) GLint Value_##IntEnum = Default; if (IntEnum) {glGetIntegerv(IntEnum, &Value_##IntEnum); glGetError();} else {Value_##IntEnum = Default;} UE_LOG(LogRHI, Log, TEXT("  " #IntEnum ": %d"), Value_##IntEnum)
#endif
	LOG_AND_GET_GL_INT_TEMP(GL_MAX_TEXTURE_SIZE, 0);
	LOG_AND_GET_GL_INT_TEMP(GL_MAX_CUBE_MAP_TEXTURE_SIZE, 0);
#if GL_MAX_ARRAY_TEXTURE_LAYERS
	LOG_AND_GET_GL_INT_TEMP(GL_MAX_ARRAY_TEXTURE_LAYERS, 0);
#endif
#if GL_MAX_3D_TEXTURE_SIZE
	LOG_AND_GET_GL_INT_TEMP(GL_MAX_3D_TEXTURE_SIZE, 0);
#endif 
	LOG_AND_GET_GL_INT_TEMP(GL_MAX_RENDERBUFFER_SIZE, 0);
	LOG_AND_GET_GL_INT_TEMP(GL_MAX_TEXTURE_IMAGE_UNITS, 0);
	if (FOpenGL::SupportsDrawBuffers())
	{
		LOG_AND_GET_GL_INT_TEMP(GL_MAX_DRAW_BUFFERS, 1);
	}
	LOG_AND_GET_GL_INT_TEMP(GL_MAX_COLOR_ATTACHMENTS, 1);
	LOG_AND_GET_GL_INT_TEMP(GL_MAX_SAMPLES, 1);
	LOG_AND_GET_GL_INT_TEMP(GL_MAX_COLOR_TEXTURE_SAMPLES, 1);
	LOG_AND_GET_GL_INT_TEMP(GL_MAX_DEPTH_TEXTURE_SAMPLES, 1);
	LOG_AND_GET_GL_INT_TEMP(GL_MAX_INTEGER_SAMPLES, 1);
	LOG_AND_GET_GL_INT_TEMP(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, 0);
	LOG_AND_GET_GL_INT_TEMP(GL_MAX_VERTEX_ATTRIBS, 0);

	if (FParse::Param(FCommandLine::Get(), TEXT("quad_buffer_stereo")))
	{
		GLboolean Result = GL_FALSE;
		glGetBooleanv(GL_STEREO, &Result);
		// Skip any errors if any were generated
		glGetError();
		GSupportsQuadBufferStereo = (Result == GL_TRUE);
	}

	if( FOpenGL::SupportsTextureFilterAnisotropic())
	{
		LOG_AND_GET_GL_INT_TEMP(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, 0);
		GMaxOpenGLTextureFilterAnisotropic = Value_GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT;
	}
#undef LOG_AND_GET_GL_INT_TEMP

	GMaxOpenGLColorSamples = Value_GL_MAX_COLOR_TEXTURE_SAMPLES;
	GMaxOpenGLDepthSamples = Value_GL_MAX_DEPTH_TEXTURE_SAMPLES;
	GMaxOpenGLIntegerSamples = Value_GL_MAX_INTEGER_SAMPLES;

	// Verify some assumptions.
	check(Value_GL_MAX_COLOR_ATTACHMENTS >= MaxSimultaneousRenderTargets || !FOpenGL::SupportsMultipleRenderTargets());

	// We don't check for compressed formats right now because vendors have not
	// done a great job reporting what is actually supported:
	//   OSX/NVIDIA doesn't claim to support SRGB DXT formats even though they work correctly.
	//   Windows/AMD sometimes claim to support no compressed formats even though they all work correctly.
#if 0
	// Check compressed texture formats.
	bool bSupportsCompressedTextures = true;
	{
		FString CompressedFormatsString = TEXT("  GL_COMPRESSED_TEXTURE_FORMATS:");
		TArray<GLint> CompressedFormats;
		GLint NumCompressedFormats = 0;
		glGetIntegerv(GL_NUM_COMPRESSED_TEXTURE_FORMATS, &NumCompressedFormats);
		CompressedFormats.Empty(NumCompressedFormats);
		CompressedFormats.AddZeroed(NumCompressedFormats);
		glGetIntegerv(GL_COMPRESSED_TEXTURE_FORMATS, CompressedFormats.GetTypedData());

		#define CHECK_COMPRESSED_FORMAT(GLName) if (CompressedFormats.Contains(GLName)) { CompressedFormatsString += TEXT(" ") TEXT(#GLName); } else { bSupportsCompressedTextures = false; }
		CHECK_COMPRESSED_FORMAT(GL_COMPRESSED_RGB_S3TC_DXT1_EXT);
		CHECK_COMPRESSED_FORMAT(GL_COMPRESSED_SRGB_S3TC_DXT1_EXT);
		CHECK_COMPRESSED_FORMAT(GL_COMPRESSED_RGBA_S3TC_DXT3_EXT);
		CHECK_COMPRESSED_FORMAT(GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT);
		CHECK_COMPRESSED_FORMAT(GL_COMPRESSED_RGBA_S3TC_DXT5_EXT);
		CHECK_COMPRESSED_FORMAT(GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT);
		//CHECK_COMPRESSED_FORMAT(GL_COMPRESSED_RG_RGTC2);
		#undef CHECK_COMPRESSED_FORMAT

		// ATI does not report that it supports RGTC2, but the 3.2 core profile mandates it.
		// For now assume it is supported and bring it up with ATI if it really isn't(?!)
		CompressedFormatsString += TEXT(" GL_COMPRESSED_RG_RGTC2");

		UE_LOG(LogRHI, Log, *CompressedFormatsString);
	}
	check(bSupportsCompressedTextures);
#endif

	// Set capabilities.
	const GLint MajorVersion = FOpenGL::GetMajorVersion();
	const GLint MinorVersion = FOpenGL::GetMinorVersion();

	// Shader platform & RHI feature level
	GMaxRHIFeatureLevel = FOpenGL::GetFeatureLevel();
	GMaxRHIShaderPlatform = FOpenGL::GetShaderPlatform();

	// Emulate uniform buffers on ES2, unless we're on a desktop platform emulating ES2.
	GUseEmulatedUniformBuffers = IsES2Platform(GMaxRHIShaderPlatform) && !IsPCPlatform(GMaxRHIShaderPlatform);
	if (!GUseEmulatedUniformBuffers && IsPCPlatform(GMaxRHIShaderPlatform))
	{
		static auto* CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("OpenGL.UseEmulatedUBs"));
		GUseEmulatedUniformBuffers = CVar && CVar->GetValueOnAnyThread() != 0;
	}

	FString FeatureLevelName;
	GetFeatureLevelName(GMaxRHIFeatureLevel, FeatureLevelName);
	FString ShaderPlatformName = LegacyShaderPlatformToShaderFormat(GMaxRHIShaderPlatform).ToString();

	UE_LOG(LogRHI, Log, TEXT("OpenGL MajorVersion = %d, MinorVersion = %d, ShaderPlatform = %s, FeatureLevel = %s"), MajorVersion, MinorVersion, *ShaderPlatformName, *FeatureLevelName);
#if PLATFORM_ANDROIDGL4
	UE_LOG(LogRHI, Log, TEXT("PLATFORM_ANDROIDGL4"));
#elif PLATFORM_ANDROIDES31
	UE_LOG(LogRHI, Log, TEXT("PLATFORM_ANDROIDES31"));
#elif PLATFORM_ANDROID
	UE_LOG(LogRHI, Log, TEXT("PLATFORM_ANDROID"));
#endif

	GMaxTextureMipCount = FMath::CeilLogTwo(Value_GL_MAX_TEXTURE_SIZE) + 1;
	GMaxTextureMipCount = FMath::Min<int32>(MAX_TEXTURE_MIP_COUNT, GMaxTextureMipCount);
	GMaxTextureDimensions = Value_GL_MAX_TEXTURE_SIZE;
	GMaxCubeTextureDimensions = Value_GL_MAX_CUBE_MAP_TEXTURE_SIZE;
#if GL_MAX_ARRAY_TEXTURE_LAYERS
	GMaxTextureArrayLayers = Value_GL_MAX_ARRAY_TEXTURE_LAYERS;
#endif

	GSupportsVolumeTextureRendering = FOpenGL::SupportsVolumeTextureRendering();
	GSupportsRenderDepthTargetableShaderResources = true;
	GSupportsRenderTargetFormat_PF_G8 = true;
	GSupportsSeparateRenderTargetBlendState = FOpenGL::SupportsSeparateAlphaBlend();
	GSupportsDepthBoundsTest = FOpenGL::SupportsDepthBoundsTest();

	GSupportsRenderTargetFormat_PF_FloatRGBA = FOpenGL::SupportsColorBufferHalfFloat();

	GSupportsShaderFramebufferFetch = FOpenGL::SupportsShaderFramebufferFetch();
	GMaxShadowDepthBufferSizeX = FMath::Min<int32>(Value_GL_MAX_RENDERBUFFER_SIZE, 4096); // Limit to the D3D11 max.
	GMaxShadowDepthBufferSizeY = FMath::Min<int32>(Value_GL_MAX_RENDERBUFFER_SIZE, 4096);
	GHardwareHiddenSurfaceRemoval = FOpenGL::HasHardwareHiddenSurfaceRemoval();

	GShaderPlatformForFeatureLevel[ERHIFeatureLevel::ES2] = (GMaxRHIFeatureLevel == ERHIFeatureLevel::ES2) ? GMaxRHIShaderPlatform : SP_OPENGL_PCES2;
	GShaderPlatformForFeatureLevel[ERHIFeatureLevel::ES3_1] = (GMaxRHIFeatureLevel == ERHIFeatureLevel::ES3_1) ? GMaxRHIShaderPlatform : SP_OPENGL_PCES3_1;
	GShaderPlatformForFeatureLevel[ERHIFeatureLevel::SM4] = PLATFORM_MAC ? SP_OPENGL_SM4_MAC : SP_OPENGL_SM4;
	GShaderPlatformForFeatureLevel[ERHIFeatureLevel::SM5] = OPENGL_ES31 ? SP_OPENGL_ES31_EXT : SP_OPENGL_SM5;

	// Set to same values as in DX11, as for the time being clip space adjustment are done entirely
	// in HLSLCC-generated shader code and OpenGLDrv.
	GMinClipZ = 0.0f;
	GProjectionSignY = 1.0f;

	// Disable texture streaming on ES2 unless we have the GL_APPLE_copy_texture_levels extension
	if (GMaxRHIFeatureLevel == ERHIFeatureLevel::ES2 && !FOpenGL::SupportsCopyTextureLevels())
	{
		GRHISupportsTextureStreaming = false;
	}
	else
	{
		GRHISupportsTextureStreaming = true;
	}

	GVertexElementTypeSupport.SetSupported(VET_Half2, FOpenGL::SupportsVertexHalfFloat());
	GVertexElementTypeSupport.SetSupported(VET_Half4, FOpenGL::SupportsVertexHalfFloat());

	for (int32 PF = 0; PF < PF_MAX; ++PF)
	{
		SetupTextureFormat(EPixelFormat(PF), FOpenGLTextureFormat());
	}

	GLenum DepthFormat = FOpenGL::GetDepthFormat();
	GLenum ShadowDepthFormat = FOpenGL::GetShadowDepthFormat();

	// Initialize the platform pixel format map.					InternalFormat				InternalFormatSRGB		Format				Type							bCompressed		bBGRA
	SetupTextureFormat( PF_Unknown,				FOpenGLTextureFormat( ));
	SetupTextureFormat( PF_A32B32G32R32F,		FOpenGLTextureFormat( GL_RGBA32F,				GL_NONE,				GL_RGBA,			GL_FLOAT,						false,			false));
	SetupTextureFormat( PF_UYVY,				FOpenGLTextureFormat( ));
	//@todo: ES2 requires GL_OES_depth_texture extension to support depth textures of any kind.
	SetupTextureFormat( PF_ShadowDepth,			FOpenGLTextureFormat( ShadowDepthFormat,		GL_NONE,				GL_DEPTH_COMPONENT,	GL_UNSIGNED_INT,				false,			false));
	SetupTextureFormat( PF_D24,					FOpenGLTextureFormat( DepthFormat,				GL_NONE,				GL_DEPTH_COMPONENT,	GL_UNSIGNED_INT,				false,			false));
	SetupTextureFormat( PF_A16B16G16R16,		FOpenGLTextureFormat( GL_RGBA16,				GL_RGBA16,				GL_RGBA,			GL_UNSIGNED_SHORT,				false,			false));
	SetupTextureFormat( PF_A1,					FOpenGLTextureFormat( ));
	SetupTextureFormat( PF_R16G16B16A16_UINT,	FOpenGLTextureFormat( GL_RGBA16UI,				GL_NONE,				GL_RGBA_INTEGER,	GL_UNSIGNED_SHORT,				false,			false));
	SetupTextureFormat( PF_R16G16B16A16_SINT,	FOpenGLTextureFormat( GL_RGBA16I,				GL_NONE,				GL_RGBA_INTEGER,	GL_SHORT,						false,			false));
	SetupTextureFormat( PF_R5G6B5_UNORM,		FOpenGLTextureFormat( ));
#if PLATFORM_DESKTOP || PLATFORM_ANDROIDGL4 || PLATFORM_ANDROIDES31
	if (PLATFORM_DESKTOP != 0 || PLATFORM_ANDROIDGL4 != 0 || FOpenGL::GetFeatureLevel() >= ERHIFeatureLevel::SM4)
	{
		// Not supported for rendering:
		SetupTextureFormat( PF_G16,				FOpenGLTextureFormat( GL_R16,					GL_R16,					GL_RED,			GL_UNSIGNED_SHORT,					false,	false));
		SetupTextureFormat( PF_R32_FLOAT,		FOpenGLTextureFormat( GL_R32F,					GL_R32F,				GL_RED,			GL_FLOAT,							false,	false));
		SetupTextureFormat( PF_G16R16F,			FOpenGLTextureFormat( GL_RG16F,					GL_RG16F,				GL_RG,			GL_HALF_FLOAT,						false,	false));
		SetupTextureFormat( PF_G16R16F_FILTER,	FOpenGLTextureFormat( GL_RG16F,					GL_RG16F,				GL_RG,			GL_HALF_FLOAT,						false,	false));
		SetupTextureFormat( PF_G32R32F,			FOpenGLTextureFormat( GL_RG32F,					GL_RG32F,				GL_RG,			GL_FLOAT,							false,	false));
		SetupTextureFormat( PF_A2B10G10R10,		FOpenGLTextureFormat( GL_RGB10_A2,				GL_RGB10_A2,			GL_RGBA,		GL_UNSIGNED_INT_2_10_10_10_REV,		false,	false));
		SetupTextureFormat( PF_R16F,			FOpenGLTextureFormat( GL_R16F,					GL_R16F,				GL_RED,			GL_HALF_FLOAT,						false,	false));
		SetupTextureFormat( PF_R16F_FILTER,		FOpenGLTextureFormat( GL_R16F,					GL_R16F,				GL_RED,			GL_HALF_FLOAT,						false,	false));
		if (FOpenGL::SupportsR11G11B10F())
		{
			// Note: Also needs to include support for compute shaders to be defined here (e.g. glBindImageTexture)
			SetupTextureFormat(PF_FloatRGB, FOpenGLTextureFormat(GL_R11F_G11F_B10F, GL_R11F_G11F_B10F, GL_RGB, GL_UNSIGNED_INT_10F_11F_11F_REV, false, false));
			SetupTextureFormat(PF_FloatR11G11B10, FOpenGLTextureFormat(GL_RGBA16F, GL_RGBA16F, GL_RGB, GL_HALF_FLOAT, false, false));
		}
		else
		{
			SetupTextureFormat(PF_FloatRGB, FOpenGLTextureFormat(GL_RGBA16F, GL_RGBA16F, GL_RGB, GL_HALF_FLOAT, false, false));
			SetupTextureFormat(PF_FloatR11G11B10, FOpenGLTextureFormat(GL_R11F_G11F_B10F, GL_R11F_G11F_B10F, GL_RGB, GL_UNSIGNED_INT_10F_11F_11F_REV, false, false));
		}
		SetupTextureFormat(PF_V8U8, FOpenGLTextureFormat(GL_RG8_SNORM, GL_NONE, GL_RG, GL_BYTE, false, false));
		SetupTextureFormat( PF_R8G8,			FOpenGLTextureFormat( GL_RG8,					GL_NONE,				GL_RG,			GL_UNSIGNED_BYTE,					false,	false));
		SetupTextureFormat( PF_BC5,				FOpenGLTextureFormat( GL_COMPRESSED_RG_RGTC2,	GL_COMPRESSED_RG_RGTC2,	GL_RG,			GL_UNSIGNED_BYTE,					true,	false));
		SetupTextureFormat( PF_BC4,				FOpenGLTextureFormat( GL_COMPRESSED_RED_RGTC1,	GL_COMPRESSED_RED_RGTC1,GL_RED,			GL_UNSIGNED_BYTE,					true,	false));
		SetupTextureFormat( PF_A8,				FOpenGLTextureFormat( GL_R8,					GL_NONE,				GL_RED,			GL_UNSIGNED_BYTE,					false,	false));
		SetupTextureFormat( PF_R32_UINT,		FOpenGLTextureFormat( GL_R32UI,					GL_NONE,				GL_RED_INTEGER,	GL_UNSIGNED_INT,					false,	false));
		SetupTextureFormat( PF_R32_SINT,		FOpenGLTextureFormat( GL_R32I,					GL_NONE,				GL_RED_INTEGER,	GL_INT,								false,	false));
		SetupTextureFormat( PF_R16_UINT,		FOpenGLTextureFormat( GL_R16UI,					GL_NONE,				GL_RED_INTEGER,	GL_UNSIGNED_SHORT,					false,	false));
		SetupTextureFormat( PF_R16_SINT,		FOpenGLTextureFormat( GL_R16I,					GL_NONE,				GL_RED_INTEGER,	GL_SHORT,							false,	false));
		SetupTextureFormat( PF_FloatRGBA,		FOpenGLTextureFormat( GL_RGBA16F,				GL_RGBA16F,				GL_RGBA,		GL_HALF_FLOAT,						false,	false));
		if (FOpenGL::GetShaderPlatform() == EShaderPlatform::SP_OPENGL_ES31_EXT)
		{
			SetupTextureFormat(PF_G8, FOpenGLTextureFormat(GL_R8, GL_R8, GL_RED, GL_UNSIGNED_BYTE, false, false));
			SetupTextureFormat(PF_B8G8R8A8, FOpenGLTextureFormat(GL_RGBA8, GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, false, true));
			SetupTextureFormat(PF_R8G8B8A8, FOpenGLTextureFormat(GL_RGBA8, GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, false, false));
			if (FOpenGL::SupportsRG16UI())
			{
				// The user should check for support for PF_G16R16 and implement a fallback if it's not supported!
				SetupTextureFormat(PF_G16R16, FOpenGLTextureFormat(GL_RG16, GL_RG16, GL_RG, GL_UNSIGNED_SHORT, false, false));
			}
		}
		else
		{
			SetupTextureFormat(PF_G8, FOpenGLTextureFormat(GL_R8, GL_SRGB8, GL_RED, GL_UNSIGNED_BYTE, false, false));
			SetupTextureFormat(PF_B8G8R8A8, FOpenGLTextureFormat(GL_RGBA8, GL_SRGB8_ALPHA8, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, false, false));
			SetupTextureFormat(PF_R8G8B8A8, FOpenGLTextureFormat(GL_RGBA8, GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, false, false));
			SetupTextureFormat(PF_G16R16, FOpenGLTextureFormat(GL_RG16, GL_RG16, GL_RG, GL_UNSIGNED_SHORT, false, false));
		}
		if (FOpenGL::SupportsPackedDepthStencil())
		{
			SetupTextureFormat(PF_DepthStencil, FOpenGLTextureFormat(GL_DEPTH24_STENCIL8, GL_NONE, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, false, false));
		}
		else
		{
			// @todo android: This is cheating by not setting a stencil anywhere, need that! And Shield is still rendering black scene
			SetupTextureFormat(PF_DepthStencil, FOpenGLTextureFormat(DepthFormat, GL_NONE, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, false, false));
		}
	}
	else
#endif // PLATFORM_DESKTOP || PLATFORM_ANDROIDGL4 || PLATFORM_ANDROIDES31
	{
#if !PLATFORM_DESKTOP && !PLATFORM_ANDROIDGL4
		// ES2-based cases
		GLuint BGRA8888 = FOpenGL::SupportsBGRA8888() ? GL_BGRA_EXT : GL_RGBA;
		GLuint RGBA8 = FOpenGL::SupportsRGBA8() ? GL_RGBA8_OES : GL_RGBA;

	#if PLATFORM_ANDROID
		SetupTextureFormat(PF_B8G8R8A8, FOpenGLTextureFormat(GL_BGRA, FOpenGL::SupportsSRGB() ? GL_SRGB_ALPHA_EXT : GL_BGRA, BGRA8888, GL_UNSIGNED_BYTE, false, false));
	#else
		SetupTextureFormat(PF_B8G8R8A8, FOpenGLTextureFormat(GL_RGBA, FOpenGL::SupportsSRGB() ? GL_SRGB_ALPHA_EXT : GL_RGBA, GL_BGRA8_EXT, FOpenGL::SupportsSRGB() ? GL_SRGB8_ALPHA8_EXT : GL_BGRA8_EXT, BGRA8888, GL_UNSIGNED_BYTE, false, false));
	#endif
		SetupTextureFormat(PF_R8G8B8A8, FOpenGLTextureFormat(RGBA8, FOpenGL::SupportsSRGB() ? GL_SRGB_ALPHA_EXT : RGBA8, GL_RGBA8, FOpenGL::SupportsSRGB() ? GL_SRGB8_ALPHA8_EXT : GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, false, false));
	#if PLATFORM_IOS
		SetupTextureFormat(PF_G8, FOpenGLTextureFormat(GL_LUMINANCE, GL_LUMINANCE, GL_LUMINANCE8_EXT, GL_LUMINANCE8_EXT, GL_LUMINANCE, GL_UNSIGNED_BYTE, false, false));
		SetupTextureFormat(PF_A8, FOpenGLTextureFormat(GL_ALPHA, GL_ALPHA, GL_ALPHA8_EXT, GL_ALPHA8_EXT, GL_ALPHA, GL_UNSIGNED_BYTE, false, false));
	#else
		SetupTextureFormat(PF_G8, FOpenGLTextureFormat(GL_LUMINANCE, GL_LUMINANCE, GL_LUMINANCE, GL_LUMINANCE, GL_LUMINANCE, GL_UNSIGNED_BYTE, false, false));
		SetupTextureFormat(PF_A8, FOpenGLTextureFormat(GL_ALPHA, GL_ALPHA, GL_ALPHA, GL_ALPHA, GL_ALPHA, GL_UNSIGNED_BYTE, false, false));
	#endif

		if (GSupportsRenderTargetFormat_PF_FloatRGBA && FOpenGL::SupportsTextureHalfFloat())
		{
#if PLATFORM_ANDROID
			SetupTextureFormat(PF_FloatRGBA, FOpenGLTextureFormat(GL_RGBA, GL_RGBA, GL_RGBA16F_EXT, GL_RGBA16F_EXT, GL_RGBA, GL_HALF_FLOAT_OES, false, false));
#else
			SetupTextureFormat(PF_FloatRGBA, FOpenGLTextureFormat(GL_RGBA, GL_RGBA, GL_RGBA, GL_HALF_FLOAT_OES, false, false));
#endif
		}
		else
		{
			SetupTextureFormat( PF_FloatRGBA, FOpenGLTextureFormat(GL_RGBA, GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE, false, false));
		}

		if (FOpenGL::SupportsPackedDepthStencil())
		{
			SetupTextureFormat(PF_DepthStencil, FOpenGLTextureFormat(GL_DEPTH_STENCIL_OES, GL_NONE, GL_DEPTH_STENCIL_OES, GL_UNSIGNED_INT_24_8_OES, false, false));
		}
		else
		{
			// @todo android: This is cheating by not setting a stencil anywhere, need that! And Shield is still rendering black scene
			SetupTextureFormat(PF_DepthStencil, FOpenGLTextureFormat(GL_DEPTH_COMPONENT, GL_NONE, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, false, false));
		}
#endif // !PLATFORM_DESKTOP
	}

	if (FOpenGL::SupportsDXT())
	{
		if ( FOpenGL::SupportsSRGB() )
		{
			SetupTextureFormat( PF_DXT1,	FOpenGLTextureFormat(GL_COMPRESSED_RGBA_S3TC_DXT1_EXT,	GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT,	GL_RGBA,	GL_UNSIGNED_BYTE,	true,	false));
			SetupTextureFormat( PF_DXT3,	FOpenGLTextureFormat(GL_COMPRESSED_RGBA_S3TC_DXT3_EXT,	GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT,	GL_RGBA,	GL_UNSIGNED_BYTE,	true,	false));
			SetupTextureFormat( PF_DXT5,	FOpenGLTextureFormat(GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,	GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT,	GL_RGBA,	GL_UNSIGNED_BYTE,	true,	false));
		}
		else
		{
			// WebGL does not support SRGB versions of DXTn texture formats! Run with SRGB formats disabled.  Will need to make sure
			// sRGB is always emulated if it's needed.
			SetupTextureFormat( PF_DXT1,	FOpenGLTextureFormat(GL_COMPRESSED_RGBA_S3TC_DXT1_EXT,	GL_COMPRESSED_RGBA_S3TC_DXT1_EXT,		GL_RGBA,	GL_UNSIGNED_BYTE,	true,	false));
			SetupTextureFormat( PF_DXT3,	FOpenGLTextureFormat(GL_COMPRESSED_RGBA_S3TC_DXT3_EXT,	GL_COMPRESSED_RGBA_S3TC_DXT3_EXT,		GL_RGBA,	GL_UNSIGNED_BYTE,	true,	false));
			SetupTextureFormat( PF_DXT5,	FOpenGLTextureFormat(GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,	GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,		GL_RGBA,	GL_UNSIGNED_BYTE,	true,	false));
		}
	}
	if ( FOpenGL::SupportsPVRTC() )
	{
		SetupTextureFormat( PF_PVRTC2,		FOpenGLTextureFormat(GL_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG, GL_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG,	GL_RGBA,	GL_UNSIGNED_BYTE,	true,	false));
		SetupTextureFormat( PF_PVRTC4,		FOpenGLTextureFormat(GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG, GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG,	GL_RGBA,	GL_UNSIGNED_BYTE,	true,	false));
	}
	if ( FOpenGL::SupportsATITC() )
	{
		SetupTextureFormat( PF_ATC_RGB,		FOpenGLTextureFormat(GL_ATC_RGB_AMD,					 GL_ATC_RGB_AMD,						GL_RGBA,	GL_UNSIGNED_BYTE,	true,	false));
		SetupTextureFormat( PF_ATC_RGBA_E,	FOpenGLTextureFormat(GL_ATC_RGBA_EXPLICIT_ALPHA_AMD,	 GL_ATC_RGBA_EXPLICIT_ALPHA_AMD,		GL_RGBA,	GL_UNSIGNED_BYTE,	true,	false));
		SetupTextureFormat( PF_ATC_RGBA_I,	FOpenGLTextureFormat(GL_ATC_RGBA_INTERPOLATED_ALPHA_AMD, GL_ATC_RGBA_INTERPOLATED_ALPHA_AMD,	GL_RGBA,	GL_UNSIGNED_BYTE,	true,	false));
	}
	if ( FOpenGL::SupportsETC1() )
	{
		SetupTextureFormat( PF_ETC1,		FOpenGLTextureFormat(GL_ETC1_RGB8_OES,					GL_ETC1_RGB8_OES,						GL_RGBA,	GL_UNSIGNED_BYTE,	true,	false));
	}
#if PLATFORM_ANDROID
	if ( FOpenGL::SupportsETC2() )
	{
		SetupTextureFormat( PF_ETC2_RGB,	FOpenGLTextureFormat(GL_COMPRESSED_RGB8_ETC2,		FOpenGL::SupportsSRGB() ? GL_COMPRESSED_SRGB8_ETC2 : GL_COMPRESSED_RGB8_ETC2,					GL_RGBA,	GL_UNSIGNED_BYTE,	true,			false));
		SetupTextureFormat( PF_ETC2_RGBA,	FOpenGLTextureFormat(GL_COMPRESSED_RGBA8_ETC2_EAC,	FOpenGL::SupportsSRGB() ? GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC : GL_COMPRESSED_RGBA8_ETC2_EAC,	GL_RGBA,	GL_UNSIGNED_BYTE,	true,			false));
	}
#endif
	if (FOpenGL::SupportsASTC())
	{
		SetupTextureFormat( PF_ASTC_4x4,	FOpenGLTextureFormat(GL_COMPRESSED_RGBA_ASTC_4x4_KHR,	GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR,	GL_RGBA,	GL_UNSIGNED_BYTE,	true,	false) );
		SetupTextureFormat( PF_ASTC_6x6,	FOpenGLTextureFormat(GL_COMPRESSED_RGBA_ASTC_6x6_KHR,	GL_COMPRESSED_SRGB8_ALPHA8_ASTC_6x6_KHR,	GL_RGBA,	GL_UNSIGNED_BYTE,	true,	false) );
		SetupTextureFormat( PF_ASTC_8x8,	FOpenGLTextureFormat(GL_COMPRESSED_RGBA_ASTC_8x8_KHR,	GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x8_KHR,	GL_RGBA,	GL_UNSIGNED_BYTE,	true,	false) );
		SetupTextureFormat( PF_ASTC_10x10,	FOpenGLTextureFormat(GL_COMPRESSED_RGBA_ASTC_10x10_KHR,	GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x10_KHR,	GL_RGBA,	GL_UNSIGNED_BYTE,	true,	false) );
		SetupTextureFormat( PF_ASTC_12x12,	FOpenGLTextureFormat(GL_COMPRESSED_RGBA_ASTC_12x12_KHR,	GL_COMPRESSED_SRGB8_ALPHA8_ASTC_12x12_KHR,	GL_RGBA,	GL_UNSIGNED_BYTE,	true,	false) );
	}

	// Some formats need to know how large a block is.
	GPixelFormats[ PF_DepthStencil		].BlockBytes	 = 4;
	GPixelFormats[ PF_FloatRGB			].BlockBytes	 = 4;
	GPixelFormats[ PF_FloatRGBA			].BlockBytes	 = 8;
}

FOpenGLDynamicRHI::FOpenGLDynamicRHI()
:	SceneFrameCounter(0)
,	ResourceTableFrameCounter(INDEX_NONE)
,	bRevertToSharedContextAfterDrawingViewport(false)
,	bIsRenderingContextAcquired(false)
,	PlatformDevice(NULL)
,	GPUProfilingData(this)
{
	// This should be called once at the start 
	check( IsInGameThread() );
	check( !GIsThreadedRendering );

	PlatformInitOpenGL();
	PlatformDevice = PlatformCreateOpenGLDevice();
	VERIFY_GL_SCOPE();
	InitRHICapabilitiesForGL();

	check(PlatformOpenGLCurrentContext(PlatformDevice) == CONTEXT_Shared);

	PrivateOpenGLDevicePtr = this;
}

extern void DestroyShadersAndPrograms();

#if PLATFORM_ANDROID
// only used to test for shader compatibility issues
static bool VerifyCompiledShader(GLuint Shader, const ANSICHAR* GlslCode, bool IsFatal )
{
	SCOPE_CYCLE_COUNTER(STAT_OpenGLShaderCompileVerifyTime);

	GLint CompileStatus;
	glGetShaderiv(Shader, GL_COMPILE_STATUS, &CompileStatus);
	if (CompileStatus != GL_TRUE)
	{
		GLint LogLength;
		ANSICHAR DefaultLog[] = "No log";
		ANSICHAR *CompileLog = DefaultLog;
		glGetShaderiv(Shader, GL_INFO_LOG_LENGTH, &LogLength);
#if PLATFORM_ANDROID
		if ( LogLength == 0 )
		{
			// make it big anyway
			// there was a bug in android 2.2 where glGetShaderiv would return 0 even though there was a error message
			// https://code.google.com/p/android/issues/detail?id=9953
			LogLength = 4096;
		}
#endif
		if (LogLength > 1)
		{
			CompileLog = (ANSICHAR *)FMemory::Malloc(LogLength);
			glGetShaderInfoLog(Shader, LogLength, NULL, CompileLog);
		}

#if DEBUG_GL_SHADERS
		if (GlslCode)
		{
			UE_LOG(LogRHI,Warning,TEXT("Shader:\n%s"),ANSI_TO_TCHAR(GlslCode));


			const ANSICHAR *Temp = GlslCode;

			for ( int i = 0; i < 30 && (*Temp != '\0'); ++i )
			{
				FString Converted = ANSI_TO_TCHAR( Temp );
				Converted.LeftChop( 256 );

				UE_LOG(LogRHI,Display,TEXT("%s"), *Converted );
				Temp += Converted.Len();
			}

		}	
#endif
		UE_LOG(LogRHI,Warning,TEXT("Failed to compile shader. Compile log:\n%s"), ANSI_TO_TCHAR(CompileLog));
		
		if (LogLength > 1)
		{
			FMemory::Free(CompileLog);
		}
		return false;
	}
	return true;
}
#endif

static void CheckVaryingLimit()
{
#if PLATFORM_ANDROID
	FOpenGL::bRequiresGLFragCoordVaryingLimitHack = false;
	if (IsES2Platform(GMaxRHIShaderPlatform))
	{
		// Some mobile GPUs require an available varying vector to support gl_FragCoord.
		// If there are only 8 supported, it is possible to run out of varyings on these
		// GPUs so test to see if need to fake gl_FragCoord with the assumption it is
		// used for mobile HDR mosaic.

		// Do not need to do this check if more than 8 varyings supported
		if (FOpenGL::GetMaxVaryingVectors() > 8)
			return;

		// Make sure MobileHDR is on and device needs mosaic
		static auto* MobileHDRCvar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MobileHDR"));
		static auto* MobileHDR32bppCvar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MobileHDR32bpp"));
		const bool bMobileHDR32bpp = (MobileHDRCvar && MobileHDRCvar->GetValueOnAnyThread() == 1)
			&& (FAndroidMisc::SupportsFloatingPointRenderTargets() == false || (MobileHDR32bppCvar && MobileHDR32bppCvar->GetValueOnAnyThread() == 1));
		if (!bMobileHDR32bpp)
			return;

		UE_LOG(LogRHI, Display, TEXT("Testing for gl_FragCoord requiring a varying since mosaic is enabled"));
		const ANSICHAR* TestVertexProgram = "\n"
			"#version 100\n"
			"attribute vec4 in_ATTRIBUTE0;\n"
			"attribute vec4 in_ATTRIBUTE1;\n"
			"varying highp vec4 TexCoord0;\n"
			"varying highp vec4 TexCoord1;\n"
			"varying highp vec4 TexCoord2;\n"
			"varying highp vec4 TexCoord3;\n"
			"varying highp vec4 TexCoord4;\n"
			"varying highp vec4 TexCoord5;\n"
			"varying highp vec4 TexCoord6;\n"
			"varying highp vec4 TexCoord7;\n"
			"void main()\n"
			"{\n"
			"   TexCoord0 = in_ATTRIBUTE1 * vec4(0.1,0.2,0.3,0.4);\n"
			"   TexCoord1 = in_ATTRIBUTE1 * vec4(0.5,0.6,0.7,0.8);\n"
			"   TexCoord2 = in_ATTRIBUTE1 * vec4(0.12,0.22,0.32,0.42);\n"
			"   TexCoord3 = in_ATTRIBUTE1 * vec4(0.52,0.62,0.72,0.82);\n"
			"   TexCoord4 = in_ATTRIBUTE1 * vec4(0.14,0.24,0.34,0.44);\n"
			"   TexCoord5 = in_ATTRIBUTE1 * vec4(0.54,0.64,0.74,0.84);\n"
			"   TexCoord6 = in_ATTRIBUTE1 * vec4(0.16,0.26,0.36,0.46);\n"
			"   TexCoord7 = in_ATTRIBUTE1 * vec4(0.56,0.66,0.76,0.86);\n"
			"	gl_Position.xyzw = in_ATTRIBUTE0;\n"
			"}\n";
		const ANSICHAR* TestFragmentProgram = "\n"
			"#version 100\n"
			"varying highp vec4 TexCoord0;\n"
			"varying highp vec4 TexCoord1;\n"
			"varying highp vec4 TexCoord2;\n"
			"varying highp vec4 TexCoord3;\n"
			"varying highp vec4 TexCoord4;\n"
			"varying highp vec4 TexCoord5;\n"
			"varying highp vec4 TexCoord6;\n"
			"varying highp vec4 TexCoord7;\n"
			"void main()\n"
			"{\n"
			"   gl_FragColor = TexCoord0 * TexCoord1 * TexCoord2 * TexCoord3 * TexCoord4 * TexCoord5 * TexCoord6 * TexCoord7 * gl_FragCoord.xyxy;"
			"}\n";

		FOpenGLCodeHeader Header;
		Header.FrequencyMarker = 0x5653;
		Header.GlslMarker = 0x474c534c;
		TArray<uint8> VertexCode;
		{
			FMemoryWriter Writer(VertexCode);
			Writer << Header;
			Writer.Serialize((void*)(TestVertexProgram), strlen(TestVertexProgram) + 1);
			Writer.Close();
		}
		Header.FrequencyMarker = 0x5053;
		Header.GlslMarker = 0x474c534c;
		TArray<uint8> FragmentCode;
		{
			FMemoryWriter Writer(FragmentCode);
			Writer << Header;
			Writer.Serialize((void*)(TestFragmentProgram), strlen(TestFragmentProgram) + 1);
			Writer.Close();
		}

		// Try to compile test shaders
		TRefCountPtr<FOpenGLVertexShader> VertexShader = (FOpenGLVertexShader*)(RHICreateVertexShader(VertexCode).GetReference());
		if (!VerifyCompiledShader(VertexShader->Resource, TestVertexProgram, false))
		{
			UE_LOG(LogRHI, Warning, TEXT("Vertex shader for varying test failed to compile. Try running anyway."));
			return;
		}
		TRefCountPtr<FOpenGLPixelShader> PixelShader = (FOpenGLPixelShader*)(RHICreatePixelShader(FragmentCode).GetReference());
		if (!VerifyCompiledShader(PixelShader->Resource, TestFragmentProgram, false))
		{
			UE_LOG(LogRHI, Warning, TEXT("Fragment shader for varying test failed to compile. Try running anyway."));
			return;
		}

		// Now try linking them.. this is where gl_FragCoord may cause a failure
		GLuint Program = glCreateProgram();
		glAttachShader(Program, VertexShader->Resource);
		glAttachShader(Program, PixelShader->Resource);
		glLinkProgram(Program);
		GLint LinkStatus = 0;
		glGetProgramiv(Program, GL_LINK_STATUS, &LinkStatus);
		if (LinkStatus != GL_TRUE)
		{
			FOpenGL::bRequiresGLFragCoordVaryingLimitHack = true;
			UE_LOG(LogRHI, Warning, TEXT("gl_FragCoord uses a varying... enabled hack"));
			return;
		}

		UE_LOG(LogRHI, Warning, TEXT("gl_FragCoord does not need a varying"));
	}
#endif
}

static void CheckTextureCubeLodSupport()
{
#if PLATFORM_ANDROID
	if (IsES2Platform(GMaxRHIShaderPlatform))
	{
		UE_LOG(LogRHI, Display, TEXT("Testing for shader compiler compatibility"));
		// This code creates a sample program and finds out which hacks are required to compile it
		const ANSICHAR* TestFragmentProgram = "\n"
			"#version 100\n"
			"#ifndef DONTEMITEXTENSIONSHADERTEXTURELODENABLE\n"
			"#extension GL_EXT_shader_texture_lod : enable\n"
			"#endif\n"
			"precision mediump float;\n"
			"precision mediump int;\n"
			"#ifndef DONTEMITSAMPLERDEFAULTPRECISION\n"
			"precision mediump sampler2D;\n"
			"precision mediump samplerCube;\n"
			"#endif\n"
			"varying vec3 TexCoord;\n"
			"uniform samplerCube Texture;\n"
			"void main()\n"
			"{\n"
			"	gl_FragColor = textureCubeLodEXT(Texture,TexCoord, 4.0);\n"
			"}\n";

		/*TArray<uint8> Code;
		Code.Add( strlen( TestFragmentProgram ) + 1 );
		FMemory::Memcpy( Code.GetData(), TestFragmentProgram, Code.Num() );*/

		FOpenGL::bRequiresDontEmitPrecisionForTextureSamplers = false;
		FOpenGL::bRequiresTextureCubeLodEXTToTextureCubeLodDefine = false;

		FOpenGLCodeHeader Header;
		Header.FrequencyMarker = 0x5053;
		Header.GlslMarker = 0x474c534c;
		TArray<uint8> Code;
		{
			FMemoryWriter Writer(Code);
			Writer << Header;
			Writer.Serialize((void*)(TestFragmentProgram), strlen(TestFragmentProgram) + 1);
			Writer.Close();
		}
		// try to compile without any hacks
		TRefCountPtr<FOpenGLPixelShader> PixelShader = (FOpenGLPixelShader*)(RHICreatePixelShader(Code).GetReference());

		if (VerifyCompiledShader(PixelShader->Resource, TestFragmentProgram, false))
		{
			UE_LOG(LogRHI, Display, TEXT("Shaders compile fine no need to enable hacks"));
			// we are done
			return;
		}
		
		FOpenGL::bRequiresDontEmitPrecisionForTextureSamplers = true;
		FOpenGL::bRequiresTextureCubeLodEXTToTextureCubeLodDefine = false;

		// second most number of devices fall into this hack category
		// try to compile without using precision for texture samplers 
		// Samsung Galaxy Express	Samsung Galaxy S3	Samsung Galaxy S3 mini	Samsung Galaxy Tab GT-P1000	Samsung Galaxy Tab 2
		PixelShader = (FOpenGLPixelShader*)(RHICreatePixelShader(Code).GetReference());

		if (VerifyCompiledShader(PixelShader->Resource, TestFragmentProgram, false))
		{
			UE_LOG(LogRHI, Warning, TEXT("Enabling shader compiler hack to remove precision modifiers for texture samplers"));

			// we are done
			return;
		}

		FOpenGL::bRequiresDontEmitPrecisionForTextureSamplers = false;
		FOpenGL::bRequiresTextureCubeLodEXTToTextureCubeLodDefine = true;

		// third most likely Samsung Galaxy Tab GT-P1000
		PixelShader = (FOpenGLPixelShader*)(RHICreatePixelShader(Code).GetReference());

		if (VerifyCompiledShader(PixelShader->Resource, TestFragmentProgram, false))
		{
			UE_LOG(LogRHI, Warning, TEXT("Enabling shader compiler hack to redefine textureCubeLodEXT to textureCubeLod"));
			// we are done
			return;
		}

		FOpenGL::bRequiresDontEmitPrecisionForTextureSamplers = true;
		FOpenGL::bRequiresTextureCubeLodEXTToTextureCubeLodDefine = true;

		// try both hacks
		PixelShader = (FOpenGLPixelShader*)(RHICreatePixelShader(Code).GetReference());

		if (VerifyCompiledShader(PixelShader->Resource, TestFragmentProgram, false))
		{
			UE_LOG(LogRHI, Warning, TEXT("Enabling shader compiler hack to redefine textureCubeLodEXT to textureCubeLod and remove precision modifiers"));
			// we are done
			return;
		}

		UE_LOG(LogRHI, Warning, TEXT("Unable to find a test shader that compiles try running anyway"));
	}
#endif
}

void FOpenGLDynamicRHI::Init()
{
	check(!GIsRHIInitialized);
	VERIFY_GL_SCOPE();

#if PLATFORM_DESKTOP
	FShaderCache::InitShaderCache();
#endif

	InitializeStateResources();

	// Create a default point sampler state for internal use.
	FSamplerStateInitializerRHI PointSamplerStateParams(SF_Point,AM_Clamp,AM_Clamp,AM_Clamp);
	PointSamplerState = this->RHICreateSamplerState(PointSamplerStateParams);

	// Allocate vertex and index buffers for DrawPrimitiveUP calls.
	DynamicVertexBuffers.Init(CalcDynamicBufferSize(1));
	DynamicIndexBuffers.Init(CalcDynamicBufferSize(1));

	// Notify all initialized FRenderResources that there's a valid RHI device to create their RHI resources for now.
	for(TLinkedList<FRenderResource*>::TIterator ResourceIt(FRenderResource::GetResourceList());ResourceIt;ResourceIt.Next())
	{
		ResourceIt->InitDynamicRHI();
	}
	for(TLinkedList<FRenderResource*>::TIterator ResourceIt(FRenderResource::GetResourceList());ResourceIt;ResourceIt.Next())
	{
		ResourceIt->InitRHI();
	}

#if PLATFORM_WINDOWS || PLATFORM_MAC || PLATFORM_LINUX

	extern int64 GOpenGLDedicatedVideoMemory;
	extern int64 GOpenGLTotalGraphicsMemory;

	GOpenGLDedicatedVideoMemory = FOpenGL::GetVideoMemorySize();

	if ( GOpenGLDedicatedVideoMemory != 0)
	{
		GOpenGLTotalGraphicsMemory = GOpenGLDedicatedVideoMemory;

		if ( GPoolSizeVRAMPercentage > 0 )
		{
			float PoolSize = float(GPoolSizeVRAMPercentage) * 0.01f * float(GOpenGLTotalGraphicsMemory);

			// Truncate GTexturePoolSize to MB (but still counted in bytes)
			GTexturePoolSize = int64(FGenericPlatformMath::TruncToInt(PoolSize / 1024.0f / 1024.0f)) * 1024 * 1024;

			UE_LOG(LogRHI, Log, TEXT("Texture pool is %llu MB (%d%% of %llu MB)"),
				GTexturePoolSize / 1024 / 1024,
				GPoolSizeVRAMPercentage,
				GOpenGLTotalGraphicsMemory / 1024 / 1024);
		}
	}

#endif

	// Flush here since we might be switching to a different context/thread for rendering
	FOpenGL::Flush();

	FHardwareInfo::RegisterHardwareInfo( NAME_RHI, TEXT( "OpenGL" ) );

	// Set the RHI initialized flag.
	GIsRHIInitialized = true;

	CheckTextureCubeLodSupport();
	CheckVaryingLimit();
}

void FOpenGLDynamicRHI::Shutdown()
{
	check(IsInGameThread() && IsInRenderingThread()); // require that the render thread has been shut down

	Cleanup();

	DestroyShadersAndPrograms();
	PlatformDestroyOpenGLDevice(PlatformDevice);

	PrivateOpenGLDevicePtr = NULL;
}

void FOpenGLDynamicRHI::Cleanup()
{
	if(GIsRHIInitialized)
	{
#if PLATFORM_DESKTOP
		FShaderCache::ShutdownShaderCache();
#endif

		// Reset the RHI initialized flag.
		GIsRHIInitialized = false;

		GPUProfilingData.Cleanup();

		// Ask all initialized FRenderResources to release their RHI resources.
		for(TLinkedList<FRenderResource*>::TIterator ResourceIt(FRenderResource::GetResourceList());ResourceIt;ResourceIt.Next())
		{
			ResourceIt->ReleaseRHI();
		}
		for(TLinkedList<FRenderResource*>::TIterator ResourceIt(FRenderResource::GetResourceList());ResourceIt;ResourceIt.Next())
		{
			ResourceIt->ReleaseDynamicRHI();
		}
	}

	// Release dynamic vertex and index buffers.
	DynamicVertexBuffers.Cleanup();
	DynamicIndexBuffers.Cleanup();

	FreeZeroStrideBuffers();

	// Release the point sampler state.
	PointSamplerState.SafeRelease();

	extern void EmptyGLSamplerStateCache();
	EmptyGLSamplerStateCache();

	// Release zero-filled dummy uniform buffer, if it exists.
	if (PendingState.ZeroFilledDummyUniformBuffer)
	{
		FOpenGL::DeleteBuffers(1, &PendingState.ZeroFilledDummyUniformBuffer);
		PendingState.ZeroFilledDummyUniformBuffer = 0;
		DecrementBufferMemory(GL_UNIFORM_BUFFER, false, ZERO_FILLED_DUMMY_UNIFORM_BUFFER_SIZE);
	}

	// Release pending shader
	PendingState.BoundShaderState.SafeRelease();
	check(!IsValidRef(PendingState.BoundShaderState));

	PendingState.CleanupResources();
	SharedContextState.CleanupResources();
	RenderingContextState.CleanupResources();
}

void FOpenGLDynamicRHI::RHIFlushResources()
{
	PlatformFlushIfNeeded();
}

void FOpenGLDynamicRHI::RHIAcquireThreadOwnership()
{
	check(!bRevertToSharedContextAfterDrawingViewport);	// if this is true, then main thread is rendering using our context right now.
	PlatformRenderingContextSetup(PlatformDevice);
	PlatformRebindResources(PlatformDevice);
	bIsRenderingContextAcquired = true;
	VERIFY_GL(RHIAcquireThreadOwnership);
}

void FOpenGLDynamicRHI::RHIReleaseThreadOwnership()
{
	VERIFY_GL(RHIReleaseThreadOwnership);
	bIsRenderingContextAcquired = false;
	PlatformNULLContextSetup();
}

void FOpenGLDynamicRHI::RegisterQuery( FOpenGLRenderQuery* Query )
{
	FScopeLock Lock(&QueriesListCriticalSection);
	Queries.Add(Query);
}

void FOpenGLDynamicRHI::UnregisterQuery( FOpenGLRenderQuery* Query )
{
	FScopeLock Lock(&QueriesListCriticalSection);
	Queries.RemoveSingleSwap(Query);
}

void FOpenGLDynamicRHI::RHIAutomaticCacheFlushAfterComputeShader(bool bEnable)
{
	// Nothing to do here...
}

void FOpenGLDynamicRHI::RHIFlushComputeShaderCache()
{
	// Nothing to do here...
}



void* FOpenGLDynamicRHI::RHIGetNativeDevice()
{
	return nullptr;
}

void FOpenGLDynamicRHI::InvalidateQueries( void )
{
	{
		FScopeLock Lock(&QueriesListCriticalSection);
		PendingState.RunningOcclusionQuery = 0;
		for( int32 Index = 0; Index < Queries.Num(); ++Index )
		{
			Queries[Index]->bInvalidResource = true;
		}
	}

	{
		FScopeLock Lock(&TimerQueriesListCriticalSection);
		
		for( int32 Index = 0; Index < TimerQueries.Num(); ++Index )
		{
			TimerQueries[Index]->bInvalidResource = true;
		}
	}
}

bool FOpenGLDynamicRHIModule::IsSupported()
{
	return PlatformInitOpenGL();
}
