// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXCommandBuffer.cpp: AGX RHI command buffer wrapper.
=============================================================================*/

#include "AGXRHIPrivate.h"

#include "AGXCommandBuffer.h"
#include "AGXRenderCommandEncoder.h"
#include "AGXBlitCommandEncoder.h"
#include "AGXComputeCommandEncoder.h"
#include <objc/runtime.h>

NSString* GAGXDebugCommandTypeNames[EAGXDebugCommandTypeInvalid] = {
	@"RenderEncoder",
	@"ComputeEncoder",
	@"BlitEncoder",
    @"EndEncoder",
    @"Pipeline",
	@"Draw",
	@"Dispatch",
	@"Blit",
	@"Signpost",
	@"PushGroup",
	@"PopGroup"
};

extern int32 GAGXRuntimeDebugLevel;

uint32 AGXSafeGetRuntimeDebuggingLevel()
{
	return GIsRHIInitialized ? GetAGXDeviceContext().GetCommandQueue().GetRuntimeDebuggingLevel() : GAGXRuntimeDebugLevel;
}

#if MTLPP_CONFIG_VALIDATE && METAL_DEBUG_OPTIONS

@implementation FAGXDebugCommandBuffer

APPLE_PLATFORM_OBJECT_ALLOC_OVERRIDES(FAGXDebugCommandBuffer)

-(id)initWithCommandBuffer:(id<MTLCommandBuffer>)Buffer
{
	id Self = [super init];
	if (Self)
	{
        DebugLevel = (EAGXDebugLevel)GAGXRuntimeDebugLevel;
		InnerBuffer = Buffer;
		DebugGroup = [NSMutableArray new];
		ActiveEncoder = nil;
		DebugInfoBuffer = nil;
		if (DebugLevel >= EAGXDebugLevelValidation)
		{
			DebugInfoBuffer = [Buffer.device newBufferWithLength:BufferOffsetAlignment options:0];
		}
	}
	return Self;
}

-(void)dealloc
{
	for (FAGXDebugCommand* Command : DebugCommands)
	{
		[Command->Label release];
		[Command->PassDesc release];
		delete Command;
	}
	
	[DebugGroup release];
	[DebugInfoBuffer release];
	DebugInfoBuffer = nil;
	
	[super dealloc];
}

@end

FAGXCommandBufferDebugging FAGXCommandBufferDebugging::Get(mtlpp::CommandBuffer& Buffer)
{
	return Buffer.GetAssociatedObject<FAGXCommandBufferDebugging>((void const*)&FAGXCommandBufferDebugging::Get);
}

FAGXCommandBufferDebugging::FAGXCommandBufferDebugging()
: ns::Object<FAGXDebugCommandBuffer*>(nullptr)
{
	
}
FAGXCommandBufferDebugging::FAGXCommandBufferDebugging(mtlpp::CommandBuffer& Buffer)
: ns::Object<FAGXDebugCommandBuffer*>([[FAGXDebugCommandBuffer alloc] initWithCommandBuffer:Buffer.GetPtr()], ns::Ownership::Assign)
{
	Buffer.SetAssociatedObject((void const*)&FAGXCommandBufferDebugging::Get, *this);
}
FAGXCommandBufferDebugging::FAGXCommandBufferDebugging(FAGXDebugCommandBuffer* handle)
: ns::Object<FAGXDebugCommandBuffer*>(handle)
{
	
}

ns::AutoReleased<ns::String> FAGXCommandBufferDebugging::GetDescription()
{
	NSMutableString* String = [[NSMutableString new] autorelease];
	NSString* Label = m_ptr->InnerBuffer.label ? m_ptr->InnerBuffer.label : @"Unknown";
	[String appendFormat:@"Command Buffer %p %@:", m_ptr->InnerBuffer, Label];
	return ns::AutoReleased<ns::String>(String);
}

ns::AutoReleased<ns::String> FAGXCommandBufferDebugging::GetDebugDescription()
{
	NSMutableString* String = [[NSMutableString new] autorelease];
	NSString* Label = m_ptr->InnerBuffer.label ? m_ptr->InnerBuffer.label : @"Unknown";
	[String appendFormat:@"Command Buffer %p %@:", m_ptr->InnerBuffer, Label];
	
	uint32 Index = 0;
	if (m_ptr->DebugInfoBuffer)
	{
		Index = *((uint32*)m_ptr->DebugInfoBuffer.contents);
	}
	
	uint32 Count = 1;
	for (FAGXDebugCommand* Command : m_ptr->DebugCommands)
	{
		if (Index == Count++)
		{
			[String appendFormat:@"\n\t--> %@: %@", GAGXDebugCommandTypeNames[Command->Type], Command->Label];
		}
		else
		{
			[String appendFormat:@"\n\t%@: %@", GAGXDebugCommandTypeNames[Command->Type], Command->Label];
		}
	}
	
	return ns::AutoReleased<ns::String>(String);
}

