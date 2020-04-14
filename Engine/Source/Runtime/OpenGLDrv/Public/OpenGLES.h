// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	OpenGLES2.h: Public OpenGL ES 2.0 definitions for non-common functionality
=============================================================================*/

#pragma once

#if !PLATFORM_DESKTOP // need this to fix compile issues with Win configuration.

#define OPENGL_ES	1

typedef GLfloat GLdouble;

#include "OpenGL.h"
#include "OpenGLUtil.h"		// for VERIFY_GL

#ifdef GL_AMD_debug_output
	#undef GL_AMD_debug_output
#endif

// Redefine to disable support for pixel buffer objects
#ifdef UGL_SUPPORTS_PIXELBUFFERS
	#undef UGL_SUPPORTS_PIXELBUFFERS
#endif
#define UGL_SUPPORTS_PIXELBUFFERS		0

// Redefine to disable support for uniform buffers
#ifdef UGL_SUPPORTS_UNIFORMBUFFERS
	#undef UGL_SUPPORTS_UNIFORMBUFFERS
#endif
#define UGL_SUPPORTS_UNIFORMBUFFERS		0


/** Unreal tokens that maps to different OpenGL tokens by platform. */
#undef UGL_ABGR8
#define UGL_ABGR8				GL_UNSIGNED_BYTE
#undef UGL_ANY_SAMPLES_PASSED
#define UGL_ANY_SAMPLES_PASSED	GL_ANY_SAMPLES_PASSED_EXT
#undef UGL_CLAMP_TO_BORDER
#define UGL_CLAMP_TO_BORDER		GL_CLAMP_TO_EDGE
#undef UGL_TIME_ELAPSED
#define UGL_TIME_ELAPSED		GL_TIME_ELAPSED_EXT

#define GL_HALF_FLOAT_OES 0x8D61
/** Map GL_EXT_separate_shader_objects to GL_ARB_separate_shader_objects */
#define GL_VERTEX_SHADER_BIT				0x00000001
#define GL_FRAGMENT_SHADER_BIT				0x00000002
#define GL_ALL_SHADER_BITS					0xFFFFFFFF
#define GL_PROGRAM_SEPARABLE				0x8258
#define GL_ACTIVE_PROGRAM					0x8259
#define GL_PROGRAM_PIPELINE_BINDING			0x825A
/** For the shader stage bits that don't exist just use 0 */
#define GL_GEOMETRY_SHADER_BIT				0x00000000
#define GL_TESS_CONTROL_SHADER_BIT			0x00000000
#define GL_TESS_EVALUATION_SHADER_BIT		0x00000000
#ifndef GL_COMPUTE_SHADER_BIT
#define GL_COMPUTE_SHADER_BIT				0x00000000
#endif

#ifndef GL_TEXTURE_1D
#define GL_TEXTURE_1D			0x0DE0
#endif

#ifndef GL_TEXTURE_1D_ARRAY
#define GL_TEXTURE_1D_ARRAY		0x8C18
#endif

#ifndef GL_TEXTURE_2D_ARRAY
#define GL_TEXTURE_2D_ARRAY		0x8C1A
#endif

#ifndef GL_TEXTURE_RECTANGLE
#define GL_TEXTURE_RECTANGLE	0x84F5
#endif

#define GL_COMPRESSED_RGB8_ETC2				0x9274
#define GL_COMPRESSED_SRGB8_ETC2			0x9275
#define GL_COMPRESSED_RGBA8_ETC2_EAC		0x9278
#define GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC 0x9279

#define GL_READ_FRAMEBUFFER_NV				0x8CA8
#define GL_DRAW_FRAMEBUFFER_NV				0x8CA9

#define GL_QUERY_COUNTER_BITS_EXT			0x8864
#define GL_CURRENT_QUERY_EXT				0x8865
#define GL_QUERY_RESULT_EXT					0x8866
#define GL_QUERY_RESULT_AVAILABLE_EXT		0x8867
#define GL_SAMPLES_PASSED_EXT				0x8914
#define GL_ANY_SAMPLES_PASSED_EXT			0x8C2F

#ifndef GL_MAX_TEXTURE_BUFFER_SIZE
#define GL_MAX_TEXTURE_BUFFER_SIZE			0x8C2B
#endif

/** Official OpenGL definitions */
#ifndef GL_FILL
#define GL_FILL 0x1B02
#endif
#define GL_PIXEL_UNPACK_BUFFER 0x88EC
#define GL_PIXEL_PACK_BUFFER 0x88EB
#ifndef GL_UNIFORM_BUFFER
#define GL_UNIFORM_BUFFER 0x8A11
#endif
#ifndef GL_COMPUTE_SHADER
#define GL_COMPUTE_SHADER 0x91B9
#endif
#ifndef GL_GEOMETRY_SHADER
#define GL_GEOMETRY_SHADER 0x8DD9
#endif
#ifndef GL_FLOAT_MAT2x3
#define GL_FLOAT_MAT2x3 0x8B65
#endif
#ifndef GL_FLOAT_MAT2x4
#define GL_FLOAT_MAT2x4 0x8B66
#endif
#ifndef GL_FLOAT_MAT3x2
#define GL_FLOAT_MAT3x2 0x8B67
#endif
#ifndef GL_FLOAT_MAT3x4
#define GL_FLOAT_MAT3x4 0x8B68
#endif
#ifndef GL_FLOAT_MAT4x2
#define GL_FLOAT_MAT4x2 0x8B69
#endif
#ifndef GL_FLOAT_MAT4x3
#define GL_FLOAT_MAT4x3 0x8B6A
#endif
#ifndef GL_SAMPLER_1D
#define GL_SAMPLER_1D 0x8B5D
#endif
#ifndef GL_SAMPLER_3D
#define GL_SAMPLER_3D 0x8B5F
#endif
#ifndef GL_SAMPLER_1D_SHADOW
#define GL_SAMPLER_1D_SHADOW 0x8B61
#endif
#ifndef GL_SAMPLER_2D_SHADOW
#define GL_SAMPLER_2D_SHADOW 0x8B62
#endif
#ifndef GL_TEXTURE_2D_MULTISAMPLE
#define GL_TEXTURE_2D_MULTISAMPLE 0x9100
#endif
#ifndef GL_TEXTURE_3D
#define GL_TEXTURE_3D 0x806F
#endif
#ifndef GL_TEXTURE_2D_ARRAY
#define GL_TEXTURE_2D_ARRAY 0x8C1A
#endif
#ifndef GL_DEPTH_STENCIL_ATTACHMENT
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#endif
#ifndef GL_HALF_FLOAT
#define GL_HALF_FLOAT 0x140B
#endif
#ifndef GL_DOUBLE
#define GL_DOUBLE 0x140A
#endif
#ifndef GL_SYNC_GPU_COMMANDS_COMPLETE
#define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#endif
#ifndef GL_SYNC_FLUSH_COMMANDS_BIT
#define GL_SYNC_FLUSH_COMMANDS_BIT 0x00000001
#endif
#ifndef GL_SAMPLES_PASSED
#define GL_SAMPLES_PASSED 0x8914
#endif
#ifndef GL_TIME_ELAPSED
#define GL_TIME_ELAPSED 0x88BF
#endif
#ifndef GL_FRONT_LEFT
#define GL_FRONT_LEFT 0x0400
#endif
#ifndef GL_FRONT_RIGHT
#define GL_FRONT_RIGHT 0x0401
#endif
#ifndef GL_BACK_LEFT
#define GL_BACK_LEFT 0x0402
#endif
#ifndef GL_BACK_RIGHT
#define GL_BACK_RIGHT 0x0403
#endif
#ifndef GL_FRONT_AND_BACK
#define GL_FRONT_AND_BACK 0x0408
#endif
#ifndef GL_LEFT
#define GL_LEFT 0x0406
#endif
#ifndef GL_RIGHT
#define GL_RIGHT 0x0407
#endif
#ifndef GL_DEPTH
#define GL_DEPTH 0x1801
#endif
#ifndef GL_STENCIL
#define GL_STENCIL 0x1802
#endif
#ifndef GL_COLOR_ATTACHMENT1
#define GL_COLOR_ATTACHMENT1              0x8CE1
#define GL_COLOR_ATTACHMENT2              0x8CE2
#define GL_COLOR_ATTACHMENT3              0x8CE3
#define GL_COLOR_ATTACHMENT4              0x8CE4
#define GL_COLOR_ATTACHMENT5              0x8CE5
#define GL_COLOR_ATTACHMENT6              0x8CE6
#define GL_COLOR_ATTACHMENT7              0x8CE7
#define GL_COLOR_ATTACHMENT8              0x8CE8
#define GL_COLOR_ATTACHMENT9              0x8CE9
#define GL_COLOR_ATTACHMENT10             0x8CEA
#define GL_COLOR_ATTACHMENT11             0x8CEB
#define GL_COLOR_ATTACHMENT12             0x8CEC
#define GL_COLOR_ATTACHMENT13             0x8CED
#define GL_COLOR_ATTACHMENT14             0x8CEE
#define GL_COLOR_ATTACHMENT15             0x8CEF
#endif
#ifndef GL_MIN
#define GL_MIN 0x8007
#endif
#ifndef GL_MAX
#define GL_MAX 0x8008
#endif
#ifndef GL_CLEAR
#define GL_CLEAR 0x1500
#endif
#ifndef GL_AND
#define GL_AND 0x1501
#endif
#ifndef GL_AND_REVERSE
#define GL_AND_REVERSE 0x1502
#endif
#ifndef GL_COPY
#define GL_COPY 0x1503
#endif
#ifndef GL_AND_INVERTED
#define GL_AND_INVERTED 0x1504
#endif
#ifndef GL_NOOP
#define GL_NOOP 0x1505
#endif
#ifndef GL_XOR
#define GL_XOR 0x1506
#endif
#ifndef GL_OR
#define GL_OR 0x1507
#endif
#ifndef GL_NOR
#define GL_NOR 0x1508
#endif
#ifndef GL_EQUIV
#define GL_EQUIV 0x1509
#endif
#ifndef GL_OR_REVERSE
#define GL_OR_REVERSE 0x150B
#endif
#ifndef GL_COPY_INVERTED
#define GL_COPY_INVERTED 0x150C
#endif
#ifndef GL_OR_INVERTED
#define GL_OR_INVERTED 0x150D
#endif
#ifndef GL_NAND
#define GL_NAND 0x150E
#endif
#ifndef GL_SET
#define GL_SET 0x150F
#endif
#ifndef GL_DEPTH_COMPONENT24
#define GL_DEPTH_COMPONENT24 0x81A6
#endif
#ifndef GL_DEPTH_COMPONENT32F
#define GL_DEPTH_COMPONENT32F 0x8CAC
#endif
#ifndef GL_DEPTH24_STENCIL8
#define GL_DEPTH24_STENCIL8 0x88F0
#endif
#ifndef GL_DEPTH32F_STENCIL8
#define GL_DEPTH32F_STENCIL8 0x8CAD
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_RGBA12
#define GL_RGBA12 0x805A
#endif
#ifndef GL_RGBA16
#define GL_RGBA16 0x805B
#endif
#ifndef GL_RGBA32F
#define GL_RGBA32F 0x8814
#endif
#ifndef GL_RGBA16I
#define GL_RGBA16I 0x8D88
#endif
#ifndef GL_RGBA16UI
#define GL_RGBA16UI 0x8D76
#endif
#ifndef GL_RGBA32I
#define GL_RGBA32I 0x8D82
#endif
#ifndef GL_RGBA32UI
#define GL_RGBA32UI 0x8D70
#endif
#ifndef GL_RGB10_A2
#define GL_RGB10_A2 0x8059
#endif
#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8 0x8C43
#endif
#ifndef GL_RG8
#define GL_RG8 0x822B
#endif
#ifndef GL_RG16
#define GL_RG16 0x822C
#endif
#ifndef GL_RG16F
#define GL_RG16F 0x822F
#endif
#ifndef GL_RG32F
#define GL_RG32F 0x8230
#endif
#ifndef GL_BGRA
#define GL_BGRA	GL_BGRA_EXT 
#endif
#ifndef GL_FRAMEBUFFER_SRGB
#define GL_FRAMEBUFFER_SRGB 0x8DB9
#endif
#ifndef GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT
#define GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT 0x8A34
#endif
#ifndef GL_UNSIGNED_INT_2_10_10_10_REV
#define GL_UNSIGNED_INT_2_10_10_10_REV 0x8368
#endif
#ifndef GL_PROGRAM_BINARY_LENGTH
#define GL_PROGRAM_BINARY_LENGTH 0x8741
#endif

