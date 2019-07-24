// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OpenGLDrv.h"
#include "OpenGLResources.h"


#if PLATFORM_WINDOWS
// This is redeclaration of WINDOWS specific FPlatformOpenGLContext
// which is declared in private OpenGLWindows.cpp file.
//@note: Keep it synced with original type (Engine\Source\Runtime\OpenGLDrv\Private\Windows\OpenGLWindows.cpp)
struct FPlatformOpenGLContext
{
	HWND WindowHandle;
	HDC DeviceContext;
	HGLRC OpenGLContext;
	bool bReleaseWindowOnDestroy;
	int32 SyncInterval;
	GLuint	ViewportFramebuffer;
	GLuint	VertexArrayObject;	// one has to be generated and set for each context (OpenGL 3.2 Core requirements)
	GLuint	BackBufferResource;
	GLenum	BackBufferTarget;
};
#endif


#if PLATFORM_LINUX
// This is redeclaration of LINUX specific FPlatformOpenGLContext
// which is declared in private OpenGLWindows.cpp file.
//@note: Keep it synced with original type (Engine\Source\Runtime\OpenGLDrv\Private\Linux\OpenGLLinux.cpp)
struct FPlatformOpenGLContext
{
	SDL_HWindow    hWnd;
	SDL_HGLContext hGLContext; // this is a (void*) pointer

	bool bReleaseWindowOnDestroy;
	int32 SyncInterval;
	GLuint	ViewportFramebuffer;
	GLuint	VertexArrayObject;	// one has to be generated and set for each context (OpenGL 3.2 Core requirements)
};

//@note: Place here any Linux targeted device implementations
#endif
