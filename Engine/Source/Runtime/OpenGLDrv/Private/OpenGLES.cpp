// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	OpenGLES.cpp: OpenGL ES implementation.
=============================================================================*/

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "OpenGLDrv.h"
#include "OpenGLDrvPrivate.h"

#if !PLATFORM_DESKTOP

#if OPENGL_ES

PFNEGLGETSYSTEMTIMENVPROC eglGetSystemTimeNV_p = NULL;
PFNEGLCREATESYNCKHRPROC eglCreateSyncKHR_p = NULL;
PFNEGLDESTROYSYNCKHRPROC eglDestroySyncKHR_p = NULL;
PFNEGLCLIENTWAITSYNCKHRPROC eglClientWaitSyncKHR_p = NULL;
PFNEGLGETSYNCATTRIBKHRPROC eglGetSyncAttribKHR_p = NULL;

// Occlusion Queries
PFNGLGENQUERIESEXTPROC 					glGenQueriesEXT = NULL;
PFNGLDELETEQUERIESEXTPROC 				glDeleteQueriesEXT = NULL;
PFNGLISQUERYEXTPROC 					glIsQueryEXT = NULL;
PFNGLBEGINQUERYEXTPROC 					glBeginQueryEXT = NULL;
PFNGLENDQUERYEXTPROC 					glEndQueryEXT = NULL;
PFNGLGETQUERYIVEXTPROC 					glGetQueryivEXT = NULL;
PFNGLGETQUERYOBJECTUIVEXTPROC 			glGetQueryObjectuivEXT = NULL;

PFNGLQUERYCOUNTEREXTPROC				glQueryCounterEXT = NULL;
PFNGLGETQUERYOBJECTUI64VEXTPROC			glGetQueryObjectui64vEXT = NULL;

// Offscreen MSAA rendering
PFNGLDISCARDFRAMEBUFFEREXTPROC			glDiscardFramebufferEXT = NULL;
PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEEXTPROC	glFramebufferTexture2DMultisampleEXT = NULL;
PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC	glRenderbufferStorageMultisampleEXT = NULL;

PFNGLPUSHGROUPMARKEREXTPROC				glPushGroupMarkerEXT = NULL;
PFNGLPOPGROUPMARKEREXTPROC				glPopGroupMarkerEXT = NULL;
PFNGLLABELOBJECTEXTPROC					glLabelObjectEXT = NULL;
PFNGLGETOBJECTLABELEXTPROC				glGetObjectLabelEXT = NULL;

PFNGLMAPBUFFEROESPROC					glMapBufferOESa = NULL;
PFNGLUNMAPBUFFEROESPROC					glUnmapBufferOESa = NULL;

PFNGLTEXSTORAGE2DPROC					glTexStorage2D = NULL;
PFNGLTEXSTORAGE3DPROC					glTexStorage3D = NULL;

// KHR_debug
PFNGLDEBUGMESSAGECONTROLKHRPROC			glDebugMessageControlKHR = NULL;
PFNGLDEBUGMESSAGEINSERTKHRPROC			glDebugMessageInsertKHR = NULL;
PFNGLDEBUGMESSAGECALLBACKKHRPROC		glDebugMessageCallbackKHR = NULL;
PFNGLGETDEBUGMESSAGELOGKHRPROC			glDebugMessageLogKHR = NULL;
PFNGLGETPOINTERVKHRPROC					glGetPointervKHR = NULL;
PFNGLPUSHDEBUGGROUPKHRPROC				glPushDebugGroupKHR = NULL;
PFNGLPOPDEBUGGROUPKHRPROC				glPopDebugGroupKHR = NULL;
PFNGLOBJECTLABELKHRPROC					glObjectLabelKHR = NULL;
PFNGLGETOBJECTLABELKHRPROC				glGetObjectLabelKHR = NULL;
PFNGLOBJECTPTRLABELKHRPROC				glObjectPtrLabelKHR = NULL;
PFNGLGETOBJECTPTRLABELKHRPROC			glGetObjectPtrLabelKHR = NULL;

PFNGLDRAWELEMENTSINSTANCEDPROC			glDrawElementsInstanced = NULL;
PFNGLDRAWARRAYSINSTANCEDPROC			glDrawArraysInstanced = NULL;

PFNGLGENVERTEXARRAYSPROC 				glGenVertexArrays = NULL;
PFNGLBINDVERTEXARRAYPROC 				glBindVertexArray = NULL;
PFNGLMAPBUFFERRANGEPROC					glMapBufferRange = NULL;
PFNGLUNMAPBUFFERPROC					glUnmapBuffer = NULL;
PFNGLCOPYBUFFERSUBDATAPROC				glCopyBufferSubData = NULL;
PFNGLDRAWARRAYSINDIRECTPROC				glDrawArraysIndirect = NULL;
PFNGLDRAWELEMENTSINDIRECTPROC			glDrawElementsIndirect = NULL;

PFNGLVERTEXATTRIBDIVISORPROC			glVertexAttribDivisor = NULL;