#ifndef GL_TEXTURE_BUFFER_OFFSET_ALIGNMENT
#define GL_TEXTURE_BUFFER_OFFSET_ALIGNMENT		0x919F
#endif

// Normalize debug macros due to naming differences across GL versions
#if defined(GL_KHR_debug) && GL_KHR_debug
#define GL_DEBUG_SOURCE_OTHER_ARB GL_DEBUG_SOURCE_OTHER_KHR
#define GL_DEBUG_SOURCE_API_ARB GL_DEBUG_SOURCE_API_KHR
#define GL_DEBUG_TYPE_ERROR_ARB GL_DEBUG_TYPE_ERROR_KHR
#define GL_DEBUG_TYPE_OTHER_ARB GL_DEBUG_TYPE_OTHER_KHR
#define GL_DEBUG_TYPE_MARKER GL_DEBUG_TYPE_MARKER_KHR
#define GL_DEBUG_TYPE_PUSH_GROUP GL_DEBUG_TYPE_PUSH_GROUP_KHR
#define GL_DEBUG_TYPE_POP_GROUP GL_DEBUG_TYPE_POP_GROUP_KHR
#define GL_DEBUG_SEVERITY_HIGH_ARB GL_DEBUG_SEVERITY_HIGH_KHR
#define GL_DEBUG_SEVERITY_LOW_ARB GL_DEBUG_SEVERITY_LOW_KHR
#define GL_DEBUG_SEVERITY_NOTIFICATION GL_DEBUG_SEVERITY_NOTIFICATION_KHR
#endif

// These are forced to 0 to prevent the generation of glErrors at query initialization
#ifdef GL_MAX_3D_TEXTURE_SIZE
#undef GL_MAX_3D_TEXTURE_SIZE
#endif
#ifdef GL_MAX_COLOR_ATTACHMENTS
#undef GL_MAX_COLOR_ATTACHMENTS
#endif
#ifdef GL_MAX_SAMPLES
#undef GL_MAX_SAMPLES
#endif
#ifdef GL_MAX_COLOR_TEXTURE_SAMPLES
#undef GL_MAX_COLOR_TEXTURE_SAMPLES
#endif
#ifdef GL_MAX_COLOR_TEXTURE_SAMPLES
#undef GL_MAX_COLOR_TEXTURE_SAMPLES
#endif
#ifdef GL_MAX_DEPTH_TEXTURE_SAMPLES
#undef GL_MAX_DEPTH_TEXTURE_SAMPLES
#endif
#ifdef GL_MAX_INTEGER_SAMPLES
#undef GL_MAX_INTEGER_SAMPLES
#endif

