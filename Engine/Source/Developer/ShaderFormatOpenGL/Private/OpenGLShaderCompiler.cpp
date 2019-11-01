// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
// .

#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryWriter.h"
#include "ShaderFormatOpenGL.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
	#include "Windows/PreWindowsApi.h"
	#include <objbase.h>
	#include <assert.h>
	#include <stdio.h>
	#include "Windows/PostWindowsApi.h"
	#include "Windows/MinWindows.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif
#include "ShaderCore.h"
#include "ShaderPreprocessor.h"
#include "ShaderCompilerCommon.h"
#include "GlslBackend.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
	#include <GL/glcorearb.h>
	#include <GL/glext.h>
	#include <GL/wglext.h>
#include "Windows/HideWindowsPlatformTypes.h"
#elif PLATFORM_LINUX
	#include <GL/glcorearb.h>
	#include <GL/glext.h>
	#include "SDL.h"
	typedef SDL_Window*		SDL_HWindow;
	typedef SDL_GLContext	SDL_HGLContext;
	struct FPlatformOpenGLContext
	{
		SDL_HWindow		hWnd;
		SDL_HGLContext	hGLContext;		//	this is a (void*) pointer
	};
#elif PLATFORM_MAC
	#include <OpenGL/OpenGL.h>
	#include <OpenGL/gl3.h>
	#include <OpenGL/gl3ext.h>
	#ifndef GL_COMPUTE_SHADER
	#define GL_COMPUTE_SHADER 0x91B9
	#endif
	#ifndef GL_TESS_EVALUATION_SHADER
	#define GL_TESS_EVALUATION_SHADER 0x8E87
	#endif
	#ifndef GL_TESS_CONTROL_SHADER
	#define GL_TESS_CONTROL_SHADER 0x8E88
	#endif
#endif
	#include "OpenGLUtil.h"
#include "OpenGLShaderResources.h"

#ifndef USE_DXC
	#define USE_DXC (PLATFORM_MAC || PLATFORM_WINDOWS) 
#endif

#if USE_DXC
THIRD_PARTY_INCLUDES_START
#include "ShaderConductor/ShaderConductor.hpp"
#include "spirv_reflect.h"
#include <map>
THIRD_PARTY_INCLUDES_END
#endif

DEFINE_LOG_CATEGORY_STATIC(LogOpenGLShaderCompiler, Log, All);


#define VALIDATE_GLSL_WITH_DRIVER		0
#define ENABLE_IMAGINATION_COMPILER		1


static FORCEINLINE bool IsES2Platform(GLSLVersion Version)
{
	return (Version == GLSL_ES2 || Version == GLSL_150_ES2 || Version == GLSL_ES2_WEBGL || Version == GLSL_150_ES2_NOUB);
}

static FORCEINLINE bool IsPCES2Platform(GLSLVersion Version)
{
	return (Version == GLSL_150_ES2 || Version == GLSL_150_ES2_NOUB || Version == GLSL_150_ES3_1);
}

// This function should match OpenGLShaderPlatformSeparable
bool FOpenGLFrontend::SupportsSeparateShaderObjects(GLSLVersion Version)
{
	// Only desktop shader platforms can use separable shaders for now,
	// the generated code relies on macros supplied at runtime to determine whether
	// shaders may be separable and/or linked.
	return Version == GLSL_150 || Version == GLSL_150_ES2 || Version == GLSL_150_ES2_NOUB || Version == GLSL_150_ES3_1 || Version == GLSL_430;
}

/*------------------------------------------------------------------------------
	Shader compiling.
------------------------------------------------------------------------------*/

#if PLATFORM_WINDOWS
/** List all OpenGL entry points needed for shader compilation. */
#define ENUM_GL_ENTRYPOINTS(EnumMacro) \
	EnumMacro(PFNGLCOMPILESHADERPROC,glCompileShader) \
	EnumMacro(PFNGLCREATESHADERPROC,glCreateShader) \
	EnumMacro(PFNGLDELETESHADERPROC,glDeleteShader) \
	EnumMacro(PFNGLGETSHADERIVPROC,glGetShaderiv) \
	EnumMacro(PFNGLGETSHADERINFOLOGPROC,glGetShaderInfoLog) \
	EnumMacro(PFNGLSHADERSOURCEPROC,glShaderSource) \
	EnumMacro(PFNGLDELETEBUFFERSPROC,glDeleteBuffers)

/** Define all GL functions. */
#define DEFINE_GL_ENTRYPOINTS(Type,Func) static Type Func = NULL;
ENUM_GL_ENTRYPOINTS(DEFINE_GL_ENTRYPOINTS);

/** This function is handled separately because it is used to get a real context. */
static PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB;

/** Platform specific OpenGL context. */
struct FPlatformOpenGLContext
{
	HWND WindowHandle;
	HDC DeviceContext;
	HGLRC OpenGLContext;
};

/**
 * A dummy wndproc.
 */
static LRESULT CALLBACK PlatformDummyGLWndproc(HWND hWnd, uint32 Message, WPARAM wParam, LPARAM lParam)
{
	return DefWindowProc(hWnd, Message, wParam, lParam);
}

/**
 * Initialize a pixel format descriptor for the given window handle.
 */
static void PlatformInitPixelFormatForDevice(HDC DeviceContext)
{
	// Pixel format descriptor for the context.
	PIXELFORMATDESCRIPTOR PixelFormatDesc;
	FMemory::Memzero(PixelFormatDesc);
	PixelFormatDesc.nSize		= sizeof(PIXELFORMATDESCRIPTOR);
	PixelFormatDesc.nVersion	= 1;
	PixelFormatDesc.dwFlags		= PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	PixelFormatDesc.iPixelType	= PFD_TYPE_RGBA;
	PixelFormatDesc.cColorBits	= 32;
	PixelFormatDesc.cDepthBits	= 0;
	PixelFormatDesc.cStencilBits	= 0;
	PixelFormatDesc.iLayerType	= PFD_MAIN_PLANE;

	// Set the pixel format and create the context.
	int32 PixelFormat = ChoosePixelFormat(DeviceContext, &PixelFormatDesc);
	if (!PixelFormat || !SetPixelFormat(DeviceContext, PixelFormat, &PixelFormatDesc))
	{
		UE_LOG(LogOpenGLShaderCompiler, Fatal,TEXT("Failed to set pixel format for device context."));
	}
}

/**
 * Create a dummy window used to construct OpenGL contexts.
 */
static void PlatformCreateDummyGLWindow(FPlatformOpenGLContext* OutContext)
{
	const TCHAR* WindowClassName = TEXT("DummyGLToolsWindow");

	// Register a dummy window class.
	static bool bInitializedWindowClass = false;
	if (!bInitializedWindowClass)
	{
		WNDCLASS wc;

		bInitializedWindowClass = true;
		FMemory::Memzero(wc);
		wc.style = CS_OWNDC;
		wc.lpfnWndProc = PlatformDummyGLWndproc;
		wc.cbClsExtra = 0;
		wc.cbWndExtra = 0;
		wc.hInstance = NULL;
		wc.hIcon = NULL;
		wc.hCursor = NULL;
		wc.hbrBackground = (HBRUSH)(COLOR_MENUTEXT);
		wc.lpszMenuName = NULL;
		wc.lpszClassName = WindowClassName;
		ATOM ClassAtom = ::RegisterClass(&wc);
		check(ClassAtom);
	}

	// Create a dummy window.
	OutContext->WindowHandle = CreateWindowEx(
		WS_EX_WINDOWEDGE,
		WindowClassName,
		NULL,
		WS_POPUP,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		NULL, NULL, NULL, NULL);
	check(OutContext->WindowHandle);

	// Get the device context.
	OutContext->DeviceContext = GetDC(OutContext->WindowHandle);
	check(OutContext->DeviceContext);
	PlatformInitPixelFormatForDevice(OutContext->DeviceContext);
}

/**
 * Create a core profile OpenGL context.
 */
static void PlatformCreateOpenGLContextCore(FPlatformOpenGLContext* OutContext, int MajorVersion, int MinorVersion, HGLRC InParentContext)
{
	check(wglCreateContextAttribsARB);
	check(OutContext);
	check(OutContext->DeviceContext);

	int AttribList[] =
	{
		WGL_CONTEXT_MAJOR_VERSION_ARB, MajorVersion,
		WGL_CONTEXT_MINOR_VERSION_ARB, MinorVersion,
		WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB | WGL_CONTEXT_DEBUG_BIT_ARB,
		WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
		0
	};

	OutContext->OpenGLContext = wglCreateContextAttribsARB(OutContext->DeviceContext, InParentContext, AttribList);
	check(OutContext->OpenGLContext);
}

/**
 * Make the context current.
 */
static void PlatformMakeGLContextCurrent(FPlatformOpenGLContext* Context)
{
	check(Context && Context->OpenGLContext && Context->DeviceContext);
	wglMakeCurrent(Context->DeviceContext, Context->OpenGLContext);
}

/**
 * Initialize an OpenGL context so that shaders can be compiled.
 */
static void PlatformInitOpenGL(void*& ContextPtr, void*& PrevContextPtr, int InMajorVersion, int InMinorVersion)
{
	static FPlatformOpenGLContext ShaderCompileContext = {0};

	ContextPtr = (void*)wglGetCurrentDC();
	PrevContextPtr = (void*)wglGetCurrentContext();

	if (ShaderCompileContext.OpenGLContext == NULL && InMajorVersion && InMinorVersion)
	{
		PlatformCreateDummyGLWindow(&ShaderCompileContext);

		// Disable warning C4191: 'type cast' : unsafe conversion from 'PROC' to 'XXX' while getting GL entry points.
		#pragma warning(push)
		#pragma warning(disable:4191)

		if (wglCreateContextAttribsARB == NULL)
		{
			// Create a dummy context so that wglCreateContextAttribsARB can be initialized.
			ShaderCompileContext.OpenGLContext = wglCreateContext(ShaderCompileContext.DeviceContext);
			check(ShaderCompileContext.OpenGLContext);
			PlatformMakeGLContextCurrent(&ShaderCompileContext);
			wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");
			check(wglCreateContextAttribsARB);
			wglDeleteContext(ShaderCompileContext.OpenGLContext);
		}

		// Create a context so that remaining GL function pointers can be initialized.
		PlatformCreateOpenGLContextCore(&ShaderCompileContext, InMajorVersion, InMinorVersion, /*InParentContext=*/ NULL);
		check(ShaderCompileContext.OpenGLContext);
		PlatformMakeGLContextCurrent(&ShaderCompileContext);

		if (glCreateShader == NULL)
		{
			// Initialize all entry points.
			#define GET_GL_ENTRYPOINTS(Type,Func) Func = (Type)wglGetProcAddress(#Func);
			ENUM_GL_ENTRYPOINTS(GET_GL_ENTRYPOINTS);

			// Check that all of the entry points have been initialized.
			bool bFoundAllEntryPoints = true;
			#define CHECK_GL_ENTRYPOINTS(Type,Func) if (Func == NULL) { bFoundAllEntryPoints = false; UE_LOG(LogOpenGLShaderCompiler, Warning, TEXT("Failed to find entry point for %s"), TEXT(#Func)); }
			ENUM_GL_ENTRYPOINTS(CHECK_GL_ENTRYPOINTS);
			checkf(bFoundAllEntryPoints, TEXT("Failed to find all OpenGL entry points."));
		}

		// Restore warning C4191.
		#pragma warning(pop)
	}
	PlatformMakeGLContextCurrent(&ShaderCompileContext);
}
static void PlatformReleaseOpenGL(void* ContextPtr, void* PrevContextPtr)
{
	wglMakeCurrent((HDC)ContextPtr, (HGLRC)PrevContextPtr);
}
#elif PLATFORM_LINUX
/** List all OpenGL entry points needed for shader compilation. */
#define ENUM_GL_ENTRYPOINTS(EnumMacro) \
	EnumMacro(PFNGLCOMPILESHADERPROC,glCompileShader) \
	EnumMacro(PFNGLCREATESHADERPROC,glCreateShader) \
	EnumMacro(PFNGLDELETESHADERPROC,glDeleteShader) \
	EnumMacro(PFNGLGETSHADERIVPROC,glGetShaderiv) \
	EnumMacro(PFNGLGETSHADERINFOLOGPROC,glGetShaderInfoLog) \
	EnumMacro(PFNGLSHADERSOURCEPROC,glShaderSource) \
	EnumMacro(PFNGLDELETEBUFFERSPROC,glDeleteBuffers)

/** Define all GL functions. */
// We need to make pointer names different from GL functions otherwise we may end up getting
// addresses of those symbols when looking for extensions.
namespace GLFuncPointers
{
	#define DEFINE_GL_ENTRYPOINTS(Type,Func) static Type Func = NULL;
	ENUM_GL_ENTRYPOINTS(DEFINE_GL_ENTRYPOINTS);
};

using namespace GLFuncPointers;

static void _PlatformCreateDummyGLWindow(FPlatformOpenGLContext *OutContext)
{
	static bool bInitializedWindowClass = false;

	// Create a dummy window.
	OutContext->hWnd = SDL_CreateWindow(NULL,
		0, 0, 1, 1,
		SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIDDEN | SDL_WINDOW_SKIP_TASKBAR );
}

static void _PlatformCreateOpenGLContextCore(FPlatformOpenGLContext* OutContext)
{
	check(OutContext);
	SDL_HWindow PrevWindow = SDL_GL_GetCurrentWindow();
	SDL_HGLContext PrevContext = SDL_GL_GetCurrentContext();

	OutContext->hGLContext = SDL_GL_CreateContext(OutContext->hWnd);
	SDL_GL_MakeCurrent(PrevWindow, PrevContext);
}

static void _ContextMakeCurrent(SDL_HWindow hWnd, SDL_HGLContext hGLDC)
{
	GLint Result = SDL_GL_MakeCurrent( hWnd, hGLDC );
	check(!Result);
}

static void PlatformInitOpenGL(void*& ContextPtr, void*& PrevContextPtr, int InMajorVersion, int InMinorVersion)
{
	static bool bInitialized = (SDL_GL_GetCurrentWindow() != NULL) && (SDL_GL_GetCurrentContext() != NULL);

	if (!bInitialized)
	{
		check(InMajorVersion > 3 || (InMajorVersion == 3 && InMinorVersion >= 2));
		if (SDL_WasInit(0) == 0)
		{
			SDL_Init(SDL_INIT_VIDEO);
		}
		else
		{
			Uint32 InitializedSubsystemsMask = SDL_WasInit(SDL_INIT_EVERYTHING);
			if ((InitializedSubsystemsMask & SDL_INIT_VIDEO) == 0)
			{
				SDL_InitSubSystem(SDL_INIT_VIDEO);
			}
		}

		if (SDL_GL_LoadLibrary(NULL))
		{
			UE_LOG(LogOpenGLShaderCompiler, Fatal, TEXT("Unable to dynamically load libGL: %s"), ANSI_TO_TCHAR(SDL_GetError()));
		}

		if (glCreateShader == nullptr)
		{
			// Initialize all entry points.
			#define GET_GL_ENTRYPOINTS(Type,Func) GLFuncPointers::Func = reinterpret_cast<Type>(SDL_GL_GetProcAddress(#Func));
			ENUM_GL_ENTRYPOINTS(GET_GL_ENTRYPOINTS);

			// Check that all of the entry points have been initialized.
			bool bFoundAllEntryPoints = true;
			#define CHECK_GL_ENTRYPOINTS(Type,Func) if (Func == nullptr) { bFoundAllEntryPoints = false; UE_LOG(LogOpenGLShaderCompiler, Warning, TEXT("Failed to find entry point for %s"), TEXT(#Func)); }
			ENUM_GL_ENTRYPOINTS(CHECK_GL_ENTRYPOINTS);
			checkf(bFoundAllEntryPoints, TEXT("Failed to find all OpenGL entry points."));
		}

		if	(SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, InMajorVersion))
		{
			UE_LOG(LogOpenGLShaderCompiler, Fatal, TEXT("Failed to set GL major version: %s"), ANSI_TO_TCHAR(SDL_GetError()));
		}

		if	(SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, InMinorVersion))
		{
			UE_LOG(LogOpenGLShaderCompiler, Fatal, TEXT("Failed to set GL minor version: %s"), ANSI_TO_TCHAR(SDL_GetError()));
		}

		if	(SDL_GL_SetAttribute( SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG))
		{
			UE_LOG(LogOpenGLShaderCompiler, Fatal, TEXT("Failed to set GL flags: %s"), ANSI_TO_TCHAR(SDL_GetError()));
		}

		if	(SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE))
		{
			UE_LOG(LogOpenGLShaderCompiler, Fatal, TEXT("Failed to set GL mask/profile: %s"), ANSI_TO_TCHAR(SDL_GetError()));
		}

		// Create a dummy context to verify opengl support.
		FPlatformOpenGLContext DummyContext;
		_PlatformCreateDummyGLWindow(&DummyContext);
		_PlatformCreateOpenGLContextCore(&DummyContext);

		if (DummyContext.hGLContext)
		{
			_ContextMakeCurrent(DummyContext.hWnd, DummyContext.hGLContext);
		}
		else
		{
			UE_LOG(LogOpenGLShaderCompiler, Fatal, TEXT("OpenGL %d.%d not supported by driver"), InMajorVersion, InMinorVersion);
			return;
		}

		PrevContextPtr = NULL;
		ContextPtr = DummyContext.hGLContext;
		bInitialized = true;
	}

	PrevContextPtr = reinterpret_cast<void*>(SDL_GL_GetCurrentContext());
	SDL_HGLContext NewContext = SDL_GL_CreateContext(SDL_GL_GetCurrentWindow());
	SDL_GL_MakeCurrent(SDL_GL_GetCurrentWindow(), NewContext);
	ContextPtr = reinterpret_cast<void*>(NewContext);
}

