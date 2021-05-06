// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <Metal/Metal.h>
#include "AGXResources.h"
#include "MetalShaderResources.h"

/**
 * EAGXDebugCommandType: Types of command recorded in our debug command-buffer wrapper.
 */
enum EAGXDebugCommandType
{
	EAGXDebugCommandTypeRenderEncoder,
	EAGXDebugCommandTypeComputeEncoder,
	EAGXDebugCommandTypeBlitEncoder,
	EAGXDebugCommandTypeEndEncoder,
    EAGXDebugCommandTypePipeline,
	EAGXDebugCommandTypeDraw,
	EAGXDebugCommandTypeDispatch,
	EAGXDebugCommandTypeBlit,
	EAGXDebugCommandTypeSignpost,
	EAGXDebugCommandTypePushGroup,
	EAGXDebugCommandTypePopGroup,
	EAGXDebugCommandTypeInvalid
};

/**
 * EAGXDebugLevel: Level of AGX RHI debug features to be enabled.
 */
enum EAGXDebugLevel
{
	EAGXDebugLevelOff,
	EAGXDebugLevelFastValidation,
	EAGXDebugLevelResetOnBind,
	EAGXDebugLevelConditionalSubmit,
	EAGXDebugLevelValidation,
	EAGXDebugLevelLogOperations,
	EAGXDebugLevelWaitForComplete,
};

NS_ASSUME_NONNULL_BEGIN
/**
 * FAGXDebugCommand: The data recorded for each command in the debug command-buffer wrapper.
 */
struct FAGXDebugCommand
{
	NSString* Label;
	EAGXDebugCommandType Type;
	MTLRenderPassDescriptor* PassDesc;
};

#if MTLPP_CONFIG_VALIDATE && METAL_DEBUG_OPTIONS

/**
 * FAGXDebugCommandBuffer: Wrapper around id<MTLCommandBuffer> that records information about commands.
 * This allows reporting of substantially more information in debug modes which can be especially helpful
 * when debugging GPU command-buffer failures.
 */
@interface FAGXDebugCommandBuffer : FApplePlatformObject
{
@public
	NSMutableArray<NSString*>* DebugGroup;
	NSString* ActiveEncoder;
	id<MTLCommandBuffer> InnerBuffer;
	TArray<FAGXDebugCommand*> DebugCommands;
	EAGXDebugLevel DebugLevel;
	id<MTLBuffer> DebugInfoBuffer;
};

/** Initialise the wrapper with the provided command-buffer. */
-(id)initWithCommandBuffer:(id<MTLCommandBuffer>)Buffer;

@end

class FAGXCommandBufferDebugging : public ns::Object<FAGXDebugCommandBuffer*>
{
public:
	FAGXCommandBufferDebugging();
	FAGXCommandBufferDebugging(mtlpp::CommandBuffer& Buffer);
	FAGXCommandBufferDebugging(FAGXDebugCommandBuffer* handle);
	
	static FAGXCommandBufferDebugging Get(mtlpp::CommandBuffer& Buffer);
	ns::AutoReleased<ns::String> GetDescription();
	ns::AutoReleased<ns::String> GetDebugDescription();
	
	void BeginRenderCommandEncoder(ns::String const& Label, mtlpp::RenderPassDescriptor const& Desc);
	void BeginComputeCommandEncoder(ns::String const& Label);
	void BeginBlitCommandEncoder(ns::String const& Label);
	void EndCommandEncoder();
	
	void SetPipeline(ns::String const& Desc);
	void Draw(ns::String const& Desc);
	void Dispatch(ns::String const& Desc);
	void Blit(ns::String const& Desc);
	
	void InsertDebugSignpost(ns::String const& Label);
	void PushDebugGroup(ns::String const& Group);
	void PopDebugGroup();
};

#endif

NS_ASSUME_NONNULL_END