PFNGLUNIFORM4UIVPROC					glUniform4uiv = NULL;
PFNGLTEXIMAGE3DPROC						glTexImage3D = NULL;
PFNGLTEXSUBIMAGE3DPROC					glTexSubImage3D = NULL;
PFNGLCOMPRESSEDTEXIMAGE3DPROC			glCompressedTexImage3D = NULL;
PFNGLCOMPRESSEDTEXSUBIMAGE3DPROC		glCompressedTexSubImage3D = NULL;
PFNGLCOPYTEXSUBIMAGE3DPROC				glCopyTexSubImage3D = NULL;
PFNGLCLEARBUFFERFIPROC					glClearBufferfi = NULL;
PFNGLCLEARBUFFERFVPROC					glClearBufferfv = NULL;
PFNGLCLEARBUFFERIVPROC					glClearBufferiv = NULL;
PFNGLCLEARBUFFERUIVPROC					glClearBufferuiv = NULL;
PFNGLREADBUFFERPROC						glReadBuffer = NULL;
PFNGLDRAWBUFFERSPROC					glDrawBuffers = NULL;
PFNGLCOLORMASKIEXTPROC					glColorMaskiEXT = NULL;
PFNGLTEXBUFFEREXTPROC					glTexBufferEXT = NULL;
PFNGLTEXBUFFERRANGEEXTPROC				glTexBufferRangeEXT = NULL;
PFNGLCOPYIMAGESUBDATAPROC				glCopyImageSubData = nullptr;

PFNGLGETPROGRAMBINARYOESPROC            glGetProgramBinary = NULL;
PFNGLPROGRAMBINARYOESPROC               glProgramBinary = NULL;

PFNGLBINDBUFFERRANGEPROC				glBindBufferRange = NULL;
PFNGLBINDBUFFERBASEPROC					glBindBufferBase = NULL;
PFNGLGETUNIFORMBLOCKINDEXPROC			glGetUniformBlockIndex = NULL;
PFNGLUNIFORMBLOCKBINDINGPROC			glUniformBlockBinding = NULL;
PFNGLVERTEXATTRIBIPOINTERPROC			glVertexAttribIPointer = NULL;
PFNGLBLITFRAMEBUFFERPROC				glBlitFramebuffer = NULL;

PFNGLGENSAMPLERSPROC					glGenSamplers = NULL;
PFNGLDELETESAMPLERSPROC					glDeleteSamplers = NULL;
PFNGLSAMPLERPARAMETERIPROC				glSamplerParameteri = NULL;
PFNGLBINDSAMPLERPROC					glBindSampler = NULL;
PFNGLPROGRAMPARAMETERIPROC				glProgramParameteri = NULL;

PFNGLMEMORYBARRIERPROC					glMemoryBarrier = NULL;
PFNGLDISPATCHCOMPUTEPROC				glDispatchCompute = NULL;
PFNGLDISPATCHCOMPUTEINDIRECTPROC		glDispatchComputeIndirect = NULL;
PFNGLBINDIMAGETEXTUREPROC				glBindImageTexture = NULL;

PFNGLDELETESYNCPROC						glDeleteSync = NULL;
PFNGLFENCESYNCPROC						glFenceSync = NULL;
PFNGLISSYNCPROC							glIsSync = NULL;
PFNGLCLIENTWAITSYNCPROC					glClientWaitSync = NULL;

PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC				glFramebufferTextureMultiviewOVR = NULL;
PFNGLFRAMEBUFFERTEXTUREMULTISAMPLEMULTIVIEWOVRPROC	glFramebufferTextureMultisampleMultiviewOVR = NULL;
PFNGLFRAMEBUFFERTEXTURELAYERPROC					glFramebufferTextureLayer = NULL;

/** GL_OES_vertex_array_object */
bool FOpenGLES::bSupportsVertexArrayObjects = false;

/** GL_OES_mapbuffer */
bool FOpenGLES::bSupportsMapBuffer = false;

/** GL_OES_depth_texture */
bool FOpenGLES::bSupportsDepthTexture = false;

/** GL_ARB_occlusion_query2, GL_EXT_occlusion_query_boolean */
bool FOpenGLES::bSupportsOcclusionQueries = false;

/** GL_EXT_disjoint_timer_query */
bool FOpenGLES::bSupportsDisjointTimeQueries = false;

static TAutoConsoleVariable<int32> CVarDisjointTimerQueries(
	TEXT("r.DisjointTimerQueries"),
	0,
	TEXT("If set to 1, allows GPU time to be measured (e.g. STAT UNIT). It defaults to 0 because some devices supports it but very slowly."),
	ECVF_ReadOnly);

/** Some timer query implementations are never disjoint */
bool FOpenGLES::bTimerQueryCanBeDisjoint = true;

/** GL_OES_rgb8_rgba8 */
bool FOpenGLES::bSupportsRGBA8 = false;

/** GL_APPLE_texture_format_BGRA8888 */
bool FOpenGLES::bSupportsBGRA8888 = false;

/** Whether BGRA supported as color attachment */
bool FOpenGLES::bSupportsBGRA8888RenderTarget = false;

/** GL_EXT_discard_framebuffer */
bool FOpenGLES::bSupportsDiscardFrameBuffer = false;

/** GL_OES_vertex_half_float */
bool FOpenGLES::bSupportsVertexHalfFloat = false;