static void PlatformReleaseOpenGL(void* ContextPtr, void* PrevContextPtr)
{
	SDL_GL_MakeCurrent(SDL_GL_GetCurrentWindow(), reinterpret_cast<SDL_HGLContext>(PrevContextPtr));
	SDL_GL_DeleteContext(reinterpret_cast<SDL_HGLContext>(ContextPtr));
}
#elif PLATFORM_MAC
static void PlatformInitOpenGL(void*& ContextPtr, void*& PrevContextPtr, int InMajorVersion, int InMinorVersion)
{
	check(InMajorVersion > 3 || (InMajorVersion == 3 && InMinorVersion >= 2));

	CGLPixelFormatAttribute AttribList[] =
	{
		kCGLPFANoRecovery,
		kCGLPFAAccelerated,
		kCGLPFAOpenGLProfile,
		(CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core,
		(CGLPixelFormatAttribute)0
	};

	CGLPixelFormatObj PixelFormat;
	GLint NumFormats = 0;
	CGLError Error = CGLChoosePixelFormat(AttribList, &PixelFormat, &NumFormats);
	check(Error == kCGLNoError);

	CGLContextObj ShaderCompileContext;
	Error = CGLCreateContext(PixelFormat, NULL, &ShaderCompileContext);
	check(Error == kCGLNoError);

	Error = CGLDestroyPixelFormat(PixelFormat);
	check(Error == kCGLNoError);

	PrevContextPtr = (void*)CGLGetCurrentContext();

	Error = CGLSetCurrentContext(ShaderCompileContext);
	check(Error == kCGLNoError);

	ContextPtr = (void*)ShaderCompileContext;
}
static void PlatformReleaseOpenGL(void* ContextPtr, void* PrevContextPtr)
{
	CGLContextObj ShaderCompileContext = (CGLContextObj)ContextPtr;
	CGLContextObj PreviousShaderCompileContext = (CGLContextObj)PrevContextPtr;
	CGLError Error;

	Error = CGLSetCurrentContext(PreviousShaderCompileContext);
	check(Error == kCGLNoError);

	Error = CGLDestroyContext(ShaderCompileContext);
	check(Error == kCGLNoError);
}
#endif

/** Map shader frequency -> GL shader type. */
GLenum GLFrequencyTable[] =
{
	GL_VERTEX_SHADER,	// SF_Vertex
	GL_TESS_CONTROL_SHADER,	 // SF_Hull
	GL_TESS_EVALUATION_SHADER, // SF_Domain
	GL_FRAGMENT_SHADER, // SF_Pixel
	GL_GEOMETRY_SHADER,	// SF_Geometry
	GL_COMPUTE_SHADER,  // SF_Compute
	// Ray tracing shaders are not supported in OpenGL
	GLenum(0), // SF_RayGen
	GLenum(0), // SF_RayMiss
	GLenum(0), // SF_RayHitGroup (closest hit, any hit, intersection)
	GLenum(0), // SF_RayCallable
};

static_assert(UE_ARRAY_COUNT(GLFrequencyTable) == SF_NumFrequencies, "Frequency table size mismatch.");

static inline bool IsDigit(TCHAR Char)
{
	return Char >= '0' && Char <= '9';
}

/**
 * Parse a GLSL error.
 * @param OutErrors - Storage for shader compiler errors.
 * @param InLine - A single line from the compile error log.
 */
void ParseGlslError(TArray<FShaderCompilerError>& OutErrors, const FString& InLine)
{
	const TCHAR* ErrorPrefix = TEXT("error: 0:");
	const TCHAR* p = *InLine;
	if (FCString::Strnicmp(p, ErrorPrefix, 9) == 0)
	{
		FString ErrorMsg;
		int32 LineNumber = 0;
		p += FCString::Strlen(ErrorPrefix);

		// Skip to a number, take that to be the line number.
		while (*p && !IsDigit(*p)) { p++; }
		while (*p && IsDigit(*p))
		{
			LineNumber = 10 * LineNumber + (*p++ - TEXT('0'));
		}

		// Skip to the next alphanumeric value, treat that as the error message.
		while (*p && !FChar::IsAlnum(*p)) { p++; }
		ErrorMsg = p;

		// Generate a compiler error.
		if (ErrorMsg.Len() > 0)
		{
			// Note that no mapping exists from the GLSL source to the original
			// HLSL source.
			FShaderCompilerError* CompilerError = new(OutErrors) FShaderCompilerError;
			CompilerError->StrippedErrorMessage = FString::Printf(
				TEXT("driver compile error(%d): %s"),
				LineNumber,
				*ErrorMsg
				);
		}
	}
}

static TArray<ANSICHAR> ParseIdentifierANSI(const FString& Str)
{
	TArray<ANSICHAR> Result;
	Result.Reserve(Str.Len());
	for (int32 Index = 0; Index < Str.Len(); ++Index)
	{
		Result.Add(FChar::ToLower((ANSICHAR)Str[Index]));
	}
	Result.Add('\0');

	return Result;
}

static uint32 ParseNumber(const TCHAR* Str)
{
	uint32 Num = 0;
	while (*Str && IsDigit(*Str))
	{
		Num = Num * 10 + *Str++ - '0';
	}
	return Num;
}


static ANSICHAR TranslateFrequencyToCrossCompilerPrefix(int32 Frequency)
{
	switch (Frequency)
	{
		case SF_Vertex: return 'v';
		case SF_Pixel: return 'p';
		case SF_Hull: return 'h';
		case SF_Domain: return 'd';
		case SF_Geometry: return 'g';
		case SF_Compute: return 'c';
	}
	return '\0';
}

static TCHAR* SetIndex(TCHAR* Str, int32 Offset, int32 Index)
{
	check(Index >= 0 && Index < 100);

	Str += Offset;
	if (Index >= 10)
	{
		*Str++ = '0' + (TCHAR)(Index / 10);
	}
	*Str++ = '0' + (TCHAR)(Index % 10);
	*Str = '\0';
	return Str;
}



/**
 * Construct the final microcode from the compiled and verified shader source.
 * @param ShaderOutput - Where to store the microcode and parameter map.
 * @param InShaderSource - GLSL source with input/output signature.
 * @param SourceLen - The length of the GLSL source code.
 */
void FOpenGLFrontend::BuildShaderOutput(
	FShaderCompilerOutput& ShaderOutput,
	const FShaderCompilerInput& ShaderInput,
	const ANSICHAR* InShaderSource,
	int32 SourceLen,
	GLSLVersion Version
	)
{
	const ANSICHAR* USFSource = InShaderSource;
	CrossCompiler::FHlslccHeader CCHeader;
	if (!CCHeader.Read(USFSource, SourceLen))
	{
		UE_LOG(LogOpenGLShaderCompiler, Error, TEXT("Bad hlslcc header found"));
	}

	if (*USFSource != '#')
	{
		UE_LOG(LogOpenGLShaderCompiler, Error, TEXT("Bad hlslcc header found! Missing '#'!"));
	}

	FOpenGLCodeHeader Header = {0};
	FShaderParameterMap& ParameterMap = ShaderOutput.ParameterMap;
	EShaderFrequency Frequency = (EShaderFrequency)ShaderOutput.Target.Frequency;

	TBitArray<> UsedUniformBufferSlots;
	UsedUniformBufferSlots.Init(false, 32);

	// Write out the magic markers.
	Header.GlslMarker = 0x474c534c;
	switch (Frequency)
	{
	case SF_Vertex:
		Header.FrequencyMarker = 0x5653;
		break;
	case SF_Pixel:
		Header.FrequencyMarker = 0x5053;
		break;
	case SF_Geometry:
		Header.FrequencyMarker = 0x4753;
		break;
	case SF_Hull:
		Header.FrequencyMarker = 0x4853;
		break;
	case SF_Domain:
		Header.FrequencyMarker = 0x4453;
		break;
	case SF_Compute:
		Header.FrequencyMarker = 0x4353;
		break;
	default:
		UE_LOG(LogOpenGLShaderCompiler, Fatal, TEXT("Invalid shader frequency: %d"), (int32)Frequency);
	}

	static const FString AttributePrefix = TEXT("in_ATTRIBUTE");
	static const FString AttributeVarPrefix = TEXT("in_var_ATTRIBUTE");
	static const FString GL_Prefix = TEXT("gl_");
	for (auto& Input : CCHeader.Inputs)
	{
		// Only process attributes for vertex shaders.
		if (Frequency == SF_Vertex && Input.Name.StartsWith(AttributePrefix))
		{
			int32 AttributeIndex = ParseNumber(*Input.Name + AttributePrefix.Len());
			Header.Bindings.InOutMask |= (1 << AttributeIndex);
		}
		else if (Frequency == SF_Vertex && Input.Name.StartsWith(AttributeVarPrefix))
		{
			int32 AttributeIndex = ParseNumber(*Input.Name + AttributeVarPrefix.Len());
			Header.Bindings.InOutMask |= (1 << AttributeIndex);
		}
		// Record user-defined input varyings
		else if (!Input.Name.StartsWith(GL_Prefix))
		{
			FOpenGLShaderVarying Var;
			Var.Location = Input.Index;
			Var.Varying = ParseIdentifierANSI(Input.Name);
			Header.Bindings.InputVaryings.Add(Var);
		}
	}

	// Generate vertex attribute remapping table.
	// This is used on devices where GL_MAX_VERTEX_ATTRIBS < 16
	if (Frequency == SF_Vertex)
	{
		uint32 AttributeMask = Header.Bindings.InOutMask;
		int32 NextAttributeSlot = 0;
		Header.Bindings.VertexRemappedMask = 0;
		for (int32 AttributeIndex = 0; AttributeIndex < 16; AttributeIndex++, AttributeMask >>= 1)
		{
			if (AttributeMask & 0x1)
			{
				Header.Bindings.VertexRemappedMask |= (1 << NextAttributeSlot);
				Header.Bindings.VertexAttributeRemap[AttributeIndex] = NextAttributeSlot++;
			}
			else
			{
				Header.Bindings.VertexAttributeRemap[AttributeIndex] = -1;
			}
		}
	}

	static const FString TargetPrefix = "out_Target";
	static const FString GL_FragDepth = "gl_FragDepth";
	for (auto& Output : CCHeader.Outputs)
	{
		// Only targets for pixel shaders must be tracked.
		if (Frequency == SF_Pixel && Output.Name.StartsWith(TargetPrefix))
		{
			uint8 TargetIndex = ParseNumber(*Output.Name + TargetPrefix.Len());
			Header.Bindings.InOutMask |= (1 << TargetIndex);
		}
		// Only depth writes for pixel shaders must be tracked.
		else if (Frequency == SF_Pixel && Output.Name.Equals(GL_FragDepth))
		{
			Header.Bindings.InOutMask |= 0x8000;
		}
		// Record user-defined output varyings
		else if (!Output.Name.StartsWith(GL_Prefix))
		{
			FOpenGLShaderVarying Var;
			Var.Location = Output.Index;
			Var.Varying = ParseIdentifierANSI(Output.Name);
			Header.Bindings.OutputVaryings.Add(Var);
		}
	}

	// general purpose binding name
	TCHAR BindingName[] = TEXT("XYZ\0\0\0\0\0\0\0\0");
	BindingName[0] = TranslateFrequencyToCrossCompilerPrefix(Frequency);

	TMap<FString, FString> BindingNameMap;

	// Then 'normal' uniform buffers.
	for (auto& UniformBlock : CCHeader.UniformBlocks)
	{
		uint16 UBIndex = UniformBlock.Index;
		check(UBIndex == Header.Bindings.NumUniformBuffers);
		UsedUniformBufferSlots[UBIndex] = true;
		if (OutputTrueParameterNames())
		{
			// make the final name this will be in the shader
			BindingName[1] = 'b';
			SetIndex(BindingName, 2, UBIndex);
			BindingNameMap.Add(BindingName, UniformBlock.Name);
		}
		else
		{
			ParameterMap.AddParameterAllocation(*UniformBlock.Name, Header.Bindings.NumUniformBuffers, 0, 0, EShaderParameterType::UniformBuffer);
		}
		Header.Bindings.NumUniformBuffers++;
	}

	const uint16 BytesPerComponent = 4;

	// Packed global uniforms
	TMap<ANSICHAR, uint16> PackedGlobalArraySize;
	for (auto& PackedGlobal : CCHeader.PackedGlobals)
	{
		ParameterMap.AddParameterAllocation(
			*PackedGlobal.Name,
			PackedGlobal.PackedType,
			PackedGlobal.Offset * BytesPerComponent,
			PackedGlobal.Count * BytesPerComponent,
			EShaderParameterType::LooseData
			);

		uint16& Size = PackedGlobalArraySize.FindOrAdd(PackedGlobal.PackedType);
		Size = FMath::Max<uint16>(BytesPerComponent * (PackedGlobal.Offset + PackedGlobal.Count), Size);
	}

	// Packed Uniform Buffers
	TMap<int, TMap<ANSICHAR, uint16> > PackedUniformBuffersSize;
	for (auto& PackedUB : CCHeader.PackedUBs)
	{
		checkf(OutputTrueParameterNames() == false, TEXT("Unexpected Packed UBs used with a shader format that needs true parameter names - If this is hit, we need to figure out how to handle them"));

		check(PackedUB.Attribute.Index == Header.Bindings.NumUniformBuffers);
		UsedUniformBufferSlots[PackedUB.Attribute.Index] = true;
		if (OutputTrueParameterNames())
		{
			BindingName[1] = 'b';
			// ???
		}
		else
		{
			ParameterMap.AddParameterAllocation(*PackedUB.Attribute.Name, Header.Bindings.NumUniformBuffers, 0, 0, EShaderParameterType::UniformBuffer);
		}
		Header.Bindings.NumUniformBuffers++;

		// Nothing else...
		//for (auto& Member : PackedUB.Members)
		//{
		//}
	}

	// Packed Uniform Buffers copy lists & setup sizes for each UB/Precision entry
	enum EFlattenUBState
	{
		Unknown,
		GroupedUBs,
		FlattenedUBs,
	};
	EFlattenUBState UBState = Unknown;
	for (auto& PackedUBCopy : CCHeader.PackedUBCopies)
	{
		CrossCompiler::FUniformBufferCopyInfo CopyInfo;
		CopyInfo.SourceUBIndex = PackedUBCopy.SourceUB;
		CopyInfo.SourceOffsetInFloats = PackedUBCopy.SourceOffset;
		CopyInfo.DestUBIndex = PackedUBCopy.DestUB;
		CopyInfo.DestUBTypeName = PackedUBCopy.DestPackedType;
		CopyInfo.DestUBTypeIndex = CrossCompiler::PackedTypeNameToTypeIndex(CopyInfo.DestUBTypeName);
		CopyInfo.DestOffsetInFloats = PackedUBCopy.DestOffset;
		CopyInfo.SizeInFloats = PackedUBCopy.Count;

		Header.UniformBuffersCopyInfo.Add(CopyInfo);

		auto& UniformBufferSize = PackedUniformBuffersSize.FindOrAdd(CopyInfo.DestUBIndex);
		uint16& Size = UniformBufferSize.FindOrAdd(CopyInfo.DestUBTypeName);
		Size = FMath::Max<uint16>(BytesPerComponent * (CopyInfo.DestOffsetInFloats + CopyInfo.SizeInFloats), Size);

		check(UBState == Unknown || UBState == GroupedUBs);
		UBState = GroupedUBs;
	}

	for (auto& PackedUBCopy : CCHeader.PackedUBGlobalCopies)
	{
		CrossCompiler::FUniformBufferCopyInfo CopyInfo;
		CopyInfo.SourceUBIndex = PackedUBCopy.SourceUB;
		CopyInfo.SourceOffsetInFloats = PackedUBCopy.SourceOffset;
		CopyInfo.DestUBIndex = PackedUBCopy.DestUB;
		CopyInfo.DestUBTypeName = PackedUBCopy.DestPackedType;
		CopyInfo.DestUBTypeIndex = CrossCompiler::PackedTypeNameToTypeIndex(CopyInfo.DestUBTypeName);
		CopyInfo.DestOffsetInFloats = PackedUBCopy.DestOffset;
		CopyInfo.SizeInFloats = PackedUBCopy.Count;

		Header.UniformBuffersCopyInfo.Add(CopyInfo);

		uint16& Size = PackedGlobalArraySize.FindOrAdd(CopyInfo.DestUBTypeName);
		Size = FMath::Max<uint16>(BytesPerComponent * (CopyInfo.DestOffsetInFloats + CopyInfo.SizeInFloats), Size);

		check(UBState == Unknown || UBState == FlattenedUBs);
		UBState = FlattenedUBs;
	}

	Header.Bindings.bFlattenUB = (UBState == FlattenedUBs);

	// Setup Packed Array info
	Header.Bindings.PackedGlobalArrays.Reserve(PackedGlobalArraySize.Num());
	for (auto Iterator = PackedGlobalArraySize.CreateIterator(); Iterator; ++Iterator)
	{
		ANSICHAR TypeName = Iterator.Key();
		uint16 Size = Iterator.Value();
		Size = (Size + 0xf) & (~0xf);
		CrossCompiler::FPackedArrayInfo Info;
		Info.Size = Size;
		Info.TypeName = TypeName;
		Info.TypeIndex = CrossCompiler::PackedTypeNameToTypeIndex(TypeName);
		Header.Bindings.PackedGlobalArrays.Add(Info);
	}

	// Setup Packed Uniform Buffers info
	Header.Bindings.PackedUniformBuffers.Reserve(PackedUniformBuffersSize.Num());
	for (auto Iterator = PackedUniformBuffersSize.CreateIterator(); Iterator; ++Iterator)
	{
		int BufferIndex = Iterator.Key();
		auto& ArraySizes = Iterator.Value();
		TArray<CrossCompiler::FPackedArrayInfo> InfoArray;
		InfoArray.Reserve(ArraySizes.Num());
		for (auto IterSizes = ArraySizes.CreateIterator(); IterSizes; ++IterSizes)
		{
			ANSICHAR TypeName = IterSizes.Key();
			uint16 Size = IterSizes.Value();
			Size = (Size + 0xf) & (~0xf);
			CrossCompiler::FPackedArrayInfo Info;
			Info.Size = Size;
			Info.TypeName = TypeName;
			Info.TypeIndex = CrossCompiler::PackedTypeNameToTypeIndex(TypeName);
			InfoArray.Add(Info);
		}

		Header.Bindings.PackedUniformBuffers.Add(InfoArray);
	}

	// Then samplers.
	for (auto& Sampler : CCHeader.Samplers)
	{
		if (OutputTrueParameterNames())
		{
			BindingName[1] = 's';
			SetIndex(BindingName, 2, Sampler.Offset);
			BindingNameMap.Add(BindingName, Sampler.Name);
		}
		else
		{
		ParameterMap.AddParameterAllocation(
			*Sampler.Name,
			0,
			Sampler.Offset,
			Sampler.Count,
			EShaderParameterType::SRV
			);
		}

		Header.Bindings.NumSamplers = FMath::Max<uint8>(
			Header.Bindings.NumSamplers,
			Sampler.Offset + Sampler.Count
			);

		for (auto& SamplerState : Sampler.SamplerStates)
		{
			if (OutputTrueParameterNames())
			{
				// add an entry for the sampler parameter as well
				BindingNameMap.Add(FString(BindingName) + TEXT("_samp"), SamplerState);
			}
			else
			{
				ParameterMap.AddParameterAllocation(
					*SamplerState,
					0,
					Sampler.Offset,
					Sampler.Count,
					EShaderParameterType::Sampler
					);
			}
		}
	}

	// Then UAVs (images in GLSL)
	for (auto& UAV : CCHeader.UAVs)
	{
		if (OutputTrueParameterNames())
		{
			// make the final name this will be in the shader
			BindingName[1] = 'i';
			SetIndex(BindingName, 2, UAV.Offset);
			BindingNameMap.Add(BindingName, UAV.Name);
		}
		else
		{
		ParameterMap.AddParameterAllocation(
			*UAV.Name,
			0,
			UAV.Offset,
			UAV.Count,
			EShaderParameterType::UAV
			);
		}

		Header.Bindings.NumUAVs = FMath::Max<uint8>(
			Header.Bindings.NumSamplers,
			UAV.Offset + UAV.Count
			);
	}

	Header.ShaderName = CCHeader.Name;

	// perform any post processing this frontend class may need to do
	ShaderOutput.bSucceeded = PostProcessShaderSource(Version, Frequency, USFSource, SourceLen + 1 - (USFSource - InShaderSource), ParameterMap, BindingNameMap, ShaderOutput.Errors, ShaderInput);

	// Build the SRT for this shader.
	{
		// Build the generic SRT for this shader.
		FShaderCompilerResourceTable GenericSRT;
		BuildResourceTableMapping(ShaderInput.Environment.ResourceTableMap, ShaderInput.Environment.ResourceTableLayoutHashes, UsedUniformBufferSlots, ShaderOutput.ParameterMap, GenericSRT);

		// Copy over the bits indicating which resource tables are active.
		Header.Bindings.ShaderResourceTable.ResourceTableBits = GenericSRT.ResourceTableBits;

		Header.Bindings.ShaderResourceTable.ResourceTableLayoutHashes = GenericSRT.ResourceTableLayoutHashes;

		// Now build our token streams.
		BuildResourceTableTokenStream(GenericSRT.TextureMap, GenericSRT.MaxBoundResourceTable, Header.Bindings.ShaderResourceTable.TextureMap);
		BuildResourceTableTokenStream(GenericSRT.ShaderResourceViewMap, GenericSRT.MaxBoundResourceTable, Header.Bindings.ShaderResourceTable.ShaderResourceViewMap);
		BuildResourceTableTokenStream(GenericSRT.SamplerMap, GenericSRT.MaxBoundResourceTable, Header.Bindings.ShaderResourceTable.SamplerMap);
		BuildResourceTableTokenStream(GenericSRT.UnorderedAccessViewMap, GenericSRT.MaxBoundResourceTable, Header.Bindings.ShaderResourceTable.UnorderedAccessViewMap);
	}

	const int32 MaxSamplers = GetMaxSamplers(Version);

	if (Header.Bindings.NumSamplers > MaxSamplers)
	{
		ShaderOutput.bSucceeded = false;
		FShaderCompilerError* NewError = new(ShaderOutput.Errors) FShaderCompilerError();
		NewError->StrippedErrorMessage =
			FString::Printf(TEXT("shader uses %d samplers exceeding the limit of %d"),
				Header.Bindings.NumSamplers, MaxSamplers);
	}
	else if (ShaderOutput.bSucceeded)
	{
		// Write out the header
		FMemoryWriter Ar(ShaderOutput.ShaderCode.GetWriteAccess(), true);
		Ar << Header;

		if (OptionalSerializeOutputAndReturnIfSerialized(Ar) == false)
		{
			Ar.Serialize((void*)USFSource, SourceLen + 1 - (USFSource - InShaderSource));
			ShaderOutput.bSucceeded = true;
		}

		// store data we can pickup later with ShaderCode.FindOptionalData('n'), could be removed for shipping
		// Daniel L: This GenerateShaderName does not generate a deterministic output among shaders as the shader code can be shared.
		//			uncommenting this will cause the project to have non deterministic materials and will hurt patch sizes
		//ShaderOutput.ShaderCode.AddOptionalData('n', TCHAR_TO_UTF8(*ShaderInput.GenerateShaderName()));

		// extract final source code as requested by the Material Editor
		if (ShaderInput.ExtraSettings.bExtractShaderSource)
		{
			TArray<ANSICHAR> GlslCodeOriginal;
			GlslCodeOriginal.Append(USFSource, FCStringAnsi::Strlen(USFSource) + 1);
			ShaderOutput.OptionalFinalShaderSource = FString(GlslCodeOriginal.GetData());
		}

		// if available, attempt run an offline compilation and extract statistics
		if (ShaderInput.ExtraSettings.OfflineCompilerPath.Len() > 0)
		{
			CompileOffline(ShaderInput, ShaderOutput, Version, USFSource);
		}
		else
		{
			ShaderOutput.NumInstructions = 0;
		}

		ShaderOutput.NumTextureSamplers = Header.Bindings.NumSamplers;
	}
}

void FOpenGLFrontend::ConvertOpenGLVersionFromGLSLVersion(GLSLVersion InVersion, int& OutMajorVersion, int& OutMinorVersion)
{
	switch(InVersion)
	{
		case GLSL_150:
			OutMajorVersion = 3;
			OutMinorVersion = 2;
			break;
		case GLSL_310_ES_EXT:
		case GLSL_430:
			OutMajorVersion = 4;
			OutMinorVersion = 3;
			break;
		case GLSL_150_ES2:
		case GLSL_150_ES2_NOUB:
		case GLSL_150_ES3_1:
			OutMajorVersion = 3;
			OutMinorVersion = 2;
			break;
		case GLSL_ES2_WEBGL:
		case GLSL_ES2:
		case GLSL_ES3_1_ANDROID:
			OutMajorVersion = 0;
			OutMinorVersion = 0;
			break;
		default:
			// Invalid enum
			check(0);
			OutMajorVersion = 0;
			OutMinorVersion = 0;
			break;
	}
}

static const TCHAR* GetGLSLES2CompilerExecutable(bool bNDACompiler)
{
	// Unfortunately no env var is set to handle install path
	return (bNDACompiler
		? TEXT("C:\\Imagination\\PowerVR\\GraphicsSDK\\Compilers\\OGLES\\Windows_x86_32\\glslcompiler_sgx543_nda.exe")
		: TEXT("C:\\Imagination\\PowerVR\\GraphicsSDK\\Compilers\\OGLES\\Windows_x86_32\\glslcompiler_sgx543.exe"));
}

static FString CreateGLSLES2CompilerArguments(const FString& ShaderFile, const FString& OutputFile, EHlslShaderFrequency Frequency, bool bNDACompiler)
{
	const TCHAR* FrequencySwitch = TEXT("");
	switch (Frequency)
	{
	case HSF_PixelShader:
		FrequencySwitch = TEXT(" -f");
		break;

	case HSF_VertexShader:
		FrequencySwitch = TEXT(" -v");
		break;

	default:
		return TEXT("");
	}

	FString Arguments = FString::Printf(TEXT("%s %s %s -profile -perfsim"), *FPaths::GetCleanFilename(ShaderFile), *FPaths::GetCleanFilename(OutputFile), FrequencySwitch);

	if (bNDACompiler)
	{
		Arguments += " -disasm";
	}

	return Arguments;
}

static FString CreateCommandLineGLSLES2(const FString& ShaderFile, const FString& OutputFile, GLSLVersion Version, EHlslShaderFrequency Frequency, bool bNDACompiler)
{
	if (Version != GLSL_ES2 && Version != GLSL_ES2_WEBGL)
	{
		return TEXT("");
	}

	FString CmdLine = FString(GetGLSLES2CompilerExecutable(bNDACompiler)) + TEXT(" ") + CreateGLSLES2CompilerArguments(ShaderFile, OutputFile, Frequency, bNDACompiler);
	CmdLine += FString(LINE_TERMINATOR) + TEXT("pause");
	return CmdLine;
}

/** Precompile a glsl shader for ES2. */
void FOpenGLFrontend::PrecompileGLSLES2(FShaderCompilerOutput& ShaderOutput, const FShaderCompilerInput& ShaderInput, const ANSICHAR* ShaderSource, EHlslShaderFrequency Frequency)
{
	const TCHAR* CompilerExecutableName = GetGLSLES2CompilerExecutable(false);
	const int32 SourceLen = FCStringAnsi::Strlen(ShaderSource);
	const bool bCompilerExecutableExists = FPaths::FileExists(CompilerExecutableName);

	// Using the debug info path to write out the files to disk for the PVR shader compiler
	if (ShaderInput.DumpDebugInfoPath != TEXT("") && bCompilerExecutableExists)
	{
		const FString GLSLSourceFile = (ShaderInput.DumpDebugInfoPath / TEXT("GLSLSource.txt"));
		bool bSavedSuccessfully = false;

		{
			FArchive* Ar = IFileManager::Get().CreateFileWriter(*GLSLSourceFile, FILEWRITE_EvenIfReadOnly);

			// Save the ansi file to disk so it can be used as input to the PVR shader compiler
			if (Ar)
			{
				bSavedSuccessfully = true;

				// @todo: Patch the code so that textureCubeLodEXT gets converted to textureCubeLod to workaround PowerVR issues
				const ANSICHAR* VersionString = FCStringAnsi::Strifind(ShaderSource, "#version 100");
				check(VersionString);
				VersionString += 12;	// strlen("# version 100");
				Ar->Serialize((void*)ShaderSource, (VersionString - ShaderSource) * sizeof(ANSICHAR));
				const char* PVRWorkaround = "\n#ifndef textureCubeLodEXT\n#define textureCubeLodEXT textureCubeLod\n#endif\n";
				Ar->Serialize((void*)PVRWorkaround, FCStringAnsi::Strlen(PVRWorkaround));
				Ar->Serialize((void*)VersionString, (SourceLen - (VersionString - ShaderSource)) * sizeof(ANSICHAR));
				delete Ar;
			}
		}

		if (bSavedSuccessfully && ENABLE_IMAGINATION_COMPILER)
		{
			const FString Arguments = CreateGLSLES2CompilerArguments(GLSLSourceFile, TEXT("ASM.txt"), Frequency, false);

			FString StdOut;
			FString StdErr;
			int32 ReturnCode = 0;

			// Run the PowerVR shader compiler and wait for completion
			FPlatformProcess::ExecProcess(GetGLSLES2CompilerExecutable(false), *Arguments, &ReturnCode, &StdOut, &StdErr);

			if (ReturnCode >= 0)
			{
				ShaderOutput.bSucceeded = true;
				ShaderOutput.Target = ShaderInput.Target;

				BuildShaderOutput(ShaderOutput, ShaderInput, ShaderSource, SourceLen, GLSL_ES2);

				// Parse the cycle count
				const int32 CycleCountStringLength = FPlatformString::Strlen(TEXT("Cycle count: "));
				const int32 CycleCountIndex = StdOut.Find(TEXT("Cycle count: "));

				if (CycleCountIndex != INDEX_NONE && CycleCountIndex + CycleCountStringLength < StdOut.Len())
				{
					const int32 CycleCountEndIndex = StdOut.Find(TEXT("\n"), ESearchCase::IgnoreCase, ESearchDir::FromStart, CycleCountIndex + CycleCountStringLength);

					if (CycleCountEndIndex != INDEX_NONE)
					{
						const FString InstructionSubstring = StdOut.Mid(CycleCountIndex + CycleCountStringLength, CycleCountEndIndex - (CycleCountIndex + CycleCountStringLength));
						ShaderOutput.NumInstructions = FCString::Atoi(*InstructionSubstring);
					}
				}
			}
			else
			{
				ShaderOutput.bSucceeded = false;

				FShaderCompilerError* NewError = new(ShaderOutput.Errors) FShaderCompilerError();
				// Print the name of the generated glsl file so we can open it with a double click in the VS.Net output window
				NewError->StrippedErrorMessage = FString::Printf(TEXT("%s \nPVR SDK glsl compiler for SGX543: %s"), *GLSLSourceFile, *StdOut);
			}
		}
		else
		{
			ShaderOutput.bSucceeded = true;
			ShaderOutput.Target = ShaderInput.Target;

			BuildShaderOutput(ShaderOutput, ShaderInput, ShaderSource, SourceLen, GLSL_ES2);
		}
	}
	else
	{
		ShaderOutput.bSucceeded = true;
		ShaderOutput.Target = ShaderInput.Target;

		BuildShaderOutput(ShaderOutput, ShaderInput, ShaderSource, SourceLen, GLSL_ES2);
	}
}

/**
 * Precompile a GLSL shader.
 * @param ShaderOutput - The precompiled shader.
 * @param ShaderInput - The shader input.
 * @param InPreprocessedShader - The preprocessed source code.
 */
void FOpenGLFrontend::PrecompileShader(FShaderCompilerOutput& ShaderOutput, const FShaderCompilerInput& ShaderInput, const ANSICHAR* ShaderSource, GLSLVersion Version, EHlslShaderFrequency Frequency)
{
	check(ShaderInput.Target.Frequency < SF_NumFrequencies);

	// Lookup the GL shader type.
	GLenum GLFrequency = GLFrequencyTable[ShaderInput.Target.Frequency];
	if (GLFrequency == GL_NONE)
	{
		ShaderOutput.bSucceeded = false;
		FShaderCompilerError* NewError = new(ShaderOutput.Errors) FShaderCompilerError();
		NewError->StrippedErrorMessage = FString::Printf(TEXT("%s shaders not supported for use in OpenGL."), CrossCompiler::GetFrequencyName((EShaderFrequency)ShaderInput.Target.Frequency));
		return;
	}

	if (Version == GLSL_ES2 || Version == GLSL_ES2_WEBGL)
	{
		PrecompileGLSLES2(ShaderOutput, ShaderInput, ShaderSource, Frequency);
	}
	else
	{
		// Create the shader with the preprocessed source code.
		void* ContextPtr;
		void* PrevContextPtr;
		int MajorVersion = 0;
		int MinorVersion = 0;
		ConvertOpenGLVersionFromGLSLVersion(Version, MajorVersion, MinorVersion);
		PlatformInitOpenGL(ContextPtr, PrevContextPtr, MajorVersion, MinorVersion);

		GLint SourceLen = FCStringAnsi::Strlen(ShaderSource);
		GLuint Shader = glCreateShader(GLFrequency);
		{
			const GLchar* SourcePtr = ShaderSource;
			glShaderSource(Shader, 1, &SourcePtr, &SourceLen);
		}

		// Compile and get results.
		glCompileShader(Shader);
		{
			GLint CompileStatus;
			glGetShaderiv(Shader, GL_COMPILE_STATUS, &CompileStatus);
			if (CompileStatus == GL_TRUE)
			{
				ShaderOutput.Target = ShaderInput.Target;
				BuildShaderOutput(
					ShaderOutput,
					ShaderInput,
					ShaderSource,
					(int32)SourceLen,
					Version
					);
			}
			else
			{
				GLint LogLength;
				glGetShaderiv(Shader, GL_INFO_LOG_LENGTH, &LogLength);
				if (LogLength > 1)
				{
					TArray<ANSICHAR> RawCompileLog;
					FString CompileLog;
					TArray<FString> LogLines;

					RawCompileLog.Empty(LogLength);
					RawCompileLog.AddZeroed(LogLength);
					glGetShaderInfoLog(Shader, LogLength, /*OutLength=*/ NULL, RawCompileLog.GetData());
					CompileLog = ANSI_TO_TCHAR(RawCompileLog.GetData());
					CompileLog.ParseIntoArray(LogLines, TEXT("\n"), true);

					for (int32 Line = 0; Line < LogLines.Num(); ++Line)
					{
						ParseGlslError(ShaderOutput.Errors, LogLines[Line]);
					}

					if (ShaderOutput.Errors.Num() == 0)
					{
						FShaderCompilerError* NewError = new(ShaderOutput.Errors) FShaderCompilerError();
						NewError->StrippedErrorMessage = FString::Printf(
							TEXT("GLSL source:\n%sGL compile log: %s\n"),
							ANSI_TO_TCHAR(ShaderSource),
							ANSI_TO_TCHAR(RawCompileLog.GetData())
							);
					}
				}
				else
				{
					FShaderCompilerError* NewError = new(ShaderOutput.Errors) FShaderCompilerError();
					NewError->StrippedErrorMessage = TEXT("Shader compile failed without errors.");
				}

				ShaderOutput.bSucceeded = false;
			}
		}
		glDeleteShader(Shader);
		PlatformReleaseOpenGL(ContextPtr, PrevContextPtr);
	}
}

void FOpenGLFrontend::SetupPerVersionCompilationEnvironment(GLSLVersion Version, FShaderCompilerDefinitions& AdditionalDefines, EHlslCompileTarget& HlslCompilerTarget)
	{
	switch (Version)
	{
		case GLSL_ES3_1_ANDROID:
			AdditionalDefines.SetDefine(TEXT("COMPILER_GLSL_ES3_1"), 1);
			AdditionalDefines.SetDefine(TEXT("ES3_1_PROFILE"), 1);
			HlslCompilerTarget = HCT_FeatureLevelES3_1;
			break;

		case GLSL_310_ES_EXT:
			AdditionalDefines.SetDefine(TEXT("COMPILER_GLSL"), 1);
			AdditionalDefines.SetDefine(TEXT("COMPILER_GLSL_ES3_1_EXT"), 1);
			AdditionalDefines.SetDefine(TEXT("ESDEFERRED_PROFILE"), 1);
			AdditionalDefines.SetDefine(TEXT("GL4_PROFILE"), 1);
			HlslCompilerTarget = HCT_FeatureLevelES3_1Ext;
			break;

		case GLSL_430:
			AdditionalDefines.SetDefine(TEXT("COMPILER_GLSL"), 1);
			AdditionalDefines.SetDefine(TEXT("GL4_PROFILE"), 1);
			HlslCompilerTarget = HCT_FeatureLevelSM5;
			break;

		case GLSL_150:
			AdditionalDefines.SetDefine(TEXT("COMPILER_GLSL"), 1);
			AdditionalDefines.SetDefine(TEXT("GL3_PROFILE"), 1);
			HlslCompilerTarget = HCT_FeatureLevelSM4;
			break;

		case GLSL_ES2_WEBGL:
			AdditionalDefines.SetDefine(TEXT("WEBGL"), 1);
			AdditionalDefines.SetDefine(TEXT("COMPILER_GLSL_ES2"), 1);
			AdditionalDefines.SetDefine(TEXT("ES2_PROFILE"), 1);
			HlslCompilerTarget = HCT_FeatureLevelES2;
			AdditionalDefines.SetDefine(TEXT("row_major"), TEXT(""));
			break;

		case GLSL_ES2:
			AdditionalDefines.SetDefine(TEXT("COMPILER_GLSL_ES2"), 1);
			AdditionalDefines.SetDefine(TEXT("ES2_PROFILE"), 1);
			HlslCompilerTarget = HCT_FeatureLevelES2;
			AdditionalDefines.SetDefine(TEXT("row_major"), TEXT(""));
			break;

		case GLSL_150_ES2:
		case GLSL_150_ES2_NOUB:
			AdditionalDefines.SetDefine(TEXT("COMPILER_GLSL"), 1);
			AdditionalDefines.SetDefine(TEXT("ES2_PROFILE"), 1);
			HlslCompilerTarget = HCT_FeatureLevelSM4;
			AdditionalDefines.SetDefine(TEXT("row_major"), TEXT(""));
			break;

		case GLSL_150_ES3_1:
			AdditionalDefines.SetDefine(TEXT("COMPILER_GLSL"), 1);
			AdditionalDefines.SetDefine(TEXT("ES3_1_PROFILE"), 1);
			HlslCompilerTarget = HCT_FeatureLevelES3_1;
			AdditionalDefines.SetDefine(TEXT("row_major"), TEXT(""));
			break;

		default:
			check(0);
	}

	AdditionalDefines.SetDefine(TEXT("OPENGL_PROFILE"), 1);
}

uint32 FOpenGLFrontend::GetMaxSamplers(GLSLVersion Version)
{
	switch (Version)
	{
		// Assume that GL4.3 targets support 32 samplers as we don't currently support separate sampler objects
		case GLSL_430:
			return 32;

		// mimicing the old GetFeatureLevelMaxTextureSamplers for the rest
		case GLSL_ES2:
		case GLSL_150_ES2:
		case GLSL_150_ES2_NOUB:
			return 8;

		case GLSL_ES2_WEBGL:
			// For WebGL 1 and 2, GL_MAX_TEXTURE_IMAGE_UNITS is generally much higher than on old GLES 2 Android
			// devices, but we only know the limit at runtime. Assume a decent desktop default.
			return 32;

		default:
			return 16;
	}
}

uint32 FOpenGLFrontend::CalculateCrossCompilerFlags(GLSLVersion Version, const TArray<uint32>& CompilerFlags)
{
	uint32  CCFlags = HLSLCC_NoPreprocess | HLSLCC_PackUniforms | HLSLCC_DX11ClipSpace | HLSLCC_RetainSizes;
	if (IsES2Platform(Version) && !IsPCES2Platform(Version))
	{
		CCFlags |= HLSLCC_FlattenUniformBuffers | HLSLCC_FlattenUniformBufferStructures;
		// Currently only enabled for ES2, as there are still features to implement for SM4+ (atomics, global store, UAVs, etc)
		CCFlags |= HLSLCC_ApplyCommonSubexpressionElimination;
		CCFlags |= HLSLCC_ExpandUBMemberArrays;
	}

	if (CompilerFlags.Contains(CFLAG_UseFullPrecisionInPS) || Version == GLSL_ES2_WEBGL)
	{
		CCFlags |= HLSLCC_UseFullPrecisionInPS;
	}

	if (Version == GLSL_150_ES2_NOUB ||
		CompilerFlags.Contains(CFLAG_FeatureLevelES31) ||
		CompilerFlags.Contains(CFLAG_UseEmulatedUB))
	{
		CCFlags |= HLSLCC_FlattenUniformBuffers | HLSLCC_FlattenUniformBufferStructures;
		// Enabling HLSLCC_GroupFlattenedUniformBuffers, see FORT-159483.
		CCFlags |= HLSLCC_GroupFlattenedUniformBuffers;
		CCFlags |= HLSLCC_ExpandUBMemberArrays;
	}

	if (SupportsSeparateShaderObjects(Version))
	{
		CCFlags |= HLSLCC_SeparateShaderObjects;
	}

	if (CompilerFlags.Contains(CFLAG_UsesExternalTexture))
	{
		CCFlags |= HLSLCC_UsesExternalTexture;
	}

	return CCFlags;
}

FGlslCodeBackend* FOpenGLFrontend::CreateBackend(GLSLVersion Version, uint32 CCFlags, EHlslCompileTarget HlslCompilerTarget)
{
	return new FGlslCodeBackend(CCFlags, HlslCompilerTarget, Version == GLSL_ES2_WEBGL);
}

FGlslLanguageSpec* FOpenGLFrontend::CreateLanguageSpec(GLSLVersion Version)
{
	bool bIsES2 = (IsES2Platform(Version) && !IsPCES2Platform(Version));
	bool bIsWebGL = (Version == GLSL_ES2_WEBGL);
					// For backwards compatibility when targeting WebGL 2 shaders,
					// generate GLES2/WebGL 1 style shaders but with GLES3/WebGL 2
					// constructs available.
	bool bIsES31 = (Version == GLSL_150_ES3_1 || Version == GLSL_ES3_1_ANDROID);

	return new FGlslLanguageSpec(bIsES2, bIsWebGL, bIsES31);
}

#if USE_DXC
static void CompileShaderDXC(FShaderCompilerInput const& Input, FShaderCompilerOutput& Output, const FString& WorkingDirectory, GLSLVersion Version, const EHlslShaderFrequency Frequency, uint32 CCFlags, FString const& PreprocessedShader, int32& Result, char*& GlslShaderSource, char*& ErrorLog)
{
	ShaderConductor::Compiler::Options Options;
	Options.removeUnusedGlobals = true;
	Options.packMatricesInRowMajor = false;
	Options.enableDebugInfo = false;
	Options.enable16bitTypes = false;
	Options.disableOptimizations = false;
	Options.globalsAsPushConstants = true;

	ShaderConductor::Compiler::SourceDesc SourceDesc;

	std::string SourceData(TCHAR_TO_UTF8(*PreprocessedShader));
	std::string FileName(TCHAR_TO_UTF8(*Input.VirtualSourceFilePath));
	std::string EntryPointName(TCHAR_TO_UTF8(*Input.EntryPointName));

	SourceData = "float4 gl_FragColor;\nfloat4 gl_LastFragColorARM;\nfloat gl_LastFragDepthARM;\nbool ARM_shader_framebuffer_fetch;\nbool ARM_shader_framebuffer_fetch_depth_stencil;\nfloat4 FramebufferFetchES2() { if(!ARM_shader_framebuffer_fetch) { return gl_FragColor; } else { return gl_LastFragColorARM; } }\nfloat DepthbufferFetchES2(float OptionalDepth, float C1, float C2) { return (!ARM_shader_framebuffer_fetch_depth_stencil ? OptionalDepth : (clamp(1.0/(gl_LastFragDepthARM*C1-C2), 0.0, 65000.0))); }\n" + SourceData;


	std::string FrameBufferDefines = "#ifdef UE_EXT_shader_framebuffer_fetch\n"
	"#define _Globals_ARM_shader_framebuffer_fetch 0\n"
	"#define FRAME_BUFFERFETCH_STORAGE_QUALIFIER inout\n"
	"#define _Globals_gl_FragColor out_var_SV_Target0\n"
	"#define _Globals_gl_LastFragColorARM vec4(65000.0, 65000.0, 65000.0, 65000.0)\n"
	"#elif defined( GL_ARM_shader_framebuffer_fetch)\n"
	"#define _Globals_ARM_shader_framebuffer_fetch 1\n"
	"#define FRAME_BUFFERFETCH_STORAGE_QUALIFIER out\n"
	"#define _Globals_gl_FragColor vec4(65000.0, 65000.0, 65000.0, 65000.0)\n"
	"#define _Globals_gl_LastFragColorARM gl_LastFragDepthARM\n"
	"#else\n"
	"#define FRAME_BUFFERFETCH_STORAGE_QUALIFIER out\n"
	"#define _Globals_ARM_shader_framebuffer_fetch 0\n"
	"#define _Globals_gl_FragColor vec4(65000.0, 65000.0, 65000.0, 65000.0)\n"
	"#define _Globals_gl_LastFragColorARM vec4(65000.0, 65000.0, 65000.0, 65000.0)\n"
	"#endif\n"
	"#ifdef GL_ARM_shader_framebuffer_fetch_depth_stencil\n"
	"	#define _Globals_ARM_shader_framebuffer_fetch_depth_stencil 1\n"
	"#else\n"
	"	#define _Globals_ARM_shader_framebuffer_fetch_depth_stencil 0\n"
	"#endif\n";
	
	ShaderConductor::MacroDefine NewDefines[] = { {"TextureExternal", "Texture2D"} };

	SourceDesc.source = SourceData.c_str();
	SourceDesc.fileName = FileName.c_str();
	SourceDesc.entryPoint = EntryPointName.c_str();
	SourceDesc.numDefines = 1;
	SourceDesc.defines = NewDefines;

	const char* FrequencyPrefix = nullptr;
	switch (Frequency)
	{
	case HSF_VertexShader:
		SourceDesc.stage = ShaderConductor::ShaderStage::VertexShader;
		FrequencyPrefix = "v";
		break;
	case HSF_PixelShader:
		SourceDesc.stage = ShaderConductor::ShaderStage::PixelShader;
		FrequencyPrefix = "p";
		break;
	case HSF_GeometryShader:
		SourceDesc.stage = ShaderConductor::ShaderStage::GeometryShader;
		FrequencyPrefix = "g";
		break;
	case HSF_HullShader:
		SourceDesc.stage = ShaderConductor::ShaderStage::HullShader;
		FrequencyPrefix = "h";
		break;
	case HSF_DomainShader:
		SourceDesc.stage = ShaderConductor::ShaderStage::DomainShader;
		FrequencyPrefix = "d";
		break;
	case HSF_ComputeShader:
		SourceDesc.stage = ShaderConductor::ShaderStage::ComputeShader;
		FrequencyPrefix = "c";
		break;

	default:
		break;
	}

	ShaderConductor::Blob* RewriteBlob = nullptr;
	ShaderConductor::Compiler::ResultDesc Results = ShaderConductor::Compiler::Rewrite(SourceDesc, Options);
	RewriteBlob = Results.target;

	SourceData.clear();
	SourceData.resize(RewriteBlob->Size());
	FCStringAnsi::Strncpy(const_cast<char*>(SourceData.c_str()), (const char*)RewriteBlob->Data(), RewriteBlob->Size());

	SourceDesc.source = SourceData.c_str();

	const bool bDumpDebugInfo = (Input.DumpDebugInfoPath != TEXT("") && IFileManager::Get().DirectoryExists(*Input.DumpDebugInfoPath));
	if (bDumpDebugInfo)
	{
		FArchive* FileWriter = IFileManager::Get().CreateFileWriter(*(Input.DumpDebugInfoPath / Input.GetSourceFilename()));
		if (FileWriter)
		{
			auto AnsiSourceFile = StringCast<ANSICHAR>(*PreprocessedShader);
			FileWriter->Serialize((ANSICHAR*)AnsiSourceFile.Get(), AnsiSourceFile.Length());
			{
				FString Line = CrossCompiler::CreateResourceTableFromEnvironment(Input.Environment);

				Line += TEXT("#if 0 /*DIRECT COMPILE*/\n");
				Line += CreateShaderCompilerWorkerDirectCommandLine(Input, CCFlags);
				Line += TEXT("\n#endif /*DIRECT COMPILE*/\n");

				FileWriter->Serialize(TCHAR_TO_ANSI(*Line), Line.Len());
			}
			FileWriter->Close();
			delete FileWriter;
		}

		FArchive* HlslFileWriter = IFileManager::Get().CreateFileWriter(*((Input.DumpDebugInfoPath / Input.GetSourceFilename()) + TEXT(".dxc.hlsl")));
		if (HlslFileWriter)
		{
			HlslFileWriter->Serialize(const_cast<char*>(SourceData.c_str()), SourceData.length());
			HlslFileWriter->Close();
			delete HlslFileWriter;
		}

		if (Input.bGenerateDirectCompileFile)
		{
			FFileHelper::SaveStringToFile(CreateShaderCompilerWorkerDirectCommandLine(Input), *(Input.DumpDebugInfoPath / TEXT("DirectCompile.txt")));
		}
	}

	Options.removeUnusedGlobals = false;

	ShaderConductor::Compiler::TargetDesc TargetDesc;
	TargetDesc.language = ShaderConductor::ShadingLanguage::SpirV;
	ShaderConductor::Compiler::ResultDesc SpirvResults = ShaderConductor::Compiler::Compile(SourceDesc, Options, TargetDesc);

	if (SpirvResults.hasError && SpirvResults.errorWarningMsg)
	{
		std::string ErrorText((const char*)SpirvResults.errorWarningMsg->Data(), SpirvResults.errorWarningMsg->Size());
#if !PLATFORM_WINDOWS
		ErrorLog = strdup(ErrorText.c_str());
#else
		ErrorLog = _strdup(ErrorText.c_str());
#endif
		ShaderConductor::DestroyBlob(SpirvResults.errorWarningMsg);
	}
	else if (!SpirvResults.hasError)
	{
		FString MetaData;

		// Now perform reflection on the SPIRV and tweak any decorations that we need to.
		// This used to be done via JSON, but that was slow and alloc happy so use SPIRV-Reflect instead.
		spv_reflect::ShaderModule Reflection(SpirvResults.target->Size(), SpirvResults.target->Data());
		check(Reflection.GetResult() == SPV_REFLECT_RESULT_SUCCESS);

		SpvReflectResult SPVRResult = SPV_REFLECT_RESULT_NOT_READY;
		uint32 Count = 0;
		TArray<SpvReflectDescriptorBinding*> Bindings;
		TSet<SpvReflectDescriptorBinding*> Counters;
		TArray<SpvReflectInterfaceVariable*> InputVars;
		TArray<SpvReflectInterfaceVariable*> OutputVars;
		TArray<SpvReflectBlockVariable*> ConstantBindings;
		uint32 GlobalSetId = 32;
		FString SRVString;
		FString UAVString;
		FString UBOString;
		FString SMPString;
		FString INPString;
		FString OUTString;
		FString PAKString;
		TArray<FString> Textures;
		TArray<FString> Samplers;
		uint32 UAVIndices = 0xffffffff;
		uint32 BufferIndices = 0xffffffff;
		uint32 TextureIndices = 0xffffffff;
		uint32 UBOIndices = 0xffffffff;
		uint32 SamplerIndices = 0xffffffff;

		std::map<std::string, std::string> UniformVarNames;
		Count = 0;
		SPVRResult = Reflection.EnumerateDescriptorBindings(&Count, nullptr);
		check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
		Bindings.SetNum(Count);
		SPVRResult = Reflection.EnumerateDescriptorBindings(&Count, Bindings.GetData());
		check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
		if (Count > 0)
		{
			TArray<SpvReflectDescriptorBinding*> UniformBindings;
			TArray<SpvReflectDescriptorBinding*> SamplerBindings;
			TArray<SpvReflectDescriptorBinding*> TextureSRVBindings;
			TArray<SpvReflectDescriptorBinding*> TextureUAVBindings;
			TArray<SpvReflectDescriptorBinding*> TBufferSRVBindings;
			TArray<SpvReflectDescriptorBinding*> TBufferUAVBindings;
			TArray<SpvReflectDescriptorBinding*> SBufferSRVBindings;
			TArray<SpvReflectDescriptorBinding*> SBufferUAVBindings;

			// Extract all the bindings first so that we process them in order - this lets us assign UAVs before other resources
			// Which is necessary to match the D3D binding scheme.
			for (auto const& Binding : Bindings)
			{
				switch (Binding->resource_type)
				{
				case SPV_REFLECT_RESOURCE_FLAG_CBV:
				{
					check(Binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
					if (Binding->accessed)
					{
						UniformBindings.Add(Binding);
					}
					break;
				}
				case SPV_REFLECT_RESOURCE_FLAG_SAMPLER:
				{
					check(Binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER);
					if (Binding->accessed)
					{
						SamplerBindings.Add(Binding);
					}
					break;
				}
				case SPV_REFLECT_RESOURCE_FLAG_SRV:
				{
					switch (Binding->descriptor_type)
					{
					case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
					{
						if (Binding->accessed)
						{
							TextureSRVBindings.Add(Binding);
						}
						break;
					}
					case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
					{
						if (Binding->accessed)
						{
							TBufferSRVBindings.Add(Binding);
						}
						break;
					}
					case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
					{
						if (Binding->accessed)
						{
							SBufferSRVBindings.Add(Binding);
						}
						break;
					}
					default:
					{
						// check(false);
						break;
					}
					}
					break;
				}
				case SPV_REFLECT_RESOURCE_FLAG_UAV:
				{
					if (Binding->uav_counter_binding)
					{
						Counters.Add(Binding->uav_counter_binding);
					}

					switch (Binding->descriptor_type)
					{
					case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
					{
						TextureUAVBindings.Add(Binding);
						break;
					}
					case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
					{
						TBufferUAVBindings.Add(Binding);
						break;
					}
					case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
					{
						if (!Counters.Contains(Binding) || Binding->accessed)
						{
							SBufferUAVBindings.Add(Binding);
						}
						break;
					}
					default:
					{
						// check(false);
						break;
					}
					}
					break;
				}
				default:
				{
					// check(false);
					break;
				}
				}
			}

			for (auto const& Binding : TBufferUAVBindings)
			{
				check(UAVIndices);
				uint32 Index = FPlatformMath::CountTrailingZeros(UAVIndices);

				// UAVs always claim all slots so we don't have conflicts as D3D expects 0-7
				BufferIndices &= ~(1 << Index);
				TextureIndices &= ~(1llu << uint64(Index));
				UAVIndices &= ~(1 << Index);

				UAVString += FString::Printf(TEXT("%s%s(%u:%u)"), UAVString.Len() ? TEXT(",") : TEXT(""), UTF8_TO_TCHAR(Binding->name), Index, 1);

				SPVRResult = Reflection.ChangeDescriptorBindingNumbers(Binding, Index, GlobalSetId);
				check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
			}

			for (auto const& Binding : SBufferUAVBindings)
			{
				check(UAVIndices);
				uint32 Index = FPlatformMath::CountTrailingZeros(UAVIndices);

				// UAVs always claim all slots so we don't have conflicts as D3D expects 0-7
				BufferIndices &= ~(1 << Index);
				TextureIndices &= ~(1llu << uint64(Index));
				UAVIndices &= ~(1 << Index);

				UAVString += FString::Printf(TEXT("%s%s(%u:%u)"), UAVString.Len() ? TEXT(",") : TEXT(""), UTF8_TO_TCHAR(Binding->name), Index, 1);

				SPVRResult = Reflection.ChangeDescriptorBindingNumbers(Binding, Index, GlobalSetId);
				check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
			}

			for (auto const& Binding : TextureUAVBindings)
			{
				check(UAVIndices);
				uint32 Index = FPlatformMath::CountTrailingZeros(UAVIndices);

				// UAVs always claim all slots so we don't have conflicts as D3D expects 0-7
				// For texture2d this allows us to emulate atomics with buffers
				BufferIndices &= ~(1 << Index);
				TextureIndices &= ~(1llu << uint64(Index));
				UAVIndices &= ~(1 << Index);

				UAVString += FString::Printf(TEXT("%s%s(%u:%u)"), UAVString.Len() ? TEXT(",") : TEXT(""), UTF8_TO_TCHAR(Binding->name), Index, 1);

				SPVRResult = Reflection.ChangeDescriptorBindingNumbers(Binding, Index, GlobalSetId);
				check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
			}

			for (auto const& Binding : TBufferSRVBindings)
			{
				check(TextureIndices);
				uint32 Index = FPlatformMath::CountTrailingZeros(TextureIndices);

				// No support for 3-component types in dxc/SPIRV/MSL - need to expose my workarounds there too
				BufferIndices &= ~(1 << Index);
				TextureIndices &= ~(1llu << uint64(Index));

				Textures.Add(UTF8_TO_TCHAR(Binding->name));

				SPVRResult = Reflection.ChangeDescriptorBindingNumbers(Binding, Index, GlobalSetId);
				check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
			}

			for (auto const& Binding : SBufferSRVBindings)
			{
				check(BufferIndices);
				uint32 Index = FPlatformMath::CountTrailingZeros(BufferIndices);

				BufferIndices &= ~(1 << Index);

				SPVRResult = Reflection.ChangeDescriptorBindingNumbers(Binding, Index, GlobalSetId);
				check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
			}

			for (auto const& Binding : UniformBindings)
			{
				check(UBOIndices);
				uint32 Index = FPlatformMath::CountTrailingZeros(UBOIndices);
				UBOIndices &= ~(1 << Index);

				// Global uniform buffer - handled specially as we care about the internal layout
				if (strstr(Binding->name, "$Globals"))
				{
					for (uint32 i = 0; i < Binding->block.member_count; i++)
					{
						SpvReflectBlockVariable& member = Binding->block.members[i];
						uint32 MbrOffset = member.absolute_offset / sizeof(float);
						uint32 MbrSize = member.size / sizeof(float);

						PAKString += FString::Printf(TEXT("%s%s(h:%u,%u)"), PAKString.Len() ? TEXT(",") : TEXT(""), UTF8_TO_TCHAR(member.name), MbrOffset, MbrSize);
					}
				}
				else
				{
					std::string OldName = "type_";
					OldName += Binding->name;
					std::string NewName = FrequencyPrefix;
					NewName += "b";
					NewName += std::to_string(Index);
					UniformVarNames[OldName] = NewName;
					// Regular uniform buffer - we only care about the binding index
					UBOString += FString::Printf(TEXT("%s%s(%u)"), UBOString.Len() ? TEXT(",") : TEXT(""), UTF8_TO_TCHAR(Binding->name), Index);
				}

				SPVRResult = Reflection.ChangeDescriptorBindingNumbers(Binding, Index, GlobalSetId);
				check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
			}

			for (auto const& Binding : TextureSRVBindings)
			{
				check(TextureIndices);
				uint32 Index = FPlatformMath::CountTrailingZeros64(TextureIndices);
				TextureIndices &= ~(1llu << uint64(Index));

				Textures.Add(UTF8_TO_TCHAR(Binding->name));

				SPVRResult = Reflection.ChangeDescriptorBindingNumbers(Binding, Index, GlobalSetId);
				check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
			}

			for (auto const& Binding : SamplerBindings)
			{
				check(SamplerIndices);
				uint32 Index = FPlatformMath::CountTrailingZeros(SamplerIndices);
				SamplerIndices &= ~(1 << Index);

				Samplers.Add(UTF8_TO_TCHAR(Binding->name));

				SPVRResult = Reflection.ChangeDescriptorBindingNumbers(Binding, Index, GlobalSetId);
				check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
			}
		}

		TArray<std::string> GlobalRemap;
		TArray<std::string> GlobalArrays;
		TMap<FString, uint32> GlobalOffsets;

		{
			Count = 0;
			SPVRResult = Reflection.EnumeratePushConstantBlocks(&Count, nullptr);
			check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
			ConstantBindings.SetNum(Count);
			SPVRResult = Reflection.EnumeratePushConstantBlocks(&Count, ConstantBindings.GetData());
			check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
			if (Count > 0)
			{
				for (auto const& Var : ConstantBindings)
				{
					// Global uniform buffer - handled specially as we care about the internal layout
					if (strstr(Var->name, "$Globals"))
					{
						for (uint32 i = 0; i < Var->member_count; i++)
						{
							SpvReflectBlockVariable& member = Var->members[i];
							
							if(!strcmp(member.name, "gl_FragColor") || !strcmp(member.name, "gl_LastFragColorARM") || !strcmp(member.name, "gl_LastFragDepthARM") || !strcmp(member.name, "ARM_shader_framebuffer_fetch") || !strcmp(member.name, "ARM_shader_framebuffer_fetch_depth_stencil"))
							{
								continue;
							}
							auto const type = *member.type_description;

							uint32 MbrOffset = Align(member.absolute_offset / sizeof(float), 4);
							uint32 MbrSize = member.size / sizeof(float);

							FString TypeQualifier;

							uint32_t masked_type = type.type_flags & 0xF;

							switch (masked_type) {
							default: checkf(false, TEXT("unsupported component type %d"), masked_type); break;
							case SPV_REFLECT_TYPE_FLAG_BOOL:
							case SPV_REFLECT_TYPE_FLAG_INT: TypeQualifier = (type.traits.numeric.scalar.signedness ? TEXT("i") : TEXT("u")); break;
							case SPV_REFLECT_TYPE_FLAG_FLOAT: TypeQualifier = (TEXT("h")); break;
							}

							FString MemberName = UTF8_TO_TCHAR(member.name);

							uint32& Offset = GlobalOffsets.FindOrAdd(TypeQualifier);

							PAKString += FString::Printf(TEXT("%s%s(%s:%u,%u)"), PAKString.Len() ? TEXT(",") : TEXT(""), *MemberName, *TypeQualifier, Offset * 4, MbrSize);

							bool const bArray = type.traits.array.dims_count > 0;

							std::string Name = "#define _Globals_";
							std::string OffsetString = std::to_string(Offset);
							Name += member.name;
							if (bArray)
							{
								std::string ArrayName = "_Globals_";
								ArrayName += member.name;
								GlobalArrays.Add(ArrayName);
								Name += "(Offset)";
								if (type.op == SpvOpTypeMatrix)
									OffsetString += " + (Offset * 4)";
								else
									OffsetString += " + Offset";
							}
							Name += " (";
							if (type.op == SpvOpTypeMatrix)
							{
								check(type.traits.numeric.matrix.column_count == 4 && type.traits.numeric.matrix.row_count == 4);
								Name += "mat4(";
								Name += FrequencyPrefix;
								Name += "u_";
								Name += TCHAR_TO_UTF8(*TypeQualifier);
								Name += "[";
								Name += OffsetString;
								Name += "],";

								Name += FrequencyPrefix;
								Name += "u_";
								Name += TCHAR_TO_UTF8(*TypeQualifier);
								Name += "[";
								Name += OffsetString;
								Name += "+ 1],";

								Name += FrequencyPrefix;
								Name += "u_";
								Name += TCHAR_TO_UTF8(*TypeQualifier);
								Name += "[";
								Name += OffsetString;
								Name += " + 2],";

								Name += FrequencyPrefix;
								Name += "u_";
								Name += TCHAR_TO_UTF8(*TypeQualifier);
								Name += "[";
								Name += OffsetString;
								Name += " + 3]";
								Name += ")";
							}
							else
							{
								Name += FrequencyPrefix;
								Name += "u_";
								Name += TCHAR_TO_UTF8(*TypeQualifier);
								Name += "[";
								Name += OffsetString;
								Name += "].";
								switch (type.traits.numeric.vector.component_count)
								{
								case 0:
								case 1:
									Name += "x";
									break;
								case 2:
									Name += "xy";
									break;
								case 3:
									Name += "xyz";
									break;
								case 4:
								default:
									Name += "xyzw";
									break;
								}
							}
							Name += ")\n";

							GlobalRemap.Add(Name);

							Offset += Align(MbrSize, 4) / 4;
						}
					}
				}
			}
		}

		TArray<FString> OutputVarNames;

		{
			Count = 0;
			SPVRResult = Reflection.EnumerateOutputVariables(&Count, nullptr);
			check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
			OutputVars.SetNum(Count);
			SPVRResult = Reflection.EnumerateOutputVariables(&Count, OutputVars.GetData());
			check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
			if (Count > 0)
			{
				uint32 AssignedInputs = 0;

				for (auto const& Var : OutputVars)
				{
					if (Var->storage_class == SpvStorageClassOutput && Var->built_in == -1)
					{
						if (Frequency == HSF_PixelShader && strstr(Var->name, "SV_Target"))
						{
							FString TypeQualifier;

							auto const type = *Var->type_description;
							uint32_t masked_type = type.type_flags & 0xF;

							switch (masked_type) {
							default: checkf(false, TEXT("unsupported component type %d"), masked_type); break;
							case SPV_REFLECT_TYPE_FLAG_BOOL: TypeQualifier = TEXT("b"); break;
							case SPV_REFLECT_TYPE_FLAG_INT: TypeQualifier = (type.traits.numeric.scalar.signedness ? TEXT("i") : TEXT("u")); break;
							case SPV_REFLECT_TYPE_FLAG_FLOAT: TypeQualifier = (type.traits.numeric.scalar.width == 32 ? TEXT("f") : TEXT("h")); break;
							}

							if (type.type_flags & SPV_REFLECT_TYPE_FLAG_MATRIX)
							{
								TypeQualifier += FString::Printf(TEXT("%d%d"), type.traits.numeric.matrix.row_count, type.traits.numeric.matrix.column_count);
							}
							else if (type.type_flags & SPV_REFLECT_TYPE_FLAG_VECTOR)
							{
								TypeQualifier += FString::Printf(TEXT("%d"), type.traits.numeric.vector.component_count);
							}
							else
							{
								TypeQualifier += TEXT("1");
							}

							FString Name = ANSI_TO_TCHAR(Var->name);
							Name.ReplaceInline(TEXT("."), TEXT("_"));
							OutputVarNames.Add(Name);
							OUTString += FString::Printf(TEXT("%s%s:out_Target%d"), OUTString.Len() ? TEXT(",") : TEXT(""), *TypeQualifier, Var->location);
						}
						else
						{
							unsigned Location = Var->location;
							unsigned SemanticIndex = Location;
							check(Var->semantic);
							unsigned i = (unsigned)strlen(Var->semantic);
							check(i);
							while (isdigit((unsigned char)(Var->semantic[i - 1])))
							{
								i--;
							}
							if (i < strlen(Var->semantic))
							{
								SemanticIndex = (unsigned)atoi(Var->semantic + i);
								if (Location != SemanticIndex)
								{
									Location = SemanticIndex;
								}
							}

							while ((1 << Location) & AssignedInputs)
							{
								Location++;
							}

							if (Location != Var->location)
							{
								SPVRResult = Reflection.ChangeOutputVariableLocation(Var, Location);
								check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
							}

							uint32 ArrayCount = 1;
							for (uint32 Dim = 0; Dim < Var->array.dims_count; Dim++)
							{
								ArrayCount *= Var->array.dims[Dim];
							}

							FString TypeQualifier;

							auto const type = *Var->type_description;
							uint32_t masked_type = type.type_flags & 0xF;

							switch (masked_type) {
							default: checkf(false, TEXT("unsupported component type %d"), masked_type); break;
							case SPV_REFLECT_TYPE_FLAG_BOOL: TypeQualifier = TEXT("b"); break;
							case SPV_REFLECT_TYPE_FLAG_INT: TypeQualifier = (type.traits.numeric.scalar.signedness ? TEXT("i") : TEXT("u")); break;
							case SPV_REFLECT_TYPE_FLAG_FLOAT: TypeQualifier = (type.traits.numeric.scalar.width == 32 ? TEXT("f") : TEXT("h")); break;
							}

							if (type.type_flags & SPV_REFLECT_TYPE_FLAG_MATRIX)
							{
								TypeQualifier += FString::Printf(TEXT("%d%d"), type.traits.numeric.matrix.row_count, type.traits.numeric.matrix.column_count);
							}
							else if (type.type_flags & SPV_REFLECT_TYPE_FLAG_VECTOR)
							{
								TypeQualifier += FString::Printf(TEXT("%d"), type.traits.numeric.vector.component_count);
							}
							else
							{
								TypeQualifier += TEXT("1");
							}

							for (uint32 j = 0; j < ArrayCount; j++)
							{
								AssignedInputs |= (1 << (Location + j));
							}

							FString Name = ANSI_TO_TCHAR(Var->name);
							Name.ReplaceInline(TEXT("."), TEXT("_"));
							OUTString += FString::Printf(TEXT("%s%s;%d:%s"), OUTString.Len() ? TEXT(",") : TEXT(""), *TypeQualifier, Location, *Name);
						}
					}
				}
			}
		}

		TArray<FString> InputVarNames;

		{
			Count = 0;
			SPVRResult = Reflection.EnumerateInputVariables(&Count, nullptr);
			check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
			InputVars.SetNum(Count);
			SPVRResult = Reflection.EnumerateInputVariables(&Count, InputVars.GetData());
			check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
			if (Count > 0)
			{
				uint32 AssignedInputs = 0;

				for (auto const& Var : InputVars)
				{
					if (Var->storage_class == SpvStorageClassInput && Var->built_in == -1)
					{
						unsigned Location = Var->location;
						unsigned SemanticIndex = Location;
						check(Var->semantic);
						unsigned i = (unsigned)strlen(Var->semantic);
						check(i);
						while (isdigit((unsigned char)(Var->semantic[i - 1])))
						{
							i--;
						}
						if (i < strlen(Var->semantic))
						{
							SemanticIndex = (unsigned)atoi(Var->semantic + i);
							if (Location != SemanticIndex)
							{
								Location = SemanticIndex;
							}
						}

						while ((1 << Location) & AssignedInputs)
						{
							Location++;
						}

						if (Location != Var->location)
						{
							SPVRResult = Reflection.ChangeInputVariableLocation(Var, Location);
							check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
						}

						uint32 ArrayCount = 1;
						for (uint32 Dim = 0; Dim < Var->array.dims_count; Dim++)
						{
							ArrayCount *= Var->array.dims[Dim];
						}

						FString TypeQualifier;

						auto const type = *Var->type_description;
						uint32_t masked_type = type.type_flags & 0xF;

						switch (masked_type) {
						default: checkf(false, TEXT("unsupported component type %d"), masked_type); break;
						case SPV_REFLECT_TYPE_FLAG_BOOL: TypeQualifier = TEXT("b"); break;
						case SPV_REFLECT_TYPE_FLAG_INT: TypeQualifier = (type.traits.numeric.scalar.signedness ? TEXT("i") : TEXT("u")); break;
						case SPV_REFLECT_TYPE_FLAG_FLOAT: TypeQualifier = (type.traits.numeric.scalar.width == 32 ? TEXT("f") : TEXT("h")); break;
						}

						if (type.type_flags & SPV_REFLECT_TYPE_FLAG_MATRIX)
						{
							TypeQualifier += FString::Printf(TEXT("%d%d"), type.traits.numeric.matrix.row_count, type.traits.numeric.matrix.column_count);
						}
						else if (type.type_flags & SPV_REFLECT_TYPE_FLAG_VECTOR)
						{
							TypeQualifier += FString::Printf(TEXT("%d"), type.traits.numeric.vector.component_count);
						}
						else
						{
							TypeQualifier += TEXT("1");
						}

						for (uint32 j = 0; j < ArrayCount; j++)
						{
							AssignedInputs |= (1 << (Location + j));
						}

						FString Name = ANSI_TO_TCHAR(Var->name);
						Name.ReplaceInline(TEXT("."), TEXT("_"));
						InputVarNames.Add(Name);
						INPString += FString::Printf(TEXT("%s%s;%d:%s"), INPString.Len() ? TEXT(",") : TEXT(""), *TypeQualifier, Location, *Name);
					}
				}
			}
		}


		ShaderConductor::Blob* OldData = SpirvResults.target;
		SpirvResults.target = ShaderConductor::CreateBlob(Reflection.GetCode(), Reflection.GetCodeSize());
		ShaderConductor::DestroyBlob(OldData);

		if (bDumpDebugInfo)
		{
			const FString GLSLFile = (Input.DumpDebugInfoPath / TEXT("Output.spirv"));

			size_t GlslSourceLen = SpirvResults.target->Size();
			const char* GlslSource = (char const*)SpirvResults.target->Data();
			if (GlslSourceLen > 0)
			{
				FArchive* FileWriter = IFileManager::Get().CreateFileWriter(*GLSLFile);
				if (FileWriter)
				{
					FileWriter->Serialize(const_cast<char*>(GlslSource), GlslSourceLen);
					FileWriter->Close();
					delete FileWriter;
				}
			}
		}

		switch (Version)
		{
		case GLSL_150_ES2:	// ES2 Emulation
		case GLSL_150_ES2_NOUB:	// ES2 Emulation with NoUBs
		case GLSL_150_ES3_1:	// ES3.1 Emulation
		case GLSL_150:
			TargetDesc.version = "330";
			TargetDesc.language = ShaderConductor::ShadingLanguage::Glsl;
			break;
		case GLSL_SWITCH_FORWARD:
			TargetDesc.version = "320";
			TargetDesc.language = ShaderConductor::ShadingLanguage::Essl;
			break;
		case GLSL_430:
		case GLSL_SWITCH:
			TargetDesc.version = "430";
			TargetDesc.language = ShaderConductor::ShadingLanguage::Glsl;
			break;
		case GLSL_ES2:
		case GLSL_ES2_WEBGL:
			TargetDesc.version = "210";
			TargetDesc.language = ShaderConductor::ShadingLanguage::Essl;
			break;
		case GLSL_310_ES_EXT:
		case GLSL_ES3_1_ANDROID:
		default:
			TargetDesc.version = "310";
			TargetDesc.language = ShaderConductor::ShadingLanguage::Essl;
			break;
		}


		TargetDesc.options = nullptr;
		TargetDesc.numOptions = 0;
		TSet<FString> ExternalTextures;
		int32 Pos = 0;
#if !PLATFORM_MAC
		TCHAR TextureExternalName[256];
#else
		ANSICHAR TextureExternalName[256];
#endif
		do
		{
			Pos = PreprocessedShader.Find(TEXT("TextureExternal"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos + 15);
			if (Pos != INDEX_NONE)
			{
#if !PLATFORM_WINDOWS
	#if !PLATFORM_MAC
				if (swscanf(&PreprocessedShader[Pos], TEXT("TextureExternal %ls"), TextureExternalName) == 1)
	#else
				if (sscanf(TCHAR_TO_ANSI(&PreprocessedShader[Pos]), "TextureExternal %s", TextureExternalName) == 1)
	#endif
#else
				if (swscanf_s(&PreprocessedShader[Pos], TEXT("TextureExternal %ls"), TextureExternalName, 256) == 1)
#endif
				{
					FString Name = TextureExternalName;
					if (Name.RemoveFromEnd(TEXT(";")))
					{
						ExternalTextures.Add(TEXT("SPIRV_Cross_Combined") + Name);
					}
				}
			}
		} while (Pos != INDEX_NONE);

		std::function<ShaderConductor::Blob*(char const*, char const*)> variableTypeRenameCallback([&ExternalTextures](char const* variableName, char const* typeName)
		{
			ShaderConductor::Blob* Blob = nullptr;
			for (FString const& ExternalTex : ExternalTextures)
			{
				if (FCStringWide::Strncmp(ANSI_TO_TCHAR(variableName), *ExternalTex, ExternalTex.Len()) == 0)
				{
					static ANSICHAR const* ExternalTexType = "samplerExternalOES";
					static SIZE_T ExternalTypeLen = FCStringAnsi::Strlen(ExternalTexType) + 1;
					Blob = ShaderConductor::CreateBlob(ExternalTexType, ExternalTypeLen);
					break;
				}
			}
			return Blob;
		});

		TargetDesc.variableTypeRenameCallback = variableTypeRenameCallback;
		ShaderConductor::Compiler::ResultDesc GlslResult = ShaderConductor::Compiler::ConvertBinary(SpirvResults, SourceDesc, TargetDesc);
		ShaderConductor::DestroyBlob(SpirvResults.target);

		if (GlslResult.hasError && GlslResult.errorWarningMsg)
		{
			if (ErrorLog)
				free(ErrorLog);

			std::string ErrorText((const char*)GlslResult.errorWarningMsg->Data(), GlslResult.errorWarningMsg->Size());
#if !PLATFORM_WINDOWS
			ErrorLog = strdup(ErrorText.c_str());
#else
			ErrorLog = _strdup(ErrorText.c_str());
#endif
			ShaderConductor::DestroyBlob(GlslResult.errorWarningMsg);
		}
		else if (!GlslResult.hasError)
		{
			Result = 1;

			std::string GlslSource((const char*)GlslResult.target->Data(), GlslResult.target->Size());

			std::string LayoutString = "#extension ";
			size_t LayoutPos = GlslSource.find(LayoutString);
			if (LayoutPos != std::string::npos)
			{
				for (FString Name : InputVarNames)
				{
					std::string DefineString = "#define ";
					DefineString += TCHAR_TO_ANSI(*Name);
					DefineString += " ";
					DefineString += TCHAR_TO_ANSI(*Name.Replace(TEXT("in_var_"), TEXT("in_")));
					DefineString += "\n";

					GlslSource.insert(LayoutPos, DefineString);
				}
				for (FString Name : OutputVarNames)
				{
					std::string DefineString = "#define ";
					DefineString += TCHAR_TO_ANSI(*Name);
					DefineString += " ";
					DefineString += TCHAR_TO_ANSI(*Name.Replace(TEXT("out_var_SV_"), TEXT("out_")));
					DefineString += "\n";

					GlslSource.insert(LayoutPos, DefineString);
				}
				for (auto const& Pair : UniformVarNames)
				{
					std::string DefineString = "#define ";
					DefineString += Pair.first;
					DefineString += " ";
					DefineString += Pair.second;
					DefineString += "\n";

					GlslSource.insert(LayoutPos, DefineString);
				}
			}

			std::string LocationString = "layout(location = ";
			size_t LocationPos = 0;
			do
			{
				LocationPos = GlslSource.find(LocationString, LocationPos);
				if (LocationPos != std::string::npos)
					GlslSource.replace(LocationPos, LocationString.length(), "INTERFACE_LOCATION(");
			} while (LocationPos != std::string::npos);

			std::string GlobalsSearchString = "uniform type_Globals _Globals;";
			std::string GlobalsString = "//";

			size_t GlobalPos = GlslSource.find(GlobalsSearchString);
			if (GlobalPos != std::string::npos)
			{
				GlslSource.insert(GlobalPos, GlobalsString);
				
				bool UsesFramebufferFetch = Frequency == HSF_PixelShader && GlslSource.find("_Globals.ARM_shader_framebuffer_fetch") != std::string::npos;

				std::string GlobalVarString = "_Globals.";
				size_t GlobalVarPos = 0;
				do
				{
					GlobalVarPos = GlslSource.find(GlobalVarString, GlobalVarPos);
					if (GlobalVarPos != std::string::npos)
					{
						GlslSource.replace(GlobalVarPos, GlobalVarString.length(), "_Globals_");
						for (std::string const& SearchString : GlobalArrays)
						{
							if (!GlslSource.compare(GlobalVarPos, SearchString.length(), SearchString))
							{
								GlslSource.replace(GlobalVarPos + SearchString.length(), 1, "(");

								size_t ClosingBrace = GlslSource.find("]", GlobalVarPos + SearchString.length());
								if (ClosingBrace != std::string::npos)
									GlslSource.replace(ClosingBrace, 1, ")");
							}
						}
					}
				} while (GlobalVarPos != std::string::npos);

				for (auto const& Pair : GlobalOffsets)
				{
					if (Pair.Value > 0)
					{
						std::string NewUniforms;
						if (Pair.Key == TEXT("u"))
						{
							NewUniforms = "uniform uvec4 ";
							NewUniforms += FrequencyPrefix;
							NewUniforms += "u_u[";
							NewUniforms += std::to_string(Pair.Value);
							NewUniforms += "];\n";
						}
						else if (Pair.Key == TEXT("i"))
						{
							NewUniforms = "uniform ivec4 ";
							NewUniforms += FrequencyPrefix;
							NewUniforms += "u_i[";
							NewUniforms += std::to_string(Pair.Value);
							NewUniforms += "];\n";
						}
						else if (Pair.Key == TEXT("h"))
						{
							NewUniforms = "uniform vec4 ";
							NewUniforms += FrequencyPrefix;
							NewUniforms += "u_h[";
							NewUniforms += std::to_string(Pair.Value);
							NewUniforms += "];\n";
						}
						GlslSource.insert(GlobalPos, NewUniforms);
					}
				}

				for (std::string const& Define : GlobalRemap)
				{
					GlslSource.insert(GlobalPos, Define);
				}
				
				if (UsesFramebufferFetch)
				{
					size_t MainPos = GlslSource.find("struct type_Globals");
					if (MainPos != std::string::npos)
						GlslSource.insert(MainPos, FrameBufferDefines);
					
					size_t OutColor = GlslSource.find("0) out ");
					if (OutColor != std::string::npos)
						GlslSource.replace(OutColor, 7, "0) FRAME_BUFFERFETCH_STORAGE_QUALIFIER ");
				}
			}

			size_t GlslSourceLen = GlslSource.length();
			if (GlslSourceLen > 0)
			{
				size_t SamplerPos = GlslSource.find("\nuniform ");

				uint32 TextureIndex = 0;
				for (FString& Texture : Textures)
				{
					TArray<FString> UsedSamplers;
					FString SamplerString;
					for (FString& Sampler : Samplers)
					{
						std::string SamplerName = TCHAR_TO_ANSI(*(Texture + Sampler));
						size_t FindCombinedSampler = GlslSource.find(SamplerName.c_str());
						if (FindCombinedSampler != std::string::npos)
						{
							uint32 NewIndex = TextureIndex + UsedSamplers.Num();
							std::string NewDefine = "#define SPIRV_Cross_Combined";
							NewDefine += SamplerName;
							NewDefine += " ";
							NewDefine += FrequencyPrefix;
							NewDefine += "s";
							NewDefine += std::to_string(NewIndex);
							NewDefine += "\n";
							GlslSource.insert(SamplerPos+1, NewDefine);

							UsedSamplers.Add(Sampler);
							SamplerString += FString::Printf(TEXT("%s%s"), SamplerString.Len() ? TEXT(",") : TEXT(""), *Sampler);
						}
					}
					if (UsedSamplers.Num() > 0)
					{
						SRVString += FString::Printf(TEXT("%s%s(%u:%u[%s])"), SRVString.Len() ? TEXT(",") : TEXT(""), *Texture, TextureIndex, UsedSamplers.Num(), *SamplerString);
						TextureIndex += UsedSamplers.Num();
					}
					else
					{
						SRVString += FString::Printf(TEXT("%s%s(%u:%u)"), SRVString.Len() ? TEXT(",") : TEXT(""), *Texture, TextureIndex++, 1);
					}
				}

				MetaData += TEXT("// Compiled by ShaderConductor\n");
				if (INPString.Len())
				{
					MetaData += FString::Printf(TEXT("// @Inputs: %s\n"), *INPString);
				}
				if (OUTString.Len())
				{
					MetaData += FString::Printf(TEXT("// @Outputs: %s\n"), *OUTString);
				}
				if (UBOString.Len())
				{
					MetaData += FString::Printf(TEXT("// @UniformBlocks: %s\n"), *UBOString);
				}
				if (PAKString.Len())
				{
					MetaData += FString::Printf(TEXT("// @PackedGlobals: %s\n"), *PAKString);
				}
				if (SRVString.Len())
				{
					MetaData += FString::Printf(TEXT("// @Samplers: %s\n"), *SRVString);
				}
				if (UAVString.Len())
				{
					MetaData += FString::Printf(TEXT("// @UAVs: %s\n"), *UAVString);
				}
				if (SMPString.Len())
				{
					MetaData += FString::Printf(TEXT("// @SamplerStates: %s\n"), *SMPString);
				}

				GlslSourceLen = GlslSource.length();

				uint32 Len = FCStringAnsi::Strlen(TCHAR_TO_ANSI(*MetaData)) + GlslSourceLen + 1;
				GlslShaderSource = (char*)malloc(Len);
				FCStringAnsi::Snprintf(GlslShaderSource, Len, "%s%s", (const char*)TCHAR_TO_ANSI(*MetaData), (const char*)GlslSource.c_str());
			}

			ShaderConductor::DestroyBlob(GlslResult.target);
		}
	}
}
#endif	// USE_DXC

/**
 * Compile a shader for OpenGL on Windows.
 * @param Input - The input shader code and environment.
 * @param Output - Contains shader compilation results upon return.
 */
void FOpenGLFrontend::CompileShader(const FShaderCompilerInput& Input,FShaderCompilerOutput& Output,const FString& WorkingDirectory, GLSLVersion Version)
{
	FString PreprocessedShader;
	FShaderCompilerDefinitions AdditionalDefines;
	EHlslCompileTarget HlslCompilerTarget = HCT_InvalidTarget;
	ECompilerFlags PlatformFlowControl = CFLAG_AvoidFlowControl;

	const bool bCompileES2With310 = (Version == GLSL_ES2 && Input.Environment.CompilerFlags.Contains(CFLAG_FeatureLevelES31));
	if (bCompileES2With310)
	{
		Version = GLSL_ES3_1_ANDROID;
	}

	// set up compiler env based on version
	SetupPerVersionCompilationEnvironment(Version, AdditionalDefines, HlslCompilerTarget);

	bool const bUseSC = Input.Environment.CompilerFlags.Contains(CFLAG_ForceDXC);
	AdditionalDefines.SetDefine(TEXT("COMPILER_HLSLCC"), bUseSC ? 2 : 1);

	const bool bDumpDebugInfo = (Input.DumpDebugInfoPath != TEXT("") && IFileManager::Get().DirectoryExists(*Input.DumpDebugInfoPath));

	if(Input.Environment.CompilerFlags.Contains(CFLAG_AvoidFlowControl) || PlatformFlowControl == CFLAG_AvoidFlowControl)
	{
		AdditionalDefines.SetDefine(TEXT("COMPILER_SUPPORTS_ATTRIBUTES"), (uint32)1);
	}
	else
	{
		AdditionalDefines.SetDefine(TEXT("COMPILER_SUPPORTS_ATTRIBUTES"), (uint32)0);
	}

	if (Input.Environment.CompilerFlags.Contains(CFLAG_UseFullPrecisionInPS))
	{
		AdditionalDefines.SetDefine(TEXT("FORCE_FLOATS"), (uint32)1);
	}

	if (Input.bSkipPreprocessedCache)
	{
		if (!FFileHelper::LoadFileToString(PreprocessedShader, *Input.VirtualSourceFilePath))
		{
			return;
		}

		// Remove const as we are on debug-only mode
		CrossCompiler::CreateEnvironmentFromResourceTable(PreprocessedShader, (FShaderCompilerEnvironment&)Input.Environment);
	}
	else
	{
		if (!PreprocessShader(PreprocessedShader, Output, Input, AdditionalDefines))
		{
			// The preprocessing stage will add any relevant errors.
			return;
		}
	}

	char* GlslShaderSource = NULL;
	char* ErrorLog = NULL;
	int32 Result = 0;
	

	const bool bIsSM5 = IsSM5(Version);

	const EHlslShaderFrequency FrequencyTable[] =
	{
		HSF_VertexShader,
		bIsSM5 ? HSF_HullShader : HSF_InvalidFrequency,
		bIsSM5 ? HSF_DomainShader : HSF_InvalidFrequency,
		HSF_PixelShader,
		IsES2Platform(Version) ? HSF_InvalidFrequency : HSF_GeometryShader,
		RHISupportsComputeShaders(Input.Target.GetPlatform()) ? HSF_ComputeShader : HSF_InvalidFrequency
	};

	const EHlslShaderFrequency Frequency = FrequencyTable[Input.Target.Frequency];
	if (Frequency == HSF_InvalidFrequency)
	{
		Output.bSucceeded = false;
		FShaderCompilerError* NewError = new(Output.Errors) FShaderCompilerError();
		NewError->StrippedErrorMessage = FString::Printf(
			TEXT("%s shaders not supported for use in OpenGL."),
			CrossCompiler::GetFrequencyName((EShaderFrequency)Input.Target.Frequency)
			);
		return;
	}

	// This requires removing the HLSLCC_NoPreprocess flag later on!
	RemoveUniformBuffersFromSource(Input.Environment, PreprocessedShader);

	uint32 CCFlags = CalculateCrossCompilerFlags(Version, Input.Environment.CompilerFlags);

	// Required as we added the RemoveUniformBuffersFromSource() function (the cross-compiler won't be able to interpret comments w/o a preprocessor)
	CCFlags &= ~HLSLCC_NoPreprocess;

#if USE_DXC
	if (bUseSC)
	{
		CompileShaderDXC(Input, Output, WorkingDirectory, Version, Frequency, CCFlags, PreprocessedShader, Result, GlslShaderSource, ErrorLog);
	}
	else
#endif
	{
		// Write out the preprocessed file and a batch file to compile it if requested (DumpDebugInfoPath is valid)
		if (bDumpDebugInfo)
		{
			FArchive* FileWriter = IFileManager::Get().CreateFileWriter(*(Input.DumpDebugInfoPath / Input.GetSourceFilename()));
			if (FileWriter)
			{
				auto AnsiSourceFile = StringCast<ANSICHAR>(*PreprocessedShader);
				FileWriter->Serialize((ANSICHAR*)AnsiSourceFile.Get(), AnsiSourceFile.Length());
				{
					FString Line = CrossCompiler::CreateResourceTableFromEnvironment(Input.Environment);

					Line += TEXT("#if 0 /*DIRECT COMPILE*/\n");
					Line += CreateShaderCompilerWorkerDirectCommandLine(Input, CCFlags);
					Line += TEXT("\n#endif /*DIRECT COMPILE*/\n");

					FileWriter->Serialize(TCHAR_TO_ANSI(*Line), Line.Len());
				}
				FileWriter->Close();
				delete FileWriter;
			}

			if (Input.bGenerateDirectCompileFile)
			{
				FFileHelper::SaveStringToFile(CreateShaderCompilerWorkerDirectCommandLine(Input), *(Input.DumpDebugInfoPath / TEXT("DirectCompile.txt")));
			}
		}

		FGlslCodeBackend* BackEnd = CreateBackend(Version, CCFlags, HlslCompilerTarget);
		FGlslLanguageSpec* LanguageSpec = CreateLanguageSpec(Version);

		FHlslCrossCompilerContext CrossCompilerContext(CCFlags, Frequency, HlslCompilerTarget);
		if (CrossCompilerContext.Init(TCHAR_TO_ANSI(*Input.VirtualSourceFilePath), LanguageSpec))
		{
			Result = CrossCompilerContext.Run(
				TCHAR_TO_ANSI(*PreprocessedShader),
				TCHAR_TO_ANSI(*Input.EntryPointName),
				BackEnd,
				&GlslShaderSource,
				&ErrorLog
				) ? 1 : 0;
		}

		delete BackEnd;
		delete LanguageSpec;
	}

	if (Result != 0)
	{
		int32 GlslSourceLen = GlslShaderSource ? FCStringAnsi::Strlen(GlslShaderSource) : 0;
		if (bDumpDebugInfo)
		{
			const FString GLSLFile = (Input.DumpDebugInfoPath / TEXT("Output.glsl"));
			const FString GLBatchFileContents = CreateCommandLineGLSLES2(GLSLFile, (Input.DumpDebugInfoPath / TEXT("Output.asm")), Version, Frequency, false);
			if (!GLBatchFileContents.IsEmpty())
			{
				FFileHelper::SaveStringToFile(GLBatchFileContents, *(Input.DumpDebugInfoPath / TEXT("GLSLCompile.bat")));
			}

			const FString NDABatchFileContents = CreateCommandLineGLSLES2(GLSLFile, (Input.DumpDebugInfoPath / TEXT("Output.asm")), Version, Frequency, true);
			if (!NDABatchFileContents.IsEmpty())
			{
				FFileHelper::SaveStringToFile(NDABatchFileContents, *(Input.DumpDebugInfoPath / TEXT("NDAGLSLCompile.bat")));
			}

			if (GlslSourceLen > 0)
			{
				uint32 Len = FCStringAnsi::Strlen(TCHAR_TO_ANSI(*Input.VirtualSourceFilePath)) + FCStringAnsi::Strlen(TCHAR_TO_ANSI(*Input.EntryPointName)) + FCStringAnsi::Strlen(GlslShaderSource) + 20;
				char* Dest = (char*)malloc(Len);
				FCStringAnsi::Snprintf(Dest, Len, "// ! %s:%s\n%s", (const char*)TCHAR_TO_ANSI(*Input.VirtualSourceFilePath), (const char*)TCHAR_TO_ANSI(*Input.EntryPointName), (const char*)GlslShaderSource);
				free(GlslShaderSource);
				GlslShaderSource = Dest;
				GlslSourceLen = FCStringAnsi::Strlen(GlslShaderSource);

				FArchive* FileWriter = IFileManager::Get().CreateFileWriter(*GLSLFile);
				if (FileWriter)
				{
					FileWriter->Serialize(GlslShaderSource,GlslSourceLen+1);
					FileWriter->Close();
					delete FileWriter;
				}
			}
		}

#if VALIDATE_GLSL_WITH_DRIVER
		PrecompileShader(Output, Input, GlslShaderSource, Version, Frequency);
#else // VALIDATE_GLSL_WITH_DRIVER
		int32 SourceLen = FCStringAnsi::Strlen(GlslShaderSource);
		Output.Target = Input.Target;
		BuildShaderOutput(Output, Input, GlslShaderSource, SourceLen, Version);
#endif // VALIDATE_GLSL_WITH_DRIVER
	}
	else
	{
		if (bDumpDebugInfo)
		{
			// Generate the batch file to help track down cross-compiler issues if necessary
			const FString GLSLFile = (Input.DumpDebugInfoPath / TEXT("Output.glsl"));
			const FString GLBatchFileContents = CreateCommandLineGLSLES2(GLSLFile, (Input.DumpDebugInfoPath / TEXT("Output.asm")), Version, Frequency, false);
			if (!GLBatchFileContents.IsEmpty())
			{
				FFileHelper::SaveStringToFile(GLBatchFileContents, *(Input.DumpDebugInfoPath / TEXT("GLSLCompile.bat")));
			}
		}

		FString Tmp = ANSI_TO_TCHAR(ErrorLog);
		TArray<FString> ErrorLines;
		Tmp.ParseIntoArray(ErrorLines, TEXT("\n"), true);
		for (int32 LineIndex = 0; LineIndex < ErrorLines.Num(); ++LineIndex)
		{
			const FString& Line = ErrorLines[LineIndex];
			CrossCompiler::ParseHlslccError(Output.Errors, Line);
		}
	}

	if (GlslShaderSource)
	{
		free(GlslShaderSource);
	}
	if (ErrorLog)
	{
		free(ErrorLog);
	}
}

enum class EPlatformType
{
	Android,
	IOS,
	Web,
	Desktop
};

struct FDeviceCapabilities
{
	EPlatformType TargetPlatform = EPlatformType::Android;
	bool bUseES30ShadingLanguage;
	bool bSupportsSeparateShaderObjects;
	bool bRequiresUEShaderFramebufferFetchDef;
	bool bRequiresDontEmitPrecisionForTextureSamplers;
	bool bSupportsShaderTextureLod;
	bool bSupportsShaderTextureCubeLod;
	bool bRequiresTextureCubeLodEXTToTextureCubeLodDefine;
};

void FOpenGLFrontend::FillDeviceCapsOfflineCompilation(struct FDeviceCapabilities& Capabilities, const GLSLVersion ShaderVersion) const
{
	FMemory::Memzero(Capabilities);

	if (ShaderVersion == GLSL_ES2 || ShaderVersion == GLSL_ES3_1_ANDROID)
	{
		Capabilities.TargetPlatform = EPlatformType::Android;

		Capabilities.bUseES30ShadingLanguage = ShaderVersion == GLSL_ES3_1_ANDROID;
		Capabilities.bRequiresUEShaderFramebufferFetchDef = true;
		Capabilities.bRequiresDontEmitPrecisionForTextureSamplers = false;
	}
	else if (ShaderVersion == GLSL_ES2_WEBGL)
	{
		Capabilities.TargetPlatform = EPlatformType::Web;
		Capabilities.bUseES30ShadingLanguage = false; // make sure this matches HTML5OpenGL.h -> FOpenGL::UseES30ShadingLanguage();
	}
	else
	{
		Capabilities.TargetPlatform = EPlatformType::Desktop;

		Capabilities.bSupportsSeparateShaderObjects = true;
	}

	Capabilities.bSupportsShaderTextureLod = true;
	Capabilities.bSupportsShaderTextureCubeLod = false;
	Capabilities.bRequiresTextureCubeLodEXTToTextureCubeLodDefine = true;
}

static bool MoveHashLines(FString& Destination, FString &Source)
{
	int32 Index = 0;
	int32 LineStart = 0;

	bool bFound = false;
	while (Index != INDEX_NONE && !bFound)
	{
		LineStart = Index;
		Index = Source.Find(TEXT("\n"), ESearchCase::IgnoreCase, ESearchDir::FromStart, Index);

		for (int32 i = LineStart; i < Index; ++i)
		{
			const auto CharValue = Source[i];
			if (CharValue == '#')
			{
				break;
			}
			else if (!FChar::IsWhitespace(CharValue))
			{
				bFound = true;
				break;
			}
		}

		++Index;
	}

	if (bFound)
	{
		Destination.Append(Source.Left(LineStart));
		Source.RemoveAt(0, LineStart);
	}

	return bFound;
}

static bool OpenGLShaderPlatformNeedsBindLocation(const GLSLVersion InShaderPlatform)
{
	switch (InShaderPlatform)
	{
		case GLSL_430:
		case GLSL_310_ES_EXT:
		case GLSL_ES3_1_ANDROID:
		case GLSL_150_ES3_1:
			return false;

		case GLSL_150:
		case GLSL_150_ES2:
		case GLSL_ES2:
		case GLSL_150_ES2_NOUB:
		case GLSL_ES2_WEBGL:
			return true;

		default:
			return true;
		break;
	}
}

inline bool OpenGLShaderPlatformSeparable(const GLSLVersion InShaderPlatform)
{
	switch (InShaderPlatform)
	{
		case GLSL_430:
		case GLSL_150:
		case GLSL_150_ES2:
		case GLSL_150_ES2_NOUB:
		case GLSL_150_ES3_1:
			return true;

		case GLSL_310_ES_EXT:
		case GLSL_ES3_1_ANDROID:
		case GLSL_ES2:
		case GLSL_ES2_WEBGL:
			return false;

		default:
			return true;
		break;
	}
}

TSharedPtr<ANSICHAR> FOpenGLFrontend::PrepareCodeForOfflineCompilation(const GLSLVersion ShaderVersion, EShaderFrequency Frequency, const ANSICHAR* InShaderSource) const
{
	FString OriginalShaderSource(ANSI_TO_TCHAR(InShaderSource));
	FString StrOutSource;

	FDeviceCapabilities Capabilities;
	FillDeviceCapsOfflineCompilation(Capabilities, ShaderVersion);

	// Whether shader was compiled for ES 3.1
	const TCHAR *ES310Version = TEXT("#version 310 es");
	const bool bES31 = OriginalShaderSource.Find(ES310Version) != INDEX_NONE;

	// Whether we need to emit mobile multi-view code or not.
	const bool bEmitMobileMultiView = OriginalShaderSource.Find(TEXT("gl_ViewID_OVR")) != INDEX_NONE;

	// Whether we need to emit texture external code or not.
	const bool bEmitTextureExternal = OriginalShaderSource.Find(TEXT("samplerExternalOES")) != INDEX_NONE;

	const bool bUseES30ShadingLanguage = Capabilities.bUseES30ShadingLanguage;

	bool bNeedsExtDrawInstancedDefine = false;
	if (Capabilities.TargetPlatform == EPlatformType::Android || Capabilities.TargetPlatform == EPlatformType::Web)
	{
		bNeedsExtDrawInstancedDefine = !bES31;
		if (bES31)
		{
			StrOutSource.Append(ES310Version);
			StrOutSource.Append(TEXT("\n"));
			OriginalShaderSource.RemoveFromStart(ES310Version);
		}
		else if (IsES2Platform(ShaderVersion))
		{
			StrOutSource.Append(TEXT("#version 100\n"));
			OriginalShaderSource.RemoveFromStart(TEXT("#version 100"));
		}
	}
	else if (Capabilities.TargetPlatform == EPlatformType::IOS)
	{
		bNeedsExtDrawInstancedDefine = true;
		StrOutSource.Append(TEXT("#version 100\n"));
		OriginalShaderSource.RemoveFromStart(TEXT("#version 100"));
	}

	if (bNeedsExtDrawInstancedDefine)
	{
		// Check for the GL_EXT_draw_instanced extension if necessary (version < 300)
		StrOutSource.Append(TEXT("#ifdef GL_EXT_draw_instanced\n"));
		StrOutSource.Append(TEXT("#define UE_EXT_draw_instanced 1\n"));
		StrOutSource.Append(TEXT("#endif\n"));
	}

	const GLenum TypeEnum = GLFrequencyTable[Frequency];
	// The incoming glsl may have preprocessor code that is dependent on defines introduced via the engine.
	// This is the place to insert such engine preprocessor defines, immediately after the glsl version declaration.
	if (Capabilities.bRequiresUEShaderFramebufferFetchDef && TypeEnum == GL_FRAGMENT_SHADER)
	{
		// Some devices (Zenfone5) support GL_EXT_shader_framebuffer_fetch but do not define GL_EXT_shader_framebuffer_fetch in GLSL compiler
		// We can't define anything with GL_, so we use UE_EXT_shader_framebuffer_fetch to enable frame buffer fetch
		StrOutSource.Append(TEXT("#define UE_EXT_shader_framebuffer_fetch 1\n"));
	}

	if (bEmitMobileMultiView)
	{
		MoveHashLines(StrOutSource, OriginalShaderSource);

		StrOutSource.Append(TEXT("\n\n"));
		StrOutSource.Append(TEXT("#extension GL_OVR_multiview2 : enable\n"));
		StrOutSource.Append(TEXT("\n\n"));
	}

	if (bEmitTextureExternal)
	{
		MoveHashLines(StrOutSource, OriginalShaderSource);
		StrOutSource.Append(TEXT("#define samplerExternalOES sampler2D\n"));
	}

	// Only desktop with separable shader platform can use GL_ARB_separate_shader_objects for reduced shader compile/link hitches
	// however ES3.1 relies on layout(location=) support
	bool const bNeedsBindLocation = OpenGLShaderPlatformNeedsBindLocation(ShaderVersion) && !bES31;
	if (OpenGLShaderPlatformSeparable(ShaderVersion) || !bNeedsBindLocation)
	{
		// Move version tag & extensions before beginning all other operations
		MoveHashLines(StrOutSource, OriginalShaderSource);

		// OpenGL SM5 shader platforms require location declarations for the layout, but don't necessarily use SSOs
		if (Capabilities.bSupportsSeparateShaderObjects || !bNeedsBindLocation)
		{
			if (Capabilities.TargetPlatform == EPlatformType::Desktop)
			{
				StrOutSource.Append(TEXT("#extension GL_ARB_separate_shader_objects : enable\n"));
				StrOutSource.Append(TEXT("#define INTERFACE_LOCATION(Pos) layout(location=Pos) \n"));
				StrOutSource.Append(TEXT("#define INTERFACE_BLOCK(Pos, Interp, Modifiers, Semantic, PreType, PostType) layout(location=Pos) Interp Modifiers struct { PreType PostType; }\n"));
			}
			else
			{
				StrOutSource.Append(TEXT("#define INTERFACE_LOCATION(Pos) layout(location=Pos) \n"));
				StrOutSource.Append(TEXT("#define INTERFACE_BLOCK(Pos, Interp, Modifiers, Semantic, PreType, PostType) layout(location=Pos) Modifiers Semantic { PreType PostType; }\n"));
			}
		}
		else
		{
			StrOutSource.Append(TEXT("#define INTERFACE_LOCATION(Pos) \n"));
			StrOutSource.Append(TEXT("#define INTERFACE_BLOCK(Pos, Interp, Modifiers, Semantic, PreType, PostType) Modifiers Semantic { Interp PreType PostType; }\n"));
		}
	}

	if (Capabilities.TargetPlatform == EPlatformType::Android)
	{
		if (IsES2Platform(ShaderVersion) && !bES31)
		{
			StrOutSource.Append(TEXT("#define HDR_32BPP_ENCODE_MODE 0.0\n"));

			// This #define fixes compiler errors on Android (which doesn't seem to support textureCubeLodEXT)
			if (bUseES30ShadingLanguage)
			{
				if (TypeEnum == GL_VERTEX_SHADER)
				{
					StrOutSource.Append(TEXT("#define texture2D texture \n"
						"#define texture2DProj textureProj \n"
						"#define texture2DLod textureLod \n"
						"#define texture2DLodEXT textureLod \n"
						"#define texture2DProjLod textureProjLod \n"
						"#define textureCube texture \n"
						"#define textureCubeLod textureLod \n"
						"#define textureCubeLodEXT textureLod \n"
						"#define texture3D texture \n"
						"#define texture3DProj textureProj \n"
						"#define texture3DLod textureLod \n"));

					OriginalShaderSource.Replace(TEXT("attribute"), TEXT("in"));
					OriginalShaderSource.Replace(TEXT("varying"), TEXT("out"));
				}
				else if (TypeEnum == GL_FRAGMENT_SHADER)
				{
					// #extension directives have to come before any non-# directives. Because
					// we add non-# stuff below and the #extension directives
					// get added to the incoming shader source we move any # directives
					// to be right after the #version to ensure they are always correct.
					MoveHashLines(StrOutSource, OriginalShaderSource);

					StrOutSource.Append(TEXT("#define texture2D texture \n"
						"#define texture2DProj textureProj \n"
						"#define texture2DLod textureLod \n"
						"#define texture2DLodEXT textureLod \n"
						"#define texture2DProjLod textureProjLod \n"
						"#define textureCube texture \n"
						"#define textureCubeLod textureLod \n"
						"#define textureCubeLodEXT textureLod \n"
						"#define texture3D texture \n"
						"#define texture3DProj textureProj \n"
						"#define texture3DLod textureLod \n"
						"#define texture3DProjLod textureProjLod \n"
						"\n"
						"#define gl_FragColor out_FragColor \n"
						"#ifdef EXT_shader_framebuffer_fetch_enabled \n"
						"inout mediump vec4 out_FragColor; \n"
						"#else \n"
						"out mediump vec4 out_FragColor; \n"
						"#endif \n"));

					OriginalShaderSource.Replace(TEXT("varying"), TEXT("in"));
				}
			}
			else
			{
				if (TypeEnum == GL_FRAGMENT_SHADER)
				{
					// Apply #defines to deal with incompatible sections of code

					if (Capabilities.bRequiresDontEmitPrecisionForTextureSamplers)
					{
						StrOutSource.Append(TEXT("#define DONTEMITSAMPLERDEFAULTPRECISION \n"));
					}

					if (!Capabilities.bSupportsShaderTextureLod || !Capabilities.bSupportsShaderTextureCubeLod)
					{
						StrOutSource.Append(TEXT("#define DONTEMITEXTENSIONSHADERTEXTURELODENABLE \n"
							"#define texture2DLodEXT(a, b, c) texture2D(a, b) \n"
							"#define textureCubeLodEXT(a, b, c) textureCube(a, b) \n"));
					}
					else if (Capabilities.bRequiresTextureCubeLodEXTToTextureCubeLodDefine)
					{
						StrOutSource.Append(TEXT("#define textureCubeLodEXT textureCubeLod \n"));
					}
				}
			}
		}
	}
	else if (Capabilities.TargetPlatform == EPlatformType::Web)
	{
		// HTML5 use case is much simpler, use a separate chunk of code from android. 
		if (!Capabilities.bSupportsShaderTextureLod)
		{
			StrOutSource.Append(TEXT("#define DONTEMITEXTENSIONSHADERTEXTURELODENABLE \n"
				"#define texture2DLodEXT(a, b, c) texture2D(a, b) \n"
				"#define textureCubeLodEXT(a, b, c) textureCube(a, b) \n"));
		}
	}

	StrOutSource.Append(TEXT("#define HLSLCC_DX11ClipSpace 1 \n"));

	// Append the possibly edited shader to the one we will compile.
	// This is to make it easier to debug as we can see the whole
	// shader source.
	StrOutSource.Append(TEXT("\n\n"));
	StrOutSource.Append(OriginalShaderSource);

	const int32 SourceLen = StrOutSource.Len();
	TSharedPtr<ANSICHAR> RetShaderSource = MakeShareable(new ANSICHAR[SourceLen + 1]);
	FCStringAnsi::Strcpy(RetShaderSource.Get(), SourceLen + 1, TCHAR_TO_ANSI(*StrOutSource));

	return RetShaderSource;
}

bool FOpenGLFrontend::PlatformSupportsOfflineCompilation(const GLSLVersion ShaderVersion) const
{
	switch (ShaderVersion)
	{
		// desktop
		case GLSL_150:
		case GLSL_430:
		case GLSL_150_ES2:
		case GLSL_150_ES2_NOUB:
		case GLSL_150_ES3_1:
		case GLSL_310_ES_EXT:
		// web
		case GLSL_ES2_WEBGL:
		// switch
		case GLSL_SWITCH:
		case GLSL_SWITCH_FORWARD:
			return false;
		break;
		// android
		case GLSL_ES2:
		case GLSL_ES3_1_ANDROID:
			return true;
		break;
	}

	return false;
}

void FOpenGLFrontend::CompileOffline(const FShaderCompilerInput& Input, FShaderCompilerOutput& Output, const GLSLVersion ShaderVersion, const ANSICHAR* InShaderSource)
{
	const bool bSupportsOfflineCompilation = PlatformSupportsOfflineCompilation(ShaderVersion);

	if (!bSupportsOfflineCompilation)
	{
		return;
	}

	TSharedPtr<ANSICHAR> ShaderSource = PrepareCodeForOfflineCompilation(ShaderVersion, (EShaderFrequency)Input.Target.Frequency, InShaderSource);

	PlatformCompileOffline(Input, Output, ShaderSource.Get(), ShaderVersion);
}

void FOpenGLFrontend::PlatformCompileOffline(const FShaderCompilerInput& Input, FShaderCompilerOutput& ShaderOutput, const ANSICHAR* ShaderSource, const GLSLVersion ShaderVersion)
{
	if (ShaderVersion == GLSL_ES2 || ShaderVersion == GLSL_ES3_1_ANDROID)
	{
		CompileOfflineMali(Input, ShaderOutput, ShaderSource, FPlatformString::Strlen(ShaderSource), false);
	}
}