#define GL_RG8I 0x8237
#define GL_RG8UI 0x8238
#define GL_RG16I 0x8239
#define GL_RG16UI 0x823A
#define GL_RG32I 0x823B
#define GL_RG32UI 0x823C
#define GL_R8 0x8229
#define GL_R16 0x822A
#define GL_R16F 0x822D
#define GL_R32F 0x822E
#define GL_R8I 0x8231
#define GL_R8UI 0x8232
#define GL_R16I 0x8233
#define GL_R16UI 0x8234
#define GL_R32I 0x8235
#define GL_R32UI 0x8236
#define GL_RGB8 0x8051
#define GL_RGB5 0x8050
#define GL_R3_G3_B2 0x2A10
#define GL_RGB4 0x804F
#define GL_SRGB8 0x8C41
#define GL_R11F_G11F_B10F 0x8C3A
#define GL_RGB9_E5 0x8C3D
#define GL_SIGNED_NORMALIZED 0x8F9C
#define GL_UNSIGNED_NORMALIZED 0x8C17
#define GL_SRGB 0x8C40
#define GL_UNSIGNED_INT_VEC2 0x8DC6
#define GL_UNSIGNED_INT_VEC3 0x8DC7
#define GL_UNSIGNED_INT_VEC4 0x8DC8
#define GL_SAMPLER_1D_ARRAY 0x8DC0
#define GL_SAMPLER_2D_ARRAY 0x8DC1
#define GL_SAMPLER_1D_ARRAY_SHADOW 0x8DC3
#define GL_SAMPLER_2D_ARRAY_SHADOW 0x8DC4
#define GL_SAMPLER_2D_MULTISAMPLE 0x9108
#define GL_SAMPLER_2D_MULTISAMPLE_ARRAY 0x910B
#define GL_SAMPLER_CUBE_SHADOW 0x8DC5
#define GL_SAMPLER_BUFFER 0x8DC2
#define GL_SAMPLER_2D_RECT 0x8B63
#define GL_SAMPLER_2D_RECT_SHADOW 0x8B64
#define GL_INT_SAMPLER_1D 0x8DC9
#define GL_INT_SAMPLER_2D 0x8DCA
#define GL_INT_SAMPLER_3D 0x8DCB
#define GL_INT_SAMPLER_CUBE 0x8DCC
#define GL_INT_SAMPLER_1D_ARRAY 0x8DCE
#define GL_INT_SAMPLER_2D_ARRAY 0x8DCF
#define GL_INT_SAMPLER_2D_MULTISAMPLE 0x9109
#define GL_INT_SAMPLER_2D_MULTISAMPLE_ARRAY 0x910C
#define GL_INT_SAMPLER_BUFFER 0x8DD0
#define GL_INT_SAMPLER_2D_RECT 0x8DCD
#define GL_UNSIGNED_INT_SAMPLER_1D 0x8DD1
#define GL_UNSIGNED_INT_SAMPLER_2D 0x8DD2
#define GL_UNSIGNED_INT_SAMPLER_3D 0x8DD3
#define GL_UNSIGNED_INT_SAMPLER_CUBE 0x8DD4
#define GL_UNSIGNED_INT_SAMPLER_1D_ARRAY 0x8DD6
#define GL_UNSIGNED_INT_SAMPLER_2D_ARRAY 0x8DD7
#define GL_UNSIGNED_INT_SAMPLER_2D_MULTISAMPLE 0x910A
#define GL_UNSIGNED_INT_SAMPLER_2D_MULTISAMPLE_ARRAY 0x910D
#define GL_UNSIGNED_INT_SAMPLER_BUFFER 0x8DD8
#define GL_UNSIGNED_INT_SAMPLER_2D_RECT 0x8DD5
#define GL_CLAMP_TO_BORDER 0x812D
#define GL_MIRROR_CLAMP_EXT 0x8742
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#define GL_MAX_DRAW_BUFFERS 0x8824
#define GL_DRAW_BUFFER0 0x8825
#define GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#define GL_READ_BUFFER 0x0C02
#define GL_POINT 0x1B00
#define GL_LINE 0x1B01
#define GL_TEXTURE_WRAP_R 0x8072
#define GL_TEXTURE_LOD_BIAS 0x8501
#define GL_TEXTURE_COMPARE_FUNC 0x884D
#define GL_TEXTURE_COMPARE_MODE 0x884C
#define GL_COMPARE_REF_TO_TEXTURE 0x884E
#define GL_POLYGON_OFFSET_LINE 0x2A02
#define GL_POLYGON_OFFSET_POINT 0x2A01
#define GL_TEXTURE_BUFFER 0x8C2A
#define GL_DEPTH_STENCIL 0x84F9
#define GL_STENCIL 0x1802
#define GL_DEPTH 0x1801
#define GL_COLOR 0x1800
#define GL_TEXTURE_BASE_LEVEL 0x813C
#define GL_TEXTURE_MAX_LEVEL 0x813D
#define GL_COPY_READ_BUFFER 0x8F36
#define GL_COPY_WRITE_BUFFER 0x8F37
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#define GL_UNPACK_IMAGE_HEIGHT 0x806E
#define GL_NUM_EXTENSIONS 0x821D

#ifdef __EMSCRIPTEN__
// Browser supports either GLES2.0 or GLES3.0 at runtime, so needs to read these
#define GL_MAX_3D_TEXTURE_SIZE 0x8073
#define GL_MAX_COLOR_ATTACHMENTS 0x8CDF
#define GL_MAX_SAMPLES 0x8D57
#else
// In native OpenGL ES 2.0, define to zero things that are not available.
#define GL_MAX_3D_TEXTURE_SIZE 0	//0x8073
#define GL_MAX_COLOR_ATTACHMENTS 0	//0x8CDF
#define GL_MAX_SAMPLES 0	//0x8D57
#endif

// OpenGL ES 3.1:
#define GL_MAX_COLOR_TEXTURE_SAMPLES 0	//0x910E
#define GL_MAX_DEPTH_TEXTURE_SAMPLES 0	//0x910F
#define GL_MAX_INTEGER_SAMPLES 0	//0x9110

typedef void (GL_APIENTRYP PFNGLGENQUERIESEXTPROC) (GLsizei n, GLuint* ids);
typedef void (GL_APIENTRYP PFNGLDELETEQUERIESEXTPROC) (GLsizei n, const GLuint* ids);
typedef GLboolean(GL_APIENTRYP PFNGLISQUERYEXTPROC) (GLuint id);
typedef void (GL_APIENTRYP PFNGLBEGINQUERYEXTPROC) (GLenum target, GLuint id);
typedef void (GL_APIENTRYP PFNGLENDQUERYEXTPROC) (GLenum target);
typedef void (GL_APIENTRYP PFNGLQUERYCOUNTEREXTPROC) (GLuint id, GLenum target);
typedef void (GL_APIENTRYP PFNGLGETQUERYIVEXTPROC) (GLenum target, GLenum pname, GLint* params);
typedef void (GL_APIENTRYP PFNGLGETQUERYOBJECTIVEXTPROC) (GLuint id, GLenum pname, GLint* params);
typedef void (GL_APIENTRYP PFNGLGETQUERYOBJECTUIVEXTPROC) (GLuint id, GLenum pname, GLuint* params);
typedef void (GL_APIENTRYP PFNGLGETQUERYOBJECTUI64VEXTPROC) (GLuint id, GLenum pname, GLuint64* params);
typedef void* (GL_APIENTRYP PFNGLMAPBUFFEROESPROC) (GLenum target, GLenum access);
typedef GLboolean(GL_APIENTRYP PFNGLUNMAPBUFFEROESPROC) (GLenum target);
typedef void (GL_APIENTRYP PFNGLPUSHGROUPMARKEREXTPROC) (GLsizei length, const GLchar* marker);
typedef void (GL_APIENTRYP PFNGLLABELOBJECTEXTPROC) (GLenum type, GLuint object, GLsizei length, const GLchar* label);
typedef void (GL_APIENTRYP PFNGLGETOBJECTLABELEXTPROC) (GLenum type, GLuint object, GLsizei bufSize, GLsizei* length, GLchar* label);
typedef void (GL_APIENTRYP PFNGLPOPGROUPMARKEREXTPROC) (void);
typedef void (GL_APIENTRYP PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEEXTPROC) (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level, GLsizei samples);
typedef void (GL_APIENTRYP PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC) (GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height);
typedef void (GL_APIENTRYP PFNGLFRAMEBUFFERTEXTURELAYERPROC) (GLenum target, GLenum attachment, GLuint texture, GLint level, GLint layer);
typedef void (GL_APIENTRYP PFNGLCOLORMASKIEXTPROC) (GLuint index, GLboolean r, GLboolean g, GLboolean b, GLboolean a);

/** from ES 3.0 but can be called on certain Adreno devices */
typedef void (GL_APIENTRYP PFNGLTEXSTORAGE2DPROC) (GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height);

// Mobile multi-view
typedef void (GL_APIENTRYP PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC) (GLenum target, GLenum attachment, GLuint texture, GLint level, GLint baseViewIndex, GLsizei numViews);
typedef void (GL_APIENTRYP PFNGLFRAMEBUFFERTEXTUREMULTISAMPLEMULTIVIEWOVRPROC) (GLenum target, GLenum attachment, GLuint texture, GLint level, GLsizei samples, GLint baseViewIndex, GLsizei numViews);

typedef void (GL_APIENTRYP PFNGLCOPYIMAGESUBDATAPROC) (GLuint srcName, GLenum srcTarget, GLint srcLevel, GLint srcX, GLint srcY, GLint srcZ, GLuint dstName, GLenum dstTarget, GLint dstLevel, GLint dstX, GLint dstY, GLint dstZ, GLsizei width, GLsizei height, GLsizei depth);

