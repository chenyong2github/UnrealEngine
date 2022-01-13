// Copyright Epic Games, Inc. All Rights Reserved.

#include "AGXRHIPrivate.h"

#include "AGXDebugCommandEncoder.h"
#include "AGXCommandBuffer.h"
#include "AGXFence.h"

#if MTLPP_CONFIG_VALIDATE && METAL_DEBUG_OPTIONS
extern int32 GAGXRuntimeDebugLevel;

@implementation FAGXDebugCommandEncoder

APPLE_PLATFORM_OBJECT_ALLOC_OVERRIDES(FAGXDebugCommandEncoder)

-(instancetype)init
{
	id Self = [super init];
	if (Self)
	{
		UpdatedFences = [[NSHashTable weakObjectsHashTable] retain];
		WaitingFences = [[NSHashTable weakObjectsHashTable] retain];
	}
	return Self;
}

-(void)dealloc
{
	[UpdatedFences release];
	[WaitingFences release];
	[super dealloc];
}
@end

FAGXCommandEncoderDebugging::FAGXCommandEncoderDebugging()
{
	
}

FAGXCommandEncoderDebugging::FAGXCommandEncoderDebugging(FAGXDebugCommandEncoder* handle)
: ns::Object<FAGXDebugCommandEncoder*>(handle)
{
	
}

void FAGXCommandEncoderDebugging::AddUpdateFence(id Fence)
{
	if ((EAGXDebugLevel)GAGXRuntimeDebugLevel >= EAGXDebugLevelValidation && Fence)
	{
		[m_ptr->UpdatedFences addObject:(FAGXDebugFence*)Fence];
		[(FAGXDebugFence*)Fence updatingEncoder:m_ptr];
	}
}

void FAGXCommandEncoderDebugging::AddWaitFence(id Fence)
{
	if ((EAGXDebugLevel)GAGXRuntimeDebugLevel >= EAGXDebugLevelValidation && Fence)
	{
		[m_ptr->WaitingFences addObject:(FAGXDebugFence*)Fence];
		[(FAGXDebugFence*)Fence waitingEncoder:m_ptr];
	}
}
#endif

