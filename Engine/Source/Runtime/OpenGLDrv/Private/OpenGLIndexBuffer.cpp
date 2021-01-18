// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	OpenGLIndexBuffer.cpp: OpenGL Index buffer RHI implementation.
=============================================================================*/

#include "CoreMinimal.h"
#include "Containers/ResourceArray.h"
#include "OpenGLDrv.h"

void FOpenGLDynamicRHI::RHITransferBufferUnderlyingResource(FRHIBuffer* DestBuffer, FRHIBuffer* SrcBuffer)
{
	VERIFY_GL_SCOPE();
	check(DestBuffer);
	FOpenGLBuffer* Dest = ResourceCast(DestBuffer);
	if (!SrcBuffer)
	{
		TRefCountPtr<FOpenGLBuffer> Src = new FOpenGLBuffer();
		Dest->Swap(*Src);
	}
	else
	{
		FOpenGLBuffer* Src = ResourceCast(SrcBuffer);
		Dest->Swap(*Src);
	}
}