/** GL_OES_texture_float */
bool FOpenGLES::bSupportsTextureFloat = false;

/** GL_OES_texture_half_float */
bool FOpenGLES::bSupportsTextureHalfFloat = false;

/** GL_EXT_color_buffer_half_float */
bool FOpenGLES::bSupportsColorBufferHalfFloat = false;

/** GL_EXT_color_buffer_float */
bool FOpenGLES::bSupportsColorBufferFloat = false;

/** GL_EXT_shader_framebuffer_fetch */
bool FOpenGLES::bSupportsShaderFramebufferFetch = false;

/* This is to avoid a bug where device supports GL_EXT_shader_framebuffer_fetch but does not define it in GLSL */
bool FOpenGLES::bRequiresUEShaderFramebufferFetchDef = false;

/** GL_ARM_shader_framebuffer_fetch_depth_stencil */
bool FOpenGLES::bSupportsShaderDepthStencilFetch = false;

/** GL_EXT_multisampled_render_to_texture */
bool FOpenGLES::bSupportsMultisampledRenderToTexture = false;

/** GL_NV_texture_compression_s3tc, GL_EXT_texture_compression_s3tc */
bool FOpenGLES::bSupportsDXT = false;

/** OpenGL ES 3.0 profile */
bool FOpenGLES::bSupportsETC2 = false;

/** GL_FRAGMENT_SHADER, GL_LOW_FLOAT */
int FOpenGLES::ShaderLowPrecision = 0;

/** GL_FRAGMENT_SHADER, GL_MEDIUM_FLOAT */
int FOpenGLES::ShaderMediumPrecision = 0;

/** GL_FRAGMENT_SHADER, GL_HIGH_FLOAT */
int FOpenGLES::ShaderHighPrecision = 0;

/** GL_NV_framebuffer_blit */
bool FOpenGLES::bSupportsNVFrameBufferBlit = false;

/** GL_OES_packed_depth_stencil */
bool FOpenGLES::bSupportsPackedDepthStencil = false;

/** textureCubeLodEXT */
bool FOpenGLES::bSupportsTextureCubeLodEXT = true;

/** GL_EXT_shader_texture_lod */
bool FOpenGLES::bSupportsShaderTextureLod = false;

/** textureCubeLod */
bool FOpenGLES::bSupportsShaderTextureCubeLod = true;

/** GL_APPLE_copy_texture_levels */
bool FOpenGLES::bSupportsCopyTextureLevels = false;

/** GL_OES_texture_npot */
bool FOpenGLES::bSupportsTextureNPOT = false;

/** GL_EXT_texture_storage */
bool FOpenGLES::bSupportsTextureStorageEXT = false;

/* This is a hack to remove the calls to "precision sampler" defaults which are produced by the cross compiler however don't compile on some android platforms */
bool FOpenGLES::bRequiresDontEmitPrecisionForTextureSamplers = false;

/* Some android platforms require textureCubeLod to be used some require textureCubeLodEXT however they either inconsistently or don't use the GL_TextureCubeLodEXT extension definition */
bool FOpenGLES::bRequiresTextureCubeLodEXTToTextureCubeLodDefine = false;

/* Some android platforms do not support the GL_OES_standard_derivatives extension */
bool FOpenGLES::bSupportsStandardDerivativesExtension = false;

/* This is a hack to remove the gl_FragCoord if shader will fail to link if exceeding the max varying on android platforms */
bool FOpenGLES::bRequiresGLFragCoordVaryingLimitHack = false;

/* This indicates failure when attempting to retrieve driver's binary representation of the hack program  */
bool FOpenGLES::bBinaryProgramRetrievalFailed = false;

/** Vertex attributes need remapping if GL_MAX_VERTEX_ATTRIBS < 16 */
bool FOpenGLES::bNeedsVertexAttribRemap = false;

/* This hack fixes an issue with SGX540 compiler which can get upset with some operations that mix highp and mediump */
bool FOpenGLES::bRequiresTexture2DPrecisionHack = false;

/* This is a hack to add a round() function when not available to a shader compiler */
bool FOpenGLES::bRequiresRoundFunctionHack = true;

/* Some Mali devices do not work correctly with early_fragment_test enabled */
bool FOpenGLES::bRequiresDisabledEarlyFragmentTests = false;

/* This is to avoid a bug in Adreno drivers that define GL_ARM_shader_framebuffer_fetch_depth_stencil even when device does not support this extension  */
bool FOpenGLES::bRequiresARMShaderFramebufferFetchDepthStencilUndef = false;

/* Indicates shader compiler hack checks are being tested */
bool FOpenGLES::bIsCheckingShaderCompilerHacks = false;

/** GL_OES_vertex_type_10_10_10_2 */
bool FOpenGLES::bSupportsRGB10A2 = false;

/** GL_OES_program_binary extension */
bool FOpenGLES::bSupportsProgramBinary = false;

/* Indicates shader compiler hack checks are being tested */
bool FOpenGLES::bIsLimitingShaderCompileCount = false;

