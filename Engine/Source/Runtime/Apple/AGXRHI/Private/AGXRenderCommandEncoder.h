// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AGXDebugCommandEncoder.h"

#if MTLPP_CONFIG_VALIDATE && METAL_DEBUG_OPTIONS
NS_ASSUME_NONNULL_BEGIN

@class FAGXShaderPipeline;
@class FAGXDebugRenderCommandEncoder;
@class FAGXDebugParallelRenderCommandEncoder;

class FAGXRenderCommandEncoderDebugging : public FAGXCommandEncoderDebugging
{
	void InsertDebugDraw();
public:
	FAGXRenderCommandEncoderDebugging();
	FAGXRenderCommandEncoderDebugging(mtlpp::RenderCommandEncoder& Encoder, mtlpp::RenderPassDescriptor const& Desc, FAGXCommandBufferDebugging& Buffer);
	FAGXRenderCommandEncoderDebugging(FAGXDebugCommandEncoder* handle);
	
	static FAGXRenderCommandEncoderDebugging Get(mtlpp::RenderCommandEncoder& Buffer);
	
	void SetPipeline(FAGXShaderPipeline* Pipeline);
	
	void SetBytes(EAGXShaderFrequency Freq, const void * bytes, NSUInteger length, NSUInteger index);
	void SetBuffer(EAGXShaderFrequency Freq,  FAGXBuffer const& buffer, NSUInteger offset, NSUInteger index);
	void SetBufferOffset(EAGXShaderFrequency Freq, NSUInteger offset, NSUInteger index);
	void SetTexture(EAGXShaderFrequency Freq,  FAGXTexture const& texture, NSUInteger index);
	void SetSamplerState(EAGXShaderFrequency Freq,  mtlpp::SamplerState const& sampler, NSUInteger index);
	void SetSamplerState(EAGXShaderFrequency Freq,  mtlpp::SamplerState const& sampler, float lodMinClamp, float lodMaxClamp, NSUInteger index);
	void SetDepthStencilState( mtlpp::DepthStencilState const& depthStencilState);
	
	void Draw(mtlpp::PrimitiveType primitiveType, NSUInteger vertexStart, NSUInteger vertexCount, NSUInteger instanceCount);
	
	void Draw(mtlpp::PrimitiveType primitiveType, NSUInteger vertexStart, NSUInteger vertexCount);
	
	void DrawIndexed(mtlpp::PrimitiveType primitiveType, NSUInteger indexCount, mtlpp::IndexType indexType, FAGXBuffer const& indexBuffer, NSUInteger indexBufferOffset, NSUInteger instanceCount);
	
	void DrawIndexed(mtlpp::PrimitiveType primitiveType, NSUInteger indexCount, mtlpp::IndexType indexType, FAGXBuffer const& indexBuffer, NSUInteger indexBufferOffset);
	
	void Draw(mtlpp::PrimitiveType primitiveType, NSUInteger vertexStart, NSUInteger vertexCount, NSUInteger instanceCount, NSUInteger baseInstance);
	
	void DrawIndexed(mtlpp::PrimitiveType primitiveType, NSUInteger indexCount, mtlpp::IndexType indexType, FAGXBuffer const& indexBuffer, NSUInteger indexBufferOffset, NSUInteger instanceCount, NSInteger baseVertex, NSUInteger baseInstance);
	
	void Draw(mtlpp::PrimitiveType primitiveType, FAGXBuffer const& indirectBuffer, NSUInteger indirectBufferOffset);
	
	void DrawIndexed(mtlpp::PrimitiveType primitiveType, mtlpp::IndexType indexType, FAGXBuffer const& indexBuffer, NSUInteger indexBufferOffset, FAGXBuffer const& indirectBuffer, NSUInteger indirectBufferOffset);
	
	/** Validates the pipeline/binding state */
	bool ValidateFunctionBindings(EAGXShaderFrequency Frequency);
	void Validate();
	
	void InsertDebugSignpost(ns::String const& Label);
	void PushDebugGroup(ns::String const& Group);
	void PopDebugGroup();
	
	void EndEncoder();
};

class FAGXParallelRenderCommandEncoderDebugging : public ns::Object<FAGXDebugParallelRenderCommandEncoder*>
{
public:
	FAGXParallelRenderCommandEncoderDebugging();
	FAGXParallelRenderCommandEncoderDebugging(mtlpp::ParallelRenderCommandEncoder& Encoder, mtlpp::RenderPassDescriptor const& Desc, FAGXCommandBufferDebugging& Buffer);
	FAGXParallelRenderCommandEncoderDebugging(FAGXDebugParallelRenderCommandEncoder* handle);

	static FAGXDebugParallelRenderCommandEncoder* Get(mtlpp::ParallelRenderCommandEncoder& Buffer);

	FAGXRenderCommandEncoderDebugging GetRenderCommandEncoderDebugger(mtlpp::RenderCommandEncoder& Encoder);

	void InsertDebugSignpost(ns::String const& Label);
	void PushDebugGroup(ns::String const& Group);
	void PopDebugGroup();
	
	void EndEncoder();
};

NS_ASSUME_NONNULL_END
#endif