extern PFNGLGENQUERIESEXTPROC 			glGenQueriesEXT;
extern PFNGLDELETEQUERIESEXTPROC 		glDeleteQueriesEXT;
extern PFNGLISQUERYEXTPROC 				glIsQueryEXT;
extern PFNGLBEGINQUERYEXTPROC 			glBeginQueryEXT;
extern PFNGLENDQUERYEXTPROC 			glEndQueryEXT;
extern PFNGLQUERYCOUNTEREXTPROC			glQueryCounterEXT;
extern PFNGLGETQUERYIVEXTPROC 			glGetQueryivEXT;
extern PFNGLGETQUERYOBJECTUIVEXTPROC 	glGetQueryObjectuivEXT;
extern PFNGLGETQUERYOBJECTUI64VEXTPROC	glGetQueryObjectui64vEXT;
extern PFNGLMAPBUFFEROESPROC			glMapBufferOESa;
extern PFNGLUNMAPBUFFEROESPROC			glUnmapBufferOESa;
extern PFNGLDISCARDFRAMEBUFFEREXTPROC 	glDiscardFramebufferEXT;
extern PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEEXTPROC	glFramebufferTexture2DMultisampleEXT;
extern PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC	glRenderbufferStorageMultisampleEXT;
extern PFNGLPUSHGROUPMARKEREXTPROC		glPushGroupMarkerEXT;
extern PFNGLLABELOBJECTEXTPROC			glLabelObjectEXT;
extern PFNGLGETOBJECTLABELEXTPROC		glGetObjectLabelEXT;
extern PFNGLPOPGROUPMARKEREXTPROC 		glPopGroupMarkerEXT;
extern PFNGLTEXSTORAGE2DPROC			glTexStorage2D;
extern PFNGLTEXSTORAGE3DPROC			glTexStorage3D;
extern PFNGLDEBUGMESSAGECONTROLKHRPROC	glDebugMessageControlKHR;
extern PFNGLDEBUGMESSAGEINSERTKHRPROC	glDebugMessageInsertKHR;
extern PFNGLDEBUGMESSAGECALLBACKKHRPROC	glDebugMessageCallbackKHR;
extern PFNGLGETDEBUGMESSAGELOGKHRPROC	glDebugMessageLogKHR;
extern PFNGLGETPOINTERVKHRPROC			glGetPointervKHR;
extern PFNGLPUSHDEBUGGROUPKHRPROC		glPushDebugGroupKHR;
extern PFNGLPOPDEBUGGROUPKHRPROC		glPopDebugGroupKHR;
extern PFNGLOBJECTLABELKHRPROC			glObjectLabelKHR;
extern PFNGLGETOBJECTLABELKHRPROC		glGetObjectLabelKHR;
extern PFNGLOBJECTPTRLABELKHRPROC		glObjectPtrLabelKHR;
extern PFNGLGETOBJECTPTRLABELKHRPROC	glGetObjectPtrLabelKHR;
extern PFNGLDRAWELEMENTSINSTANCEDPROC	glDrawElementsInstanced;
extern PFNGLDRAWARRAYSINSTANCEDPROC		glDrawArraysInstanced;
extern PFNGLVERTEXATTRIBDIVISORPROC		glVertexAttribDivisor;

extern PFNGLGENVERTEXARRAYSPROC 		glGenVertexArrays;
extern PFNGLBINDVERTEXARRAYPROC 		glBindVertexArray;
extern PFNGLMAPBUFFERRANGEPROC			glMapBufferRange;
extern PFNGLUNMAPBUFFERPROC				glUnmapBuffer;
extern PFNGLCOPYBUFFERSUBDATAPROC		glCopyBufferSubData;
extern PFNGLDRAWARRAYSINDIRECTPROC		glDrawArraysIndirect;
extern PFNGLDRAWELEMENTSINDIRECTPROC	glDrawElementsIndirect;

extern PFNGLTEXBUFFEREXTPROC			glTexBufferEXT;
extern PFNGLTEXBUFFERRANGEEXTPROC		glTexBufferRangeEXT;
extern PFNGLUNIFORM4UIVPROC				glUniform4uiv;
extern PFNGLCLEARBUFFERFIPROC			glClearBufferfi;
extern PFNGLCLEARBUFFERFVPROC			glClearBufferfv;
extern PFNGLCLEARBUFFERIVPROC			glClearBufferiv;
extern PFNGLCLEARBUFFERUIVPROC			glClearBufferuiv;
extern PFNGLREADBUFFERPROC				glReadBuffer;
extern PFNGLDRAWBUFFERSPROC				glDrawBuffers;
extern PFNGLCOLORMASKIEXTPROC			glColorMaskiEXT;
extern PFNGLTEXIMAGE3DPROC				glTexImage3D;
extern PFNGLTEXSUBIMAGE3DPROC			glTexSubImage3D;
extern PFNGLCOMPRESSEDTEXIMAGE3DPROC    glCompressedTexImage3D;
extern PFNGLCOMPRESSEDTEXSUBIMAGE3DPROC	glCompressedTexSubImage3D;
extern PFNGLCOPYTEXSUBIMAGE3DPROC		glCopyTexSubImage3D;
extern PFNGLCOPYIMAGESUBDATAPROC		glCopyImageSubData;

extern PFNGLGETPROGRAMBINARYOESPROC     glGetProgramBinary;
extern PFNGLPROGRAMBINARYOESPROC        glProgramBinary;

extern PFNGLBINDBUFFERRANGEPROC			glBindBufferRange;
extern PFNGLBINDBUFFERBASEPROC			glBindBufferBase;
extern PFNGLGETUNIFORMBLOCKINDEXPROC	glGetUniformBlockIndex;
extern PFNGLUNIFORMBLOCKBINDINGPROC		glUniformBlockBinding;
extern PFNGLBLITFRAMEBUFFERPROC			glBlitFramebuffer;

extern PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC glFramebufferTextureMultiviewOVR;
extern PFNGLFRAMEBUFFERTEXTUREMULTISAMPLEMULTIVIEWOVRPROC glFramebufferTextureMultisampleMultiviewOVR;
extern PFNGLVERTEXATTRIBIPOINTERPROC	glVertexAttribIPointer;

extern PFNGLGENSAMPLERSPROC				glGenSamplers;
extern PFNGLDELETESAMPLERSPROC			glDeleteSamplers;
extern PFNGLSAMPLERPARAMETERIPROC		glSamplerParameteri;
extern PFNGLBINDSAMPLERPROC				glBindSampler;

extern PFNGLPROGRAMPARAMETERIPROC		glProgramParameteri;

extern PFNGLMEMORYBARRIERPROC			glMemoryBarrier;
extern PFNGLDISPATCHCOMPUTEPROC			glDispatchCompute;
extern PFNGLDISPATCHCOMPUTEINDIRECTPROC	glDispatchComputeIndirect;
extern PFNGLBINDIMAGETEXTUREPROC		glBindImageTexture;

extern PFNGLDELETESYNCPROC				glDeleteSync;
extern PFNGLFENCESYNCPROC				glFenceSync;
extern PFNGLISSYNCPROC					glIsSync;
extern PFNGLCLIENTWAITSYNCPROC			glClientWaitSync;

extern PFNGLFRAMEBUFFERTEXTURELAYERPROC glFramebufferTextureLayer;

struct FOpenGLES : public FOpenGLBase
{
	static FORCEINLINE bool IsES31Usable()
	{
		check(CurrentFeatureLevelSupport != EFeatureLevelSupport::Invalid);
		return CurrentFeatureLevelSupport >= EFeatureLevelSupport::ES31;
	}

	static FORCEINLINE bool IsES32Usable()
	{
		check(CurrentFeatureLevelSupport != EFeatureLevelSupport::Invalid);
		return CurrentFeatureLevelSupport == EFeatureLevelSupport::ES32;
	}

	static void		ProcessQueryGLInt();
	static void		ProcessExtensions(const FString& ExtensionsString);