void FAGXCommandBufferDebugging::BeginRenderCommandEncoder(ns::String const& Label, mtlpp::RenderPassDescriptor const& Desc)
{
	if (m_ptr->DebugLevel >= EAGXDebugLevelValidation)
	{
		if (m_ptr->DebugLevel >= EAGXDebugLevelLogOperations)
		{
			check(!m_ptr->ActiveEncoder);
			m_ptr->ActiveEncoder = [Label.GetPtr() retain];
			FAGXDebugCommand* Command = new FAGXDebugCommand;
			Command->Type = EAGXDebugCommandTypeRenderEncoder;
			Command->Label = [Label.GetPtr() retain];
			Command->PassDesc = [Desc.GetPtr() retain];
			m_ptr->DebugCommands.Add(Command);
		}
	}
}
void FAGXCommandBufferDebugging::BeginComputeCommandEncoder(ns::String const& Label)
{
	if (m_ptr->DebugLevel >= EAGXDebugLevelLogOperations)
	{
		check(!m_ptr->ActiveEncoder);
		m_ptr->ActiveEncoder = [Label.GetPtr() retain];
		FAGXDebugCommand* Command = new FAGXDebugCommand;
		Command->Type = EAGXDebugCommandTypeComputeEncoder;
		Command->Label = [m_ptr->ActiveEncoder retain];
		Command->PassDesc = nil;
		m_ptr->DebugCommands.Add(Command);
	}
}
void FAGXCommandBufferDebugging::BeginBlitCommandEncoder(ns::String const& Label)
{
	if (m_ptr->DebugLevel >= EAGXDebugLevelLogOperations)
	{
		check(!m_ptr->ActiveEncoder);
		m_ptr->ActiveEncoder = [Label.GetPtr() retain];
		FAGXDebugCommand* Command = new FAGXDebugCommand;
		Command->Type = EAGXDebugCommandTypeBlitEncoder;
		Command->Label = [m_ptr->ActiveEncoder retain];
		Command->PassDesc = nil;
		m_ptr->DebugCommands.Add(Command);
	}
}
void FAGXCommandBufferDebugging::EndCommandEncoder()
{
	if (m_ptr->DebugLevel >= EAGXDebugLevelLogOperations)
	{
		check(m_ptr->ActiveEncoder);
		FAGXDebugCommand* Command = new FAGXDebugCommand;
		Command->Type = EAGXDebugCommandTypeEndEncoder;
		Command->Label = m_ptr->ActiveEncoder;
		Command->PassDesc = nil;
		m_ptr->DebugCommands.Add(Command);
		m_ptr->ActiveEncoder = nil;
	}
}

void FAGXCommandBufferDebugging::SetPipeline(ns::String const& Desc)
{
	if (m_ptr->DebugLevel >= EAGXDebugLevelLogOperations)
	{
		FAGXDebugCommand* Command = new FAGXDebugCommand;
		Command->Type = EAGXDebugCommandTypePipeline;
		Command->Label = [Desc.GetPtr() retain];
		Command->PassDesc = nil;
		m_ptr->DebugCommands.Add(Command);
	}
}
void FAGXCommandBufferDebugging::Draw(ns::String const& Desc)
{
	if (m_ptr->DebugLevel >= EAGXDebugLevelLogOperations)
	{
		FAGXDebugCommand* Command = new FAGXDebugCommand;
		Command->Type = EAGXDebugCommandTypeDraw;
		Command->Label = [Desc.GetPtr() retain];
		Command->PassDesc = nil;
		m_ptr->DebugCommands.Add(Command);
	}
}
void FAGXCommandBufferDebugging::Dispatch(ns::String const& Desc)
{
	if (m_ptr->DebugLevel >= EAGXDebugLevelLogOperations)
	{
		FAGXDebugCommand* Command = new FAGXDebugCommand;
		Command->Type = EAGXDebugCommandTypeDispatch;
		Command->Label = [Desc.GetPtr() retain];
		Command->PassDesc = nil;
		m_ptr->DebugCommands.Add(Command);
	}
}
void FAGXCommandBufferDebugging::Blit(ns::String const& Desc)
{
	if (m_ptr->DebugLevel >= EAGXDebugLevelLogOperations)
	{
		FAGXDebugCommand* Command = new FAGXDebugCommand;
		Command->Type = EAGXDebugCommandTypeBlit;
		Command->Label = [Desc.GetPtr() retain];
		Command->PassDesc = nil;
		m_ptr->DebugCommands.Add(Command);
	}
}

void FAGXCommandBufferDebugging::InsertDebugSignpost(ns::String const& Label)
{
	if (m_ptr->DebugLevel >= EAGXDebugLevelLogOperations)
	{
		FAGXDebugCommand* Command = new FAGXDebugCommand;
		Command->Type = EAGXDebugCommandTypeSignpost;
		Command->Label = [Label.GetPtr() retain];
		Command->PassDesc = nil;
		m_ptr->DebugCommands.Add(Command);
	}
}
void FAGXCommandBufferDebugging::PushDebugGroup(ns::String const& Group)
{
	if (m_ptr->DebugLevel >= EAGXDebugLevelLogOperations)
	{
		[m_ptr->DebugGroup addObject:Group.GetPtr()];
		FAGXDebugCommand* Command = new FAGXDebugCommand;
		Command->Type = EAGXDebugCommandTypePushGroup;
		Command->Label = [Group.GetPtr() retain];
		Command->PassDesc = nil;
		m_ptr->DebugCommands.Add(Command);
	}
}
void FAGXCommandBufferDebugging::PopDebugGroup()
{
	if (m_ptr->DebugLevel >= EAGXDebugLevelLogOperations)
	{
		if (m_ptr->DebugGroup.lastObject)
		{
			FAGXDebugCommand* Command = new FAGXDebugCommand;
			Command->Type = EAGXDebugCommandTypePopGroup;
			Command->Label = [m_ptr->DebugGroup.lastObject retain];
			Command->PassDesc = nil;
			m_ptr->DebugCommands.Add(Command);
		}
	}
}

#endif