bool FOpenGLES::bUseHalfFloatTexStorage = false;
bool FOpenGLES::bSupportsTextureBuffer = false;
bool FOpenGLES::bUseES30ShadingLanguage = false;
bool FOpenGLES::bES31Support = false;
bool FOpenGLES::bHasHardwareHiddenSurfaceRemoval = false;
bool FOpenGLES::bSupportsMobileMultiView = false;
GLint FOpenGLES::MaxMSAASamplesTileMem = 1;

GLint FOpenGLES::MaxComputeTextureImageUnits = -1;
GLint FOpenGLES::MaxComputeUniformComponents = -1;

GLint FOpenGLES::MaxComputeUAVUnits = -1;
GLint FOpenGLES::MaxPixelUAVUnits = -1;
GLint FOpenGLES::MaxCombinedUAVUnits = 0;

FOpenGLES::EFeatureLevelSupport FOpenGLES::CurrentFeatureLevelSupport = FOpenGLES::EFeatureLevelSupport::ES31;

bool FOpenGLES::SupportsDisjointTimeQueries()
{
	bool bAllowDisjointTimerQueries = false;
	bAllowDisjointTimerQueries = (CVarDisjointTimerQueries.GetValueOnRenderThread() == 1);
	return bSupportsDisjointTimeQueries && bAllowDisjointTimerQueries;
}

void FOpenGLES::ProcessQueryGLInt()
{
	GLint MaxVertexAttribs;
	LOG_AND_GET_GL_INT(GL_MAX_VERTEX_ATTRIBS, 0, MaxVertexAttribs);
	bNeedsVertexAttribRemap = MaxVertexAttribs < 16;
	if (bNeedsVertexAttribRemap)
	{
		UE_LOG(LogRHI, Warning,
			TEXT("Device reports support for %d vertex attributes, UE4 requires 16. Rendering artifacts may occur."),
			MaxVertexAttribs
		);
	}

	LOG_AND_GET_GL_INT(GL_MAX_VARYING_VECTORS, 0, MaxVaryingVectors);
	LOG_AND_GET_GL_INT(GL_MAX_VERTEX_UNIFORM_VECTORS, 0, MaxVertexUniformComponents);
	LOG_AND_GET_GL_INT(GL_MAX_FRAGMENT_UNIFORM_VECTORS, 0, MaxPixelUniformComponents);
	LOG_AND_GET_GL_INT(GL_TEXTURE_BUFFER_OFFSET_ALIGNMENT, 0, TextureBufferAlignment);

	const GLint RequiredMaxVertexUniformComponents = 256;
	if (MaxVertexUniformComponents < RequiredMaxVertexUniformComponents)
	{
		UE_LOG(LogRHI, Warning,
			TEXT("Device reports support for %d vertex uniform vectors, UE4 requires %d. Rendering artifacts may occur, especially with skeletal meshes. Some drivers, e.g. iOS, report a smaller number than is actually supported."),
			MaxVertexUniformComponents,
			RequiredMaxVertexUniformComponents
		);
	}
	MaxVertexUniformComponents = FMath::Max<GLint>(MaxVertexUniformComponents, RequiredMaxVertexUniformComponents);

	MaxGeometryUniformComponents = 0;

	MaxGeometryTextureImageUnits = 0;
	MaxHullTextureImageUnits = 0;
	MaxDomainTextureImageUnits = 0;
}