	static FORCEINLINE bool SupportsVertexArrayObjects() { return bSupportsVertexArrayObjects; }
	static FORCEINLINE bool SupportsMapBuffer() { return bSupportsMapBuffer; }
	static FORCEINLINE bool SupportsDepthTexture() { return bSupportsDepthTexture; }
	static FORCEINLINE bool SupportsTextureSwizzle() { return true; }
	static FORCEINLINE bool SupportsDrawBuffers() { return true; }
	static FORCEINLINE bool SupportsPixelBuffers() { return false; }
	static FORCEINLINE bool SupportsUniformBuffers() { return true; }
	static FORCEINLINE bool SupportsStructuredBuffers() { return bES31Support; }
	static FORCEINLINE bool SupportsOcclusionQueries() { return bSupportsOcclusionQueries; }
	static FORCEINLINE bool SupportsExactOcclusionQueries() { return false; }
	// MLCHANGES BEGIN -- changed to use bSupportsDisjointTimeQueries
	static FORCEINLINE bool SupportsTimestampQueries() { return bSupportsDisjointTimeQueries; }
	// MLCHANGES END
	static bool SupportsDisjointTimeQueries();
	static FORCEINLINE bool SupportsBlitFramebuffer() { return true; }
	static FORCEINLINE bool SupportsDepthStencilRead() { return false; }
	static FORCEINLINE bool SupportsFloatReadSurface() { return SupportsColorBufferHalfFloat(); }
	static FORCEINLINE bool SupportsMultipleRenderTargets() { return true; }
	static FORCEINLINE bool SupportsWideMRT() { return bES31Support; }
	static FORCEINLINE bool SupportsMultisampledTextures() { return false; }
	static FORCEINLINE bool SupportsFences() { return false; }
	static FORCEINLINE bool SupportsPolygonMode() { return false; }
	static FORCEINLINE bool SupportsSamplerObjects() { return IsES31Usable(); }
	static FORCEINLINE bool SupportsTexture3D() { return true; }
	static FORCEINLINE bool SupportsMobileMultiView() { return bSupportsMobileMultiView; }
	static FORCEINLINE bool SupportsImageExternal() { return false; }
	static FORCEINLINE bool SupportsTextureLODBias() { return false; }
	static FORCEINLINE bool SupportsTextureCompare() { return false; }
	static FORCEINLINE bool SupportsTextureBaseLevel() { return false; }
	static FORCEINLINE bool SupportsTextureMaxLevel() { return false; }
	static FORCEINLINE bool SupportsVertexAttribInteger() { return false; }
	static FORCEINLINE bool SupportsVertexAttribShort() { return false; }
	static FORCEINLINE bool SupportsVertexAttribByte() { return false; }
	static FORCEINLINE bool SupportsVertexAttribDouble() { return false; }
	static FORCEINLINE bool SupportsDrawIndexOffset() { return false; }
	static FORCEINLINE bool SupportsResourceView() { return bSupportsTextureBuffer; }
	static FORCEINLINE bool SupportsCopyBuffer() { return false; }
	static FORCEINLINE bool SupportsDiscardFrameBuffer() { return bSupportsDiscardFrameBuffer; }
	static FORCEINLINE bool SupportsIndexedExtensions() { return false; }
	static FORCEINLINE bool SupportsVertexHalfFloat() { return bSupportsVertexHalfFloat; }
	static FORCEINLINE bool SupportsTextureFloat() { return bSupportsTextureFloat; }
	static FORCEINLINE bool SupportsTextureHalfFloat() { return bSupportsTextureHalfFloat; }
	static FORCEINLINE bool SupportsColorBufferFloat() { return bSupportsColorBufferFloat; }
	static FORCEINLINE bool SupportsColorBufferHalfFloat() { return bSupportsColorBufferHalfFloat; }
	static FORCEINLINE bool	SupportsRG16UI() { return false; }
	static FORCEINLINE bool	SupportsRG32UI() { return false; }
	static FORCEINLINE bool SupportsR11G11B10F() { return false; }
	static FORCEINLINE bool SupportsShaderFramebufferFetch() { return bSupportsShaderFramebufferFetch; }
	static FORCEINLINE bool SupportsShaderDepthStencilFetch() { return bSupportsShaderDepthStencilFetch; }
	static FORCEINLINE bool SupportsMultisampledRenderToTexture() { return bSupportsMultisampledRenderToTexture; }
	static FORCEINLINE bool SupportsVertexArrayBGRA() { return false; }
	static FORCEINLINE bool SupportsBGRA8888() { return bSupportsBGRA8888; }
	static FORCEINLINE bool SupportsBGRA8888RenderTarget() { return bSupportsBGRA8888RenderTarget; }
	static FORCEINLINE bool SupportsSRGB() { return IsES31Usable(); }
	static FORCEINLINE bool SupportsRGBA8() { return bSupportsRGBA8; }
	static FORCEINLINE bool SupportsDXT() { return bSupportsDXT; }
	static FORCEINLINE bool SupportsETC2() { return bSupportsETC2; }
	static FORCEINLINE bool SupportsCombinedDepthStencilAttachment() { return false; }
	static FORCEINLINE bool SupportsPackedDepthStencil() { return bSupportsPackedDepthStencil; }
	static FORCEINLINE bool SupportsTextureCubeLodEXT() { return bSupportsTextureCubeLodEXT; }
	static FORCEINLINE bool SupportsShaderTextureLod() { return bSupportsShaderTextureLod; }
	static FORCEINLINE bool SupportsShaderTextureCubeLod() { return bSupportsShaderTextureCubeLod; }
	static FORCEINLINE bool SupportsCopyTextureLevels() { return bSupportsCopyTextureLevels; }
	static FORCEINLINE GLenum GetDepthFormat() { return GL_DEPTH_COMPONENT24; }
	static FORCEINLINE GLenum GetShadowDepthFormat() { return GL_DEPTH_COMPONENT16; }
	static FORCEINLINE bool SupportsFramebufferSRGBEnable() { return false; }
	static FORCEINLINE bool SupportsRGB10A2() { return bSupportsRGB10A2; }
	static FORCEINLINE bool UseES30ShadingLanguage() { return bUseES30ShadingLanguage; }
	static FORCEINLINE bool SupportsComputeShaders() { return bES31Support; }
	static FORCEINLINE bool SupportsDrawIndirect() { return true; }

	static FORCEINLINE bool RequiresUEShaderFramebufferFetchDef() { return bRequiresUEShaderFramebufferFetchDef; }
	static FORCEINLINE bool RequiresDontEmitPrecisionForTextureSamplers() { return bRequiresDontEmitPrecisionForTextureSamplers; }
	static FORCEINLINE bool RequiresTextureCubeLodEXTToTextureCubeLodDefine() { return bRequiresTextureCubeLodEXTToTextureCubeLodDefine; }
	static FORCEINLINE bool SupportsStandardDerivativesExtension() { return bSupportsStandardDerivativesExtension; }
	static FORCEINLINE bool RequiresGLFragCoordVaryingLimitHack() { return bRequiresGLFragCoordVaryingLimitHack; }
	static FORCEINLINE bool HasBinaryProgramRetrievalFailed() { return bBinaryProgramRetrievalFailed; }
	static FORCEINLINE bool RequiresTexture2DPrecisionHack() { return bRequiresTexture2DPrecisionHack; }
	static FORCEINLINE bool RequiresRoundFunctionHack() { return bRequiresRoundFunctionHack; }
	static FORCEINLINE bool RequiresDisabledEarlyFragmentTests() { return bRequiresDisabledEarlyFragmentTests; }
	static FORCEINLINE bool RequiresARMShaderFramebufferFetchDepthStencilUndef() { return bRequiresARMShaderFramebufferFetchDepthStencilUndef; }
	static FORCEINLINE bool IsCheckingShaderCompilerHacks() { return bIsCheckingShaderCompilerHacks; }
	static FORCEINLINE bool IsLimitingShaderCompileCount() { return bIsLimitingShaderCompileCount; }

	// Adreno doesn't support HALF_FLOAT
	static FORCEINLINE int32 GetReadHalfFloatPixelsEnum() { return GL_FLOAT; }

	static FORCEINLINE GLenum GetVertexHalfFloatFormat() { return bES31Support ? GL_HALF_FLOAT : GL_HALF_FLOAT_OES; }
	static FORCEINLINE GLenum GetTextureHalfFloatPixelType() { return GL_HALF_FLOAT; }
	static FORCEINLINE GLenum GetTextureHalfFloatInternalFormat() { return GL_RGBA16F; }
	static FORCEINLINE GLint GetMaxMSAASamplesTileMem() { return MaxMSAASamplesTileMem; }
	static FORCEINLINE bool NeedsVertexAttribRemapTable() { return bNeedsVertexAttribRemap; }

	// On iOS both glMapBufferOES() and glBufferSubData() for immediate vertex and index data
	// is the slow path (they both hit GPU sync and data cache flush in driver according to profiling in driver symbols).
	// Turning this to false reverts back to not using vertex and index buffers
	// for glDrawArrays() and glDrawElements() on dynamic data.
	static FORCEINLINE bool SupportsFastBufferData() { return false; }

	// ES 2 will not work with non-power of two textures with non-clamp mode
	static FORCEINLINE bool SupportsTextureNPOT() { return bSupportsTextureNPOT; }

	// Optional
	static FORCEINLINE void BeginQuery(GLenum QueryType, GLuint QueryId)
	{
		check(QueryType == UGL_ANY_SAMPLES_PASSED || SupportsDisjointTimeQueries());
		glBeginQueryEXT(QueryType, QueryId);
	}

	static FORCEINLINE void EndQuery(GLenum QueryType)
	{
		check(QueryType == UGL_ANY_SAMPLES_PASSED || SupportsDisjointTimeQueries());
		glEndQueryEXT(QueryType);
	}

	static FORCEINLINE void QueryTimestampCounter(GLuint QueryID) UGL_OPTIONAL_VOID

	static FORCEINLINE void GenQueries(GLsizei NumQueries, GLuint* QueryIDs)
	{
		glGenQueriesEXT(NumQueries, QueryIDs);
	}

	static FORCEINLINE void DeleteQueries(GLsizei NumQueries, const GLuint* QueryIDs)
	{
		glDeleteQueriesEXT(NumQueries, QueryIDs);
	}

	static FORCEINLINE void GetQueryObject(GLuint QueryId, EQueryMode QueryMode, GLuint* OutResult)
	{
		GLenum QueryName = (QueryMode == QM_Result) ? GL_QUERY_RESULT_EXT : GL_QUERY_RESULT_AVAILABLE_EXT;
		glGetQueryObjectuivEXT(QueryId, QueryName, OutResult);
	}

	static FORCEINLINE void GetQueryObject(GLuint QueryId, EQueryMode QueryMode, uint64* OutResult) UGL_OPTIONAL_VOID

		static FORCEINLINE void LabelObject(GLenum Type, GLuint Object, const ANSICHAR* Name)
	{
		if (glLabelObjectEXT != nullptr)
		{
			glLabelObjectEXT(Type, Object, 0, Name);
		}
	}

