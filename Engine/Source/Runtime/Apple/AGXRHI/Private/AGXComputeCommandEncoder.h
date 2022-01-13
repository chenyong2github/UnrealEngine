// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AGXDebugCommandEncoder.h"

#if MTLPP_CONFIG_VALIDATE && METAL_DEBUG_OPTIONS
NS_ASSUME_NONNULL_BEGIN

@class FAGXDebugComputeCommandEncoder;
@class FAGXShaderPipeline;


class FAGXComputeCommandEncoderDebugging : public FAGXCommandEncoderDebugging
{
	void InsertDebugDispatch();
	void Validate();
public:
	FAGXComputeCommandEncoderDebugging();
	FAGXComputeCommandEncoderDebugging(mtlpp::ComputeCommandEncoder& Encoder, FAGXCommandBufferDebugging& Buffer);
	FAGXComputeCommandEncoderDebugging(FAGXDebugCommandEncoder* handle);
	
	static FAGXComputeCommandEncoderDebugging Get(mtlpp::ComputeCommandEncoder& Buffer);
	
	void InsertDebugSignpost(ns::String const& Label);
	void PushDebugGroup(ns::String const& Group);
	void PopDebugGroup();
	void EndEncoder();
	void DispatchThreadgroups(mtlpp::Size const& threadgroupsPerGrid, mtlpp::Size const& threadsPerThreadgroup);
	void SetPipeline(FAGXShaderPipeline* Pipeline);
	void SetBytes(const void * bytes, NSUInteger length, NSUInteger index);
	void SetBuffer( FAGXBuffer const& buffer, NSUInteger offset, NSUInteger index);
	void SetBufferOffset(NSUInteger offset, NSUInteger index);
	void SetTexture( FAGXTexture const& texture, NSUInteger index);
	void SetSamplerState( mtlpp::SamplerState const& sampler, NSUInteger index);
	void SetSamplerState( mtlpp::SamplerState const& sampler, float lodMinClamp, float lodMaxClamp, NSUInteger index);
	
	void DispatchThreadgroupsWithIndirectBuffer(FAGXBuffer const& indirectBuffer, NSUInteger indirectBufferOffset, mtlpp::Size const& threadsPerThreadgroup);
};

NS_ASSUME_NONNULL_END
#endif