void FOpenGLES::ProcessExtensions(const FString& ExtensionsString)
{
	ProcessQueryGLInt();
	FOpenGLBase::ProcessExtensions(ExtensionsString);

	bSupportsMapBuffer = ExtensionsString.Contains(TEXT("GL_OES_mapbuffer"));
	bSupportsDepthTexture = ExtensionsString.Contains(TEXT("GL_OES_depth_texture"));
	bSupportsOcclusionQueries = ExtensionsString.Contains(TEXT("GL_ARB_occlusion_query2")) || ExtensionsString.Contains(TEXT("GL_EXT_occlusion_query_boolean"));
	bSupportsDisjointTimeQueries = ExtensionsString.Contains(TEXT("GL_EXT_disjoint_timer_query")) || ExtensionsString.Contains(TEXT("GL_NV_timer_query"));
	bTimerQueryCanBeDisjoint = !ExtensionsString.Contains(TEXT("GL_NV_timer_query"));
	bSupportsRGBA8 = ExtensionsString.Contains(TEXT("GL_OES_rgb8_rgba8"));
	bSupportsBGRA8888 = ExtensionsString.Contains(TEXT("GL_APPLE_texture_format_BGRA8888")) || ExtensionsString.Contains(TEXT("GL_IMG_texture_format_BGRA8888")) || ExtensionsString.Contains(TEXT("GL_EXT_texture_format_BGRA8888"));
	bSupportsBGRA8888RenderTarget = bSupportsBGRA8888;
	bSupportsVertexHalfFloat = ExtensionsString.Contains(TEXT("GL_OES_vertex_half_float"));
	bSupportsTextureFloat = ExtensionsString.Contains(TEXT("GL_OES_texture_float"));
	bSupportsTextureHalfFloat = ExtensionsString.Contains(TEXT("GL_OES_texture_half_float"));
	bSupportsColorBufferFloat = ExtensionsString.Contains(TEXT("GL_EXT_color_buffer_float"));
	bSupportsColorBufferHalfFloat = ExtensionsString.Contains(TEXT("GL_EXT_color_buffer_half_float"));
	bSupportsShaderFramebufferFetch = ExtensionsString.Contains(TEXT("GL_EXT_shader_framebuffer_fetch")) || ExtensionsString.Contains(TEXT("GL_NV_shader_framebuffer_fetch"))
		|| ExtensionsString.Contains(TEXT("GL_ARM_shader_framebuffer_fetch ")); // has space at the end to exclude GL_ARM_shader_framebuffer_fetch_depth_stencil match
	bRequiresUEShaderFramebufferFetchDef = ExtensionsString.Contains(TEXT("GL_EXT_shader_framebuffer_fetch"));
	bSupportsShaderDepthStencilFetch = ExtensionsString.Contains(TEXT("GL_ARM_shader_framebuffer_fetch_depth_stencil"));
	bSupportsMultisampledRenderToTexture = ExtensionsString.Contains(TEXT("GL_EXT_multisampled_render_to_texture"));
	bSupportsDXT = ExtensionsString.Contains(TEXT("GL_NV_texture_compression_s3tc")) || ExtensionsString.Contains(TEXT("GL_EXT_texture_compression_s3tc"));
	bSupportsVertexArrayObjects = ExtensionsString.Contains(TEXT("GL_OES_vertex_array_object"));
	bSupportsDiscardFrameBuffer = ExtensionsString.Contains(TEXT("GL_EXT_discard_framebuffer"));
	bSupportsNVFrameBufferBlit = ExtensionsString.Contains(TEXT("GL_NV_framebuffer_blit"));
	bSupportsPackedDepthStencil = ExtensionsString.Contains(TEXT("GL_OES_packed_depth_stencil"));
	bSupportsShaderTextureLod = ExtensionsString.Contains(TEXT("GL_EXT_shader_texture_lod"));
	bSupportsTextureStorageEXT = ExtensionsString.Contains(TEXT("GL_EXT_texture_storage"));
	bSupportsCopyTextureLevels = bSupportsTextureStorageEXT && ExtensionsString.Contains(TEXT("GL_APPLE_copy_texture_levels"));
	bSupportsTextureNPOT = ExtensionsString.Contains(TEXT("GL_OES_texture_npot")) || ExtensionsString.Contains(TEXT("GL_ARB_texture_non_power_of_two"));
	bSupportsStandardDerivativesExtension = ExtensionsString.Contains(TEXT("GL_OES_standard_derivatives"));
	bSupportsRGB10A2 = ExtensionsString.Contains(TEXT("GL_OES_vertex_type_10_10_10_2"));
	bSupportsProgramBinary = ExtensionsString.Contains(TEXT("GL_OES_get_program_binary"));

	// Report shader precision
	int Range[2];
	glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER, GL_LOW_FLOAT, Range, &ShaderLowPrecision);
	glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER, GL_MEDIUM_FLOAT, Range, &ShaderMediumPrecision);
	glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER, GL_HIGH_FLOAT, Range, &ShaderHighPrecision);
	UE_LOG(LogRHI, Log, TEXT("Fragment shader lowp precision: %d"), ShaderLowPrecision);
	UE_LOG(LogRHI, Log, TEXT("Fragment shader mediump precision: %d"), ShaderMediumPrecision);
	UE_LOG(LogRHI, Log, TEXT("Fragment shader highp precision: %d"), ShaderHighPrecision);

	if (FPlatformMisc::IsDebuggerPresent() && UE_BUILD_DEBUG)
	{
		// Enable GL debug markers if we're running in Xcode
		extern int32 GEmitMeshDrawEvent;
		GEmitMeshDrawEvent = 1;
		SetEmitDrawEvents(true);
	}

	GSupportsDepthRenderTargetWithoutColorRenderTarget = false;
	bSupportsOcclusionQueries = true;

	// Get procedures
	if (bSupportsOcclusionQueries || SupportsDisjointTimeQueries())
	{
		glGenQueriesEXT = (PFNGLGENQUERIESEXTPROC)((void*)eglGetProcAddress("glGenQueries"));
		glDeleteQueriesEXT = (PFNGLDELETEQUERIESEXTPROC)((void*)eglGetProcAddress("glDeleteQueries"));
		glIsQueryEXT = (PFNGLISQUERYEXTPROC)((void*)eglGetProcAddress("glIsQuery"));
		glBeginQueryEXT = (PFNGLBEGINQUERYEXTPROC)((void*)eglGetProcAddress("glBeginQuery"));
		glEndQueryEXT = (PFNGLENDQUERYEXTPROC)((void*)eglGetProcAddress("glEndQuery"));
		glGetQueryivEXT = (PFNGLGETQUERYIVEXTPROC)((void*)eglGetProcAddress("glGetQueryiv"));
		glGetQueryObjectuivEXT = (PFNGLGETQUERYOBJECTUIVEXTPROC)((void*)eglGetProcAddress("glGetQueryObjectuiv"));

		if (SupportsDisjointTimeQueries())
		{
			glQueryCounterEXT = (PFNGLQUERYCOUNTEREXTPROC)((void*)eglGetProcAddress("glQueryCounterEXT"));
			glGetQueryObjectui64vEXT = (PFNGLGETQUERYOBJECTUI64VEXTPROC)((void*)eglGetProcAddress("glGetQueryObjectui64vEXT"));

			// If EXT_disjoint_timer_query wasn't found, NV_timer_query might be available
			if (glQueryCounterEXT == NULL)
			{
				glQueryCounterEXT = (PFNGLQUERYCOUNTEREXTPROC)eglGetProcAddress("glQueryCounterNV");
			}
			if (glGetQueryObjectui64vEXT == NULL)
			{
				glGetQueryObjectui64vEXT = (PFNGLGETQUERYOBJECTUI64VEXTPROC)eglGetProcAddress("glGetQueryObjectui64vNV");
			}
		}
	}

	glDiscardFramebufferEXT = (PFNGLDISCARDFRAMEBUFFEREXTPROC)((void*)eglGetProcAddress("glDiscardFramebufferEXT"));
	glPushGroupMarkerEXT = (PFNGLPUSHGROUPMARKEREXTPROC)((void*)eglGetProcAddress("glPushGroupMarkerEXT"));
	glPopGroupMarkerEXT = (PFNGLPOPGROUPMARKEREXTPROC)((void*)eglGetProcAddress("glPopGroupMarkerEXT"));

	if (ExtensionsString.Contains(TEXT("GL_EXT_DEBUG_LABEL")))
	{
		glLabelObjectEXT = (PFNGLLABELOBJECTEXTPROC)((void*)eglGetProcAddress("glLabelObjectEXT"));
		glGetObjectLabelEXT = (PFNGLGETOBJECTLABELEXTPROC)((void*)eglGetProcAddress("glGetObjectLabelEXT"));
	}

	if (ExtensionsString.Contains(TEXT("GL_EXT_multisampled_render_to_texture")))
	{
		glFramebufferTexture2DMultisampleEXT = (PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEEXTPROC)((void*)eglGetProcAddress("glFramebufferTexture2DMultisampleEXT"));
		glRenderbufferStorageMultisampleEXT = (PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC)((void*)eglGetProcAddress("glRenderbufferStorageMultisampleEXT"));
		glGetIntegerv(GL_MAX_SAMPLES_EXT, &MaxMSAASamplesTileMem);
		MaxMSAASamplesTileMem = FMath::Max<GLint>(MaxMSAASamplesTileMem, 1);
		UE_LOG(LogRHI, Log, TEXT("Support for %dx MSAA detected"), MaxMSAASamplesTileMem);
	}
	else
	{
		// indicates RHI supports on-chip MSAA but this device does not.
		MaxMSAASamplesTileMem = 1;
	}

	if (bES31Support)
	{
		GET_GL_INT(GL_MAX_COMPUTE_TEXTURE_IMAGE_UNITS, 0, MaxComputeTextureImageUnits);
		GET_GL_INT(GL_MAX_COMPUTE_UNIFORM_COMPONENTS, 0, MaxComputeUniformComponents);

		LOG_AND_GET_GL_INT(GL_MAX_COMBINED_IMAGE_UNIFORMS, 0, MaxCombinedUAVUnits);
		LOG_AND_GET_GL_INT(GL_MAX_COMPUTE_IMAGE_UNIFORMS, 0, MaxComputeUAVUnits);
		LOG_AND_GET_GL_INT(GL_MAX_FRAGMENT_IMAGE_UNIFORMS, 0, MaxPixelUAVUnits);

		// clamp UAV units to a sensible limit
		MaxCombinedUAVUnits = FMath::Min(MaxCombinedUAVUnits, 8);
		MaxComputeUAVUnits = FMath::Min(MaxComputeUAVUnits, MaxCombinedUAVUnits);
		MaxPixelUAVUnits = FMath::Min(MaxPixelUAVUnits, MaxCombinedUAVUnits);
	}

	bSupportsETC2 = true;
	bUseES30ShadingLanguage = true;

	glDrawElementsInstanced = (PFNGLDRAWELEMENTSINSTANCEDPROC)((void*)eglGetProcAddress("glDrawElementsInstanced"));
	glDrawArraysInstanced = (PFNGLDRAWARRAYSINSTANCEDPROC)((void*)eglGetProcAddress("glDrawArraysInstanced"));
	glVertexAttribDivisor = (PFNGLVERTEXATTRIBDIVISORPROC)((void*)eglGetProcAddress("glVertexAttribDivisor"));
	glUniform4uiv = (PFNGLUNIFORM4UIVPROC)((void*)eglGetProcAddress("glUniform4uiv"));
	glTexImage3D = (PFNGLTEXIMAGE3DPROC)((void*)eglGetProcAddress("glTexImage3D"));
	glTexSubImage3D = (PFNGLTEXSUBIMAGE3DPROC)((void*)eglGetProcAddress("glTexSubImage3D"));
	glCompressedTexImage3D = (PFNGLCOMPRESSEDTEXIMAGE3DPROC)((void*)eglGetProcAddress("glCompressedTexImage3D"));
	glCompressedTexSubImage3D = (PFNGLCOMPRESSEDTEXSUBIMAGE3DPROC)((void*)eglGetProcAddress("glCompressedTexSubImage3D"));
	glCopyTexSubImage3D = (PFNGLCOPYTEXSUBIMAGE3DPROC)((void*)eglGetProcAddress("glCopyTexSubImage3D"));
	glClearBufferfi = (PFNGLCLEARBUFFERFIPROC)((void*)eglGetProcAddress("glClearBufferfi"));
	glClearBufferfv = (PFNGLCLEARBUFFERFVPROC)((void*)eglGetProcAddress("glClearBufferfv"));
	glClearBufferiv = (PFNGLCLEARBUFFERIVPROC)((void*)eglGetProcAddress("glClearBufferiv"));
	glClearBufferuiv = (PFNGLCLEARBUFFERUIVPROC)((void*)eglGetProcAddress("glClearBufferuiv"));
	glDrawBuffers = (PFNGLDRAWBUFFERSPROC)((void*)eglGetProcAddress("glDrawBuffers"));
	glReadBuffer = (PFNGLREADBUFFERPROC)((void*)eglGetProcAddress("glReadBuffer"));

	glMapBufferRange = (PFNGLMAPBUFFERRANGEPROC)((void*)eglGetProcAddress("glMapBufferRange"));
	glCopyBufferSubData = (PFNGLCOPYBUFFERSUBDATAPROC)((void*)eglGetProcAddress("glCopyBufferSubData"));
	glUnmapBuffer = (PFNGLUNMAPBUFFERPROC)((void*)eglGetProcAddress("glUnmapBuffer"));
	glBindBufferRange = (PFNGLBINDBUFFERRANGEPROC)((void*)eglGetProcAddress("glBindBufferRange"));
	glBindBufferBase = (PFNGLBINDBUFFERBASEPROC)((void*)eglGetProcAddress("glBindBufferBase"));
	glGetUniformBlockIndex = (PFNGLGETUNIFORMBLOCKINDEXPROC)((void*)eglGetProcAddress("glGetUniformBlockIndex"));
	glUniformBlockBinding = (PFNGLUNIFORMBLOCKBINDINGPROC)((void*)eglGetProcAddress("glUniformBlockBinding"));
	glVertexAttribIPointer = (PFNGLVERTEXATTRIBIPOINTERPROC)((void*)eglGetProcAddress("glVertexAttribIPointer"));
	glBlitFramebuffer = (PFNGLBLITFRAMEBUFFERPROC)(void*)eglGetProcAddress("glBlitFramebuffer");

	glGenSamplers = (PFNGLGENSAMPLERSPROC)((void*)eglGetProcAddress("glGenSamplers"));
	glDeleteSamplers = (PFNGLDELETESAMPLERSPROC)((void*)eglGetProcAddress("glDeleteSamplers"));
	glSamplerParameteri = (PFNGLSAMPLERPARAMETERIPROC)((void*)eglGetProcAddress("glSamplerParameteri"));
	glBindSampler = (PFNGLBINDSAMPLERPROC)((void*)eglGetProcAddress("glBindSampler"));
	glProgramParameteri = (PFNGLPROGRAMPARAMETERIPROC)((void*)eglGetProcAddress("glProgramParameteri"));

	glTexStorage3D = (PFNGLTEXSTORAGE3DPROC)((void*)eglGetProcAddress("glTexStorage3D"));

	glDeleteSync = (PFNGLDELETESYNCPROC)((void*)eglGetProcAddress("glDeleteSync"));
	glFenceSync = (PFNGLFENCESYNCPROC)((void*)eglGetProcAddress("glFenceSync"));
	glIsSync = (PFNGLISSYNCPROC)((void*)eglGetProcAddress("glIsSync"));
	glClientWaitSync = (PFNGLCLIENTWAITSYNCPROC)((void*)eglGetProcAddress("glClientWaitSync"));

	glFramebufferTextureLayer = (PFNGLFRAMEBUFFERTEXTURELAYERPROC)((void*)eglGetProcAddress("glFramebufferTextureLayer"));

	// Required by the ES3 spec
	bSupportsTextureFloat = true;
	bSupportsTextureHalfFloat = true;
	bSupportsRGB10A2 = true;
	bSupportsVertexHalfFloat = true;

	// According to https://www.khronos.org/registry/gles/extensions/EXT/EXT_color_buffer_float.txt
	bSupportsColorBufferHalfFloat = (bSupportsColorBufferHalfFloat || bSupportsColorBufferFloat);

	GSupportsDepthRenderTargetWithoutColorRenderTarget = true;
	
	// Mobile multi-view setup
	const bool bMultiViewSupport = ExtensionsString.Contains(TEXT("GL_OVR_multiview"));
	const bool bMultiView2Support = ExtensionsString.Contains(TEXT("GL_OVR_multiview2"));
	const bool bMultiViewMultiSampleSupport = ExtensionsString.Contains(TEXT("GL_OVR_multiview_multisampled_render_to_texture"));
	if (bMultiViewSupport && bMultiView2Support && bMultiViewMultiSampleSupport)
	{
		glFramebufferTextureMultiviewOVR = (PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC)((void*)eglGetProcAddress("glFramebufferTextureMultiviewOVR"));
		glFramebufferTextureMultisampleMultiviewOVR = (PFNGLFRAMEBUFFERTEXTUREMULTISAMPLEMULTIVIEWOVRPROC)((void*)eglGetProcAddress("glFramebufferTextureMultisampleMultiviewOVR"));

		bSupportsMobileMultiView = (glFramebufferTextureMultiviewOVR != NULL) && (glFramebufferTextureMultisampleMultiviewOVR != NULL);

		// Just because the driver declares multi-view support and hands us valid function pointers doesn't actually guarantee the feature works...
		if (bSupportsMobileMultiView)
		{
			UE_LOG(LogRHI, Log, TEXT("Device supports mobile multi-view."));
		}
	}

	if (bES31Support)
	{
		glDrawArraysIndirect = (PFNGLDRAWARRAYSINDIRECTPROC)((void*)eglGetProcAddress("glDrawArraysIndirect"));
		glDrawElementsIndirect = (PFNGLDRAWELEMENTSINDIRECTPROC)((void*)eglGetProcAddress("glDrawElementsIndirect"));
		bSupportsTextureBuffer = ExtensionsString.Contains(TEXT("GL_EXT_texture_buffer"));
		if (bSupportsTextureBuffer)
		{
			glTexBufferEXT = (PFNGLTEXBUFFEREXTPROC)((void*)eglGetProcAddress("glTexBufferEXT"));
			glTexBufferRangeEXT = (PFNGLTEXBUFFERRANGEEXTPROC)((void*)eglGetProcAddress("glTexBufferRangeEXT"));
		}

		GSupportsDepthRenderTargetWithoutColorRenderTarget = true;

		//
		glMemoryBarrier = (PFNGLMEMORYBARRIERPROC)((void*)eglGetProcAddress("glMemoryBarrier"));
		glDispatchCompute = (PFNGLDISPATCHCOMPUTEPROC)((void*)eglGetProcAddress("glDispatchCompute"));
		glDispatchComputeIndirect = (PFNGLDISPATCHCOMPUTEINDIRECTPROC)((void*)eglGetProcAddress("glDispatchComputeIndirect"));
		glBindImageTexture = (PFNGLBINDIMAGETEXTUREPROC)((void*)eglGetProcAddress("glBindImageTexture"));

		// ES 3.2
		glColorMaskiEXT = (PFNGLCOLORMASKIEXTPROC)((void*)eglGetProcAddress("glColorMaski"));
		if (glColorMaskiEXT == nullptr)
		{
			glColorMaskiEXT = (PFNGLCOLORMASKIEXTPROC)((void*)eglGetProcAddress("glColorMaskiEXT"));
		}
	}

	// test for glCopyImageSubData functionality
	// if device supports GLES 3.2 or higher get api function address otherwise search for glCopyImageSubDataEXT extension
	{
		if (IsES32Usable())
		{
			glCopyImageSubData = (PFNGLCOPYIMAGESUBDATAPROC)((void*)eglGetProcAddress("glCopyImageSubData"));
		}
		else
		{
			// search for extension name first because a non-null eglGetProcAddress() result does not necessarily imply the presence of the extension
			if (ExtensionsString.Contains(TEXT("GL_EXT_copy_image")))
			{
				glCopyImageSubData = (PFNGLCOPYIMAGESUBDATAPROC)((void*)eglGetProcAddress("glCopyImageSubDataEXT"));
			}
		}
		bSupportsCopyImage = (glCopyImageSubData != nullptr);
	}

	glTexStorage2D = (PFNGLTEXSTORAGE2DPROC)((void*)eglGetProcAddress("glTexStorage2D"));
	if (glTexStorage2D != NULL)
	{
		bUseHalfFloatTexStorage = true;
	}
	else
	{
		// need to disable GL_EXT_color_buffer_half_float support because we have no way to allocate the storage and the driver doesn't work without it.
		UE_LOG(LogRHI, Warning, TEXT("Disabling support for GL_EXT_color_buffer_half_float as we cannot bind glTexStorage2D"));
		bSupportsColorBufferHalfFloat = false;
	}

	// Set lowest possible limits for texture units, to avoid extra work in GL RHI
	MaxTextureImageUnits = FMath::Min(MaxTextureImageUnits, 16);
	MaxVertexTextureImageUnits = FMath::Min(MaxVertexTextureImageUnits, 16);
	MaxCombinedTextureImageUnits = FMath::Min(MaxCombinedTextureImageUnits, 32);

	if (bSupportsBGRA8888)
	{
		// Check whether device supports BGRA as color attachment
		GLuint FrameBuffer;
		glGenFramebuffers(1, &FrameBuffer);
		glBindFramebuffer(GL_FRAMEBUFFER, FrameBuffer);
		GLuint BGRA8888Texture;
		glGenTextures(1, &BGRA8888Texture);
		glBindTexture(GL_TEXTURE_2D, BGRA8888Texture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_BGRA_EXT, 256, 256, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, NULL);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, BGRA8888Texture, 0);

		bSupportsBGRA8888RenderTarget = (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

		glDeleteTextures(1, &BGRA8888Texture);
		glDeleteFramebuffers(1, &FrameBuffer);
	}
}

#endif

#endif //desktop