	static FORCEINLINE GLsizei GetLabelObject(GLenum Type, GLuint Object, GLsizei BufferSize, ANSICHAR* OutName)
	{
		GLsizei Length = 0;
		if (glGetObjectLabelEXT != nullptr)
		{
			glGetObjectLabelEXT(Type, Object, BufferSize, &Length, OutName);
		}
		return Length;
	}

	static FORCEINLINE void PushGroupMarker(const ANSICHAR* Name)
	{
		if (glPushGroupMarkerEXT != nullptr)
		{
			glPushGroupMarkerEXT(0, Name);
		}
	}

	static FORCEINLINE void PopGroupMarker()
	{
		if (glPopGroupMarkerEXT != nullptr)
		{
			glPopGroupMarkerEXT();
		}
	}

	static FORCEINLINE void* MapBufferRange(GLenum Type, uint32 InOffset, uint32 InSize, EResourceLockMode LockMode)
	{
		GLenum Access;
		switch (LockMode)
		{
		case EResourceLockMode::RLM_ReadOnly:
			Access = GL_MAP_READ_BIT;
			break;
		case EResourceLockMode::RLM_WriteOnly:
			Access = (GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
			break;
		case EResourceLockMode::RLM_WriteOnlyUnsynchronized:
			Access = (GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
			break;
		case EResourceLockMode::RLM_WriteOnlyPersistent:
			Access = (GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
			break;
		case EResourceLockMode::RLM_ReadWrite:
		default:
			Access = (GL_MAP_READ_BIT | GL_MAP_WRITE_BIT);
	}
		return glMapBufferRange(Type, InOffset, InSize, Access);
	}

	static FORCEINLINE void UnmapBuffer(GLenum Type)
	{
		glUnmapBuffer(Type);
	}

	static FORCEINLINE void UnmapBufferRange(GLenum Type, uint32 InOffset, uint32 InSize)
	{
		UnmapBuffer(Type);
	}

	static FORCEINLINE void TexImage3D(GLenum Target, GLint Level, GLint InternalFormat, GLsizei Width, GLsizei Height, GLsizei Depth, GLint Border, GLenum Format, GLenum Type, const GLvoid* PixelData)
	{
		glTexImage3D(Target, Level, InternalFormat, Width, Height, Depth, Border, Format, Type, PixelData);
	}

	static FORCEINLINE void CompressedTexImage3D(GLenum Target, GLint Level, GLenum InternalFormat, GLsizei Width, GLsizei Height, GLsizei Depth, GLint Border, GLsizei ImageSize, const GLvoid* PixelData)
	{
		glCompressedTexImage3D(Target, Level, InternalFormat, Width, Height, Depth, Border, ImageSize, PixelData);
	}

	static FORCEINLINE void TexSubImage3D(GLenum Target, GLint Level, GLint XOffset, GLint YOffset, GLint ZOffset, GLsizei Width, GLsizei Height, GLsizei Depth, GLenum Format, GLenum Type, const GLvoid* PixelData)
	{
		glTexSubImage3D(Target, Level, XOffset, YOffset, ZOffset, Width, Height, Depth, Format, Type, PixelData);
	}

	static FORCEINLINE void	CopyTexSubImage1D(GLenum Target, GLint Level, GLint XOffset, GLint X, GLint Y, GLsizei Width)
	{
	}

	static FORCEINLINE void	CopyTexSubImage2D(GLenum Target, GLint Level, GLint XOffset, GLint YOffset, GLint X, GLint Y, GLsizei Width, GLsizei Height)
	{
		glCopyTexSubImage2D(Target, Level, XOffset, YOffset, X, Y, Width, Height);
	}

	static FORCEINLINE void	CopyTexSubImage3D(GLenum Target, GLint Level, GLint XOffset, GLint YOffset, GLint ZOffset, GLint X, GLint Y, GLsizei Width, GLsizei Height)
	{
		glCopyTexSubImage3D(Target, Level, XOffset, YOffset, ZOffset, X, Y, Width, Height);
	}

	static FORCEINLINE void CopyImageSubData(GLuint SrcName, GLenum SrcTarget, GLint SrcLevel, GLint SrcX, GLint SrcY, GLint SrcZ, GLuint DstName, GLenum DstTarget, GLint DstLevel, GLint DstX, GLint DstY, GLint DstZ, GLsizei Width, GLsizei Height, GLsizei Depth)
	{
		check(bSupportsCopyImage);

		glCopyImageSubData(SrcName, SrcTarget, SrcLevel, SrcX, SrcY, SrcZ, DstName, DstTarget, DstLevel, DstX, DstY, DstZ, Width, Height, Depth);
	}

	static FORCEINLINE void ClearBufferfv(GLenum Buffer, GLint DrawBufferIndex, const GLfloat* Value)
	{
		glClearBufferfv(Buffer, DrawBufferIndex, Value);
	}

	static FORCEINLINE void ClearBufferfi(GLenum Buffer, GLint DrawBufferIndex, GLfloat Depth, GLint Stencil)
	{
		glClearBufferfi(Buffer, DrawBufferIndex, Depth, Stencil);
	}

	static FORCEINLINE void ClearBufferiv(GLenum Buffer, GLint DrawBufferIndex, const GLint* Value)
	{
		glClearBufferiv(Buffer, DrawBufferIndex, Value);
	}

	static FORCEINLINE void DrawBuffers(GLsizei NumBuffers, const GLenum* Buffers)
	{
		glDrawBuffers(NumBuffers, Buffers);
	}

	static FORCEINLINE void ReadBuffer(GLenum Mode)
	{
		glReadBuffer(Mode);
	}

	static FORCEINLINE void ColorMaskIndexed(GLuint Index, GLboolean Red, GLboolean Green, GLboolean Blue, GLboolean Alpha)
	{
		if (Index > 0 && glColorMaskiEXT)
		{
			// ext or OpenGL ES 3.2
			glColorMaskiEXT(Index, Red, Green, Blue, Alpha);
		}
		else
		{
			glColorMask(Red, Green, Blue, Alpha);
		}
	}

	static FORCEINLINE void TexBuffer(GLenum Target, GLenum InternalFormat, GLuint Buffer)
	{
		glTexBufferEXT(Target, InternalFormat, Buffer);
	}

	static FORCEINLINE void TexBufferRange(GLenum Target, GLenum InternalFormat, GLuint Buffer, GLintptr Offset, GLsizeiptr Size)
	{
		glTexBufferRangeEXT(Target, InternalFormat, Buffer, Offset, Size);
	}

	static FORCEINLINE void ProgramUniform4uiv(GLuint Program, GLint Location, GLsizei Count, const GLuint* Value)
	{
		glUniform4uiv(Location, Count, Value);
	}

	static FORCEINLINE bool SupportsProgramBinary() { return bSupportsProgramBinary; }

	static FORCEINLINE void GetProgramBinary(GLuint Program, GLsizei BufSize, GLsizei* Length, GLenum* BinaryFormat, void* Binary)
	{
		glGetProgramBinary(Program, BufSize, Length, BinaryFormat, Binary);
	}

	static FORCEINLINE void ProgramBinary(GLuint Program, GLenum BinaryFormat, const void* Binary, GLsizei Length)
	{
		glProgramBinary(Program, BinaryFormat, Binary, Length);
	}

	static FORCEINLINE void ProgramParameter(GLuint Program, GLenum PName, GLint Value)
	{
		check(glProgramParameteri);
		glProgramParameteri(Program, PName, Value);
	}

	static FORCEINLINE void BindBufferBase(GLenum Target, GLuint Index, GLuint Buffer)
	{
		check(IsES31Usable());
		glBindBufferBase(Target, Index, Buffer);
	}

	static FORCEINLINE void BindBufferRange(GLenum Target, GLuint Index, GLuint Buffer, GLintptr Offset, GLsizeiptr Size)
	{
		check(IsES31Usable());
		glBindBufferRange(Target, Index, Buffer, Offset, Size);
	}

	static FORCEINLINE GLuint GetUniformBlockIndex(GLuint Program, const GLchar* UniformBlockName)
	{
		check(IsES31Usable());
		return glGetUniformBlockIndex(Program, UniformBlockName);
	}

	static FORCEINLINE void UniformBlockBinding(GLuint Program, GLuint UniformBlockIndex, GLuint UniformBlockBinding)
	{
		check(IsES31Usable());
		glUniformBlockBinding(Program, UniformBlockIndex, UniformBlockBinding);
	}

	static FORCEINLINE void BufferSubData(GLenum Target, GLintptr Offset, GLsizeiptr Size, const GLvoid* Data)
	{
		check(Target == GL_ARRAY_BUFFER || Target == GL_ELEMENT_ARRAY_BUFFER || (Target == GL_UNIFORM_BUFFER && IsES31Usable()));
		glBufferSubData(Target, Offset, Size, Data);
	}

	static FORCEINLINE void VertexAttribIPointer(GLuint Index, GLint Size, GLenum Type, GLsizei Stride, const GLvoid* Pointer)
	{
		if (IsES31Usable())
		{
			glVertexAttribIPointer(Index, Size, Type, Stride, Pointer);
		}
		else
		{
			glVertexAttribPointer(Index, Size, Type, GL_FALSE, Stride, Pointer);
		}
	}

	static FORCEINLINE void GenSamplers(GLsizei Count, GLuint* Samplers)
	{
		glGenSamplers(Count, Samplers);
	}

	static FORCEINLINE void DeleteSamplers(GLsizei Count, GLuint* Samplers)
	{
		glDeleteSamplers(Count, Samplers);
	}

	static FORCEINLINE void SetSamplerParameter(GLuint Sampler, GLenum Parameter, GLint Value)
	{
		glSamplerParameteri(Sampler, Parameter, Value);
	}

	static FORCEINLINE void BindSampler(GLuint Unit, GLuint Sampler)
	{
		glBindSampler(Unit, Sampler);
	}

	static FORCEINLINE void MemoryBarrier(GLbitfield Barriers)
	{
		glMemoryBarrier(Barriers);
	}

	static FORCEINLINE void DispatchCompute(GLuint NumGroupsX, GLuint NumGroupsY, GLuint NumGroupsZ)
	{
		glDispatchCompute(NumGroupsX, NumGroupsY, NumGroupsZ);
	}

	static FORCEINLINE void DispatchComputeIndirect(GLintptr Offset)
	{
		glDispatchComputeIndirect(Offset);
	}

	static FORCEINLINE void BindImageTexture(GLuint Unit, GLuint Texture, GLint Level, GLboolean Layered, GLint Layer, GLenum Access, GLenum Format)
	{
		glBindImageTexture(Unit, Texture, Level, Layered, Layer, Access, Format);
	}
	
	

	static FORCEINLINE void DepthRange(GLdouble Near, GLdouble Far)
	{
		glDepthRangef(Near, Far);
	}

	static FORCEINLINE void EnableIndexed(GLenum Parameter, GLuint Index)
	{
		// We don't have MRT on ES2 and Index was used for RenderTargetIndex so Index can be ignore, other Parameters might not work.
		check(Parameter == GL_BLEND);

		glEnable(Parameter);
	}

	static FORCEINLINE void DisableIndexed(GLenum Parameter, GLuint Index)
	{
		// We don't have MRT on ES2 and Index was used for RenderTargetIndex so Index can be ignore, other Parameters might not work.
		check(Parameter == GL_BLEND);

		glDisable(Parameter);
	}

	static FORCEINLINE void VertexAttribPointer(GLuint Index, GLint Size, GLenum Type, GLboolean Normalized, GLsizei Stride, const GLvoid* Pointer)
	{
		Size = (Size == GL_BGRA) ? 4 : Size;
		glVertexAttribPointer(Index, Size, Type, Normalized, Stride, Pointer);
	}

	static FORCEINLINE void ClearDepth(GLdouble Depth)
	{
		glClearDepthf(Depth);
	}

	static FORCEINLINE GLuint GetMajorVersion()
	{
		return 3;
	}

	static FORCEINLINE GLuint GetMinorVersion()
	{
		return 1;
	}

	static FORCEINLINE EShaderPlatform GetShaderPlatform()
	{
		return SP_OPENGL_ES3_1_ANDROID;
	}

	static FORCEINLINE ERHIFeatureLevel::Type GetFeatureLevel()
	{
		return ERHIFeatureLevel::ES3_1;
	}

	static FORCEINLINE FString GetAdapterName()
	{
		return (TCHAR*)ANSI_TO_TCHAR((const ANSICHAR*)glGetString(GL_RENDERER));
	}

	static FORCEINLINE void DrawBuffer(GLenum Mode)
	{
	}

	static FORCEINLINE void TexParameter(GLenum Target, GLenum Parameter, GLint Value)
	{
		glTexParameteri(Target, Parameter, Value);
	}

	static FORCEINLINE void FramebufferTexture(GLenum Target, GLenum Attachment, GLuint Texture, GLint Level)
	{
		check(0);
	}

	static FORCEINLINE void FramebufferTexture3D(GLenum Target, GLenum Attachment, GLenum TexTarget, GLuint Texture, GLint Level, GLint ZOffset)
	{
		check(0);
	}

	static FORCEINLINE void FramebufferTextureLayer(GLenum Target, GLenum Attachment, GLuint Texture, GLint Level, GLint Layer)
	{
		glFramebufferTextureLayer(Target, Attachment, Texture, Level, Layer);
	}

	static FORCEINLINE void FramebufferTexture2D(GLenum Target, GLenum Attachment, GLenum TexTarget, GLuint Texture, GLint Level)
	{
		check(Attachment == GL_COLOR_ATTACHMENT0 || Attachment == GL_DEPTH_ATTACHMENT || Attachment == GL_STENCIL_ATTACHMENT
			|| (SupportsMultipleRenderTargets() && Attachment >= GL_COLOR_ATTACHMENT0 && Attachment <= GL_COLOR_ATTACHMENT7));

		glFramebufferTexture2D(Target, Attachment, TexTarget, Texture, Level);
		VERIFY_GL(FramebufferTexture_2D);
	}

	static FORCEINLINE void BlitFramebuffer(GLint SrcX0, GLint SrcY0, GLint SrcX1, GLint SrcY1, GLint DstX0, GLint DstY0, GLint DstX1, GLint DstY1, GLbitfield Mask, GLenum Filter)
	{
		if (IsES31Usable())
		{
			glBlitFramebuffer(SrcX0, SrcY0, SrcX1, SrcY1, DstX0, DstY0, DstX1, DstY1, Mask, Filter);
		}
	}

	static FORCEINLINE bool TexStorage2D(GLenum Target, GLint Levels, GLint InternalFormat, GLsizei Width, GLsizei Height, GLenum Format, GLenum Type, uint32 Flags)
	{
		// glTexStorage2D accepts only sized internal formats and thus we reject base formats
		// also GL_BGRA8_EXT seems to be unsupported
		bool bValidFormat = true;
		switch (InternalFormat)
		{
		case GL_DEPTH_COMPONENT:
		case GL_DEPTH_STENCIL:
		case GL_RED:
		case GL_RG:
		case GL_RGB:
		case GL_RGBA:
		case GL_BGRA_EXT:
		case GL_BGRA8_EXT:
		case GL_LUMINANCE:
		case GL_LUMINANCE_ALPHA:
		case GL_ALPHA:
		case GL_RED_INTEGER:
		case GL_RG_INTEGER:
		case GL_RGB_INTEGER:
		case GL_RGBA_INTEGER:
			bValidFormat = false;
			break;
		}

		if (bValidFormat || (bUseHalfFloatTexStorage && Type == GetTextureHalfFloatPixelType() && (Flags & TexCreate_RenderTargetable) != 0))
		{
			glTexStorage2D(Target, Levels, InternalFormat, Width, Height);
			VERIFY_GL(glTexStorage2D);
			return true;
		}

		return false;
	}

	static FORCEINLINE void DrawArraysInstanced(GLenum Mode, GLint First, GLsizei Count, GLsizei InstanceCount)
	{
		glDrawArraysInstanced(Mode, First, Count, InstanceCount);
	}

	static FORCEINLINE void DrawElementsInstanced(GLenum Mode, GLsizei Count, GLenum Type, const GLvoid* Indices, GLsizei InstanceCount)
	{
		glDrawElementsInstanced(Mode, Count, Type, Indices, InstanceCount);
	}

	static FORCEINLINE void CopyBufferSubData(GLenum ReadTarget, GLenum WriteTarget, GLintptr ReadOffset, GLintptr WriteOffset, GLsizeiptr Size)
	{
		glCopyBufferSubData(ReadTarget, WriteTarget, ReadOffset, WriteOffset, Size);
	}

	static FORCEINLINE void DrawArraysIndirect(GLenum Mode, const void* Offset)
	{
		glDrawArraysIndirect(Mode, Offset);
	}

	static FORCEINLINE void DrawElementsIndirect(GLenum Mode, GLenum Type, const void* Offset)
	{
		glDrawElementsIndirect(Mode, Type, Offset);
	}

	static FORCEINLINE void VertexAttribDivisor(GLuint Index, GLuint Divisor)
	{
		glVertexAttribDivisor(Index, Divisor);
	}

	static FORCEINLINE void TexStorage3D(GLenum Target, GLint Levels, GLint InternalFormat, GLsizei Width, GLsizei Height, GLsizei Depth, GLenum Format, GLenum Type)
	{
		if (glTexStorage3D)
		{
			glTexStorage3D(Target, Levels, InternalFormat, Width, Height, Depth);
		}
		else
		{
			const bool bArrayTexture = Target == GL_TEXTURE_2D_ARRAY || Target == GL_TEXTURE_CUBE_MAP_ARRAY;
			for (uint32 MipIndex = 0; MipIndex < uint32(Levels); MipIndex++)
			{
				glTexImage3D(
					Target,
					MipIndex,
					InternalFormat,
					FMath::Max<uint32>(1, (Width >> MipIndex)),
					FMath::Max<uint32>(1, (Height >> MipIndex)),
					(bArrayTexture) ? Depth : FMath::Max<uint32>(1, (Depth >> MipIndex)),
					0,
					Format,
					Type,
					NULL
				);

				VERIFY_GL(TexImage_3D);
			}
		}
	}

	// 	static FORCEINLINE void BindBufferBase(GLenum Target, GLuint Index, GLuint Buffer) UGL_OPTIONAL_VOID
	// 	static FORCEINLINE GLuint GetUniformBlockIndex(GLuint Program, const GLchar *UniformBlockName) UGL_REQUIRED(GL_INVALID_INDEX)


	static FPlatformOpenGLDevice* CreateDevice()	UGL_REQUIRED(NULL)
	static FPlatformOpenGLContext* CreateContext(FPlatformOpenGLDevice* Device, void* WindowHandle)	UGL_REQUIRED(NULL)

	static FORCEINLINE void DiscardFramebufferEXT(GLenum Target, GLsizei NumAttachments, const GLenum* Attachments)
	{
		glDiscardFramebufferEXT(Target, NumAttachments, Attachments);
	}

	static FORCEINLINE void GenBuffers(GLsizei n, GLuint* buffers)
	{
		glGenBuffers(n, buffers);
	}

	static FORCEINLINE void GenTextures(GLsizei n, GLuint* textures)
	{
		glGenTextures(n, textures);
	}

	static FORCEINLINE bool TimerQueryDisjoint()
	{
		bool Disjoint = false;

		if (bTimerQueryCanBeDisjoint)
		{
			GLint WasDisjoint = 0;
			glGetIntegerv(GL_GPU_DISJOINT_EXT, &WasDisjoint);
			Disjoint = (WasDisjoint != 0);
		}

		return Disjoint;
	}

protected:

	/** GL_OES_vertex_array_object */
	static bool bSupportsVertexArrayObjects;

	/** GL_OES_depth_texture */
	static bool bSupportsDepthTexture;

	/** GL_OES_mapbuffer */
	static bool bSupportsMapBuffer;

	/** GL_ARB_occlusion_query2, GL_EXT_occlusion_query_boolean */
	static bool bSupportsOcclusionQueries;

	/** GL_EXT_disjoint_timer_query */
	static bool bSupportsDisjointTimeQueries;

	/** Some timer query implementations are never disjoint */
	static bool bTimerQueryCanBeDisjoint;

	/** GL_OES_rgb8_rgba8 */
	static bool bSupportsRGBA8;

	/** GL_APPLE_texture_format_BGRA8888 */
	static bool bSupportsBGRA8888;

	/** Whether BGRA supported as color attachment */
	static bool bSupportsBGRA8888RenderTarget;

	/** GL_OES_vertex_half_float */
	static bool bSupportsVertexHalfFloat;

	/** GL_EXT_discard_framebuffer */
	static bool bSupportsDiscardFrameBuffer;

	/** GL_NV_texture_compression_s3tc, GL_EXT_texture_compression_s3tc */
	static bool bSupportsDXT;

	/** OpenGL ES 3.0 profile */
	static bool bSupportsETC2;

	/** GL_OES_texture_float */
	static bool bSupportsTextureFloat;

	/** GL_OES_texture_half_float */
	static bool bSupportsTextureHalfFloat;

	/** GL_EXT_color_buffer_float */
	static bool bSupportsColorBufferFloat;

	/** GL_EXT_color_buffer_half_float */
	static bool bSupportsColorBufferHalfFloat;

	/** GL_EXT_shader_framebuffer_fetch */
	static bool bSupportsShaderFramebufferFetch;

	/** workaround for GL_EXT_shader_framebuffer_fetch */
	static bool bRequiresUEShaderFramebufferFetchDef;

	/** GL_ARM_shader_framebuffer_fetch_depth_stencil */
	static bool bSupportsShaderDepthStencilFetch;

	/** GL_EXT_MULTISAMPLED_RENDER_TO_TEXTURE */
	static bool bSupportsMultisampledRenderToTexture;

	/** GL_FRAGMENT_SHADER, GL_LOW_FLOAT */
	static int ShaderLowPrecision;

	/** GL_FRAGMENT_SHADER, GL_MEDIUM_FLOAT */
	static int ShaderMediumPrecision;

	/** GL_FRAGMENT_SHADER, GL_HIGH_FLOAT */
	static int ShaderHighPrecision;

	/** GL_NV_framebuffer_blit */
	static bool bSupportsNVFrameBufferBlit;

	/** GL_OES_packed_depth_stencil */
	static bool bSupportsPackedDepthStencil;

	/** textureCubeLodEXT */
	static bool bSupportsTextureCubeLodEXT;

	/** GL_EXT_shader_texture_lod */
	static bool bSupportsShaderTextureLod;

	/** textureCubeLod */
	static bool bSupportsShaderTextureCubeLod;

	/** GL_APPLE_copy_texture_levels */
	static bool bSupportsCopyTextureLevels;

	/** GL_OES_texture_npot */
	static bool bSupportsTextureNPOT;

	/** GL_EXT_texture_storage */
	static bool bSupportsTextureStorageEXT;

	/** GL_OES_standard_derivations */
	static bool bSupportsStandardDerivativesExtension;

	/** Vertex attributes need remapping if GL_MAX_VERTEX_ATTRIBS < 16 */
	static bool bNeedsVertexAttribRemap;

	/** Maximum number of MSAA samples supported on chip in tile memory, or 1 if not available */
	static GLint MaxMSAASamplesTileMem;
public:
	/* This is a hack to remove the calls to "precision sampler" defaults which are produced by the cross compiler however don't compile on some android platforms */
	static bool bRequiresDontEmitPrecisionForTextureSamplers;

	/* Some android platforms require textureCubeLod to be used some require textureCubeLodEXT however they either inconsistently or don't use the GL_TextureCubeLodEXT extension definition */
	static bool bRequiresTextureCubeLodEXTToTextureCubeLodDefine;

	/* This is a hack to remove the gl_FragCoord if shader will fail to link if exceeding the max varying on android platforms */
	static bool bRequiresGLFragCoordVaryingLimitHack;

	/* This indicates failure when attempting to retrieve driver's binary representation of the hack program  */
	static bool bBinaryProgramRetrievalFailed;

	/* This hack fixes an issue with SGX540 compiler which can get upset with some operations that mix highp and mediump */
	static bool bRequiresTexture2DPrecisionHack;

	/* This is a hack to add a round() function when not available to a shader compiler */
	static bool bRequiresRoundFunctionHack;

	/* Some Mali devices do not work correctly with early_fragment_test enabled */
	static bool bRequiresDisabledEarlyFragmentTests;
		
	/* This is to avoid a bug in Adreno drivers that define GL_ARM_shader_framebuffer_fetch_depth_stencil even when device does not support this extension  */
	static bool bRequiresARMShaderFramebufferFetchDepthStencilUndef;

	/* Indicates shader compiler hack checks are being tested */
	static bool bIsCheckingShaderCompilerHacks;

	/** GL_OES_vertex_type_10_10_10_2 */
	static bool bSupportsRGB10A2;

	/** GL_OES_get_program_binary */
	static bool bSupportsProgramBinary;

	/* Indicates shader compiler should be limited */
	static bool bIsLimitingShaderCompileCount;

	enum class EFeatureLevelSupport : uint8
	{
		Invalid,	// no feature level has yet been determined
		ES2,
		ES31,
		ES32
	};

	/** Describes which feature level is currently being supported */
	static EFeatureLevelSupport CurrentFeatureLevelSupport;

	// whether to use ES 3.0 function glTexStorage2D to allocate storage for GL_HALF_FLOAT_OES render target textures
	static bool bUseHalfFloatTexStorage;

	// GL_EXT_texture_buffer
	static bool bSupportsTextureBuffer;

	// whether to use ES 3.0 shading language
	static bool bUseES30ShadingLanguage;

	// whether device supports ES 3.1
	static bool bES31Support;

	/** Whether device supports Hidden Surface Removal */
	static bool bHasHardwareHiddenSurfaceRemoval;

	/** Whether device supports mobile multi-view */
	static bool bSupportsMobileMultiView;

	static GLint MaxComputeTextureImageUnits;
	static GLint MaxComputeUniformComponents;

	static GLint MaxCombinedUAVUnits;
	static GLint MaxComputeUAVUnits;
	static GLint MaxPixelUAVUnits;
};

#endif //desktop
