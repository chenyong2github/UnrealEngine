/*
 * Copyright 2016-2017 Nikolay Aleksiev. All rights reserved.
 * License: https://github.com/naleksiev/mtlpp/blob/master/LICENSE
 */
// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.
// Modifications for Unreal Engine

#pragma once

#include "defines.hpp"
#include "ns.hpp"

MTLPP_PROTOCOL(MTLCommandBuffer);

namespace mtlpp
{
    class Device;
    class CommandQueue;
    class BlitCommandEncoder;
    class RenderCommandEncoder;
    class ParallelRenderCommandEncoder;
    class ComputeCommandEncoder;
    class CommandQueue;
    class Drawable;
    class RenderPassDescriptor;

    enum class CommandBufferStatus
    {
        NotEnqueued = 0,
        Enqueued    = 1,
        Committed   = 2,
        Scheduled   = 3,
        Completed   = 4,
        Error       = 5,
    }
    MTLPP_AVAILABLE(10_11, 8_0);

    enum class CommandBufferError
    {
        None                                      = 0,
        Internal                                  = 1,
        Timeout                                   = 2,
        PageFault                                 = 3,
        Blacklisted                               = 4,
        NotPermitted                              = 7,
        OutOfMemory                               = 8,
        InvalidResource                           = 9,
        Memoryless      MTLPP_AVAILABLE_IOS(10_0) = 10,
		DeviceRemoved  MTLPP_AVAILABLE_MAC(10_13) = 11,
    }
    MTLPP_AVAILABLE(10_11, 8_0);

    class CommandBuffer : public ns::Object<ns::Protocol<id<MTLCommandBuffer>>::type>
    {
    public:
        CommandBuffer() { }
        CommandBuffer(ns::Protocol<id<MTLCommandBuffer>>::type handle) : ns::Object<ns::Protocol<id<MTLCommandBuffer>>::type>(handle) { }

        Device              GetDevice() const;
        CommandQueue        GetCommandQueue() const;
        bool                GetRetainedReferences() const;
        ns::String          GetLabel() const;
        CommandBufferStatus GetStatus() const;
        ns::Error           GetError() const;
        double              GetKernelStartTime() const MTLPP_AVAILABLE_IOS(10_3);
        double              GetKernelEndTime() const MTLPP_AVAILABLE_IOS(10_3);
        double              GetGpuStartTime() const MTLPP_AVAILABLE_IOS(10_3);
        double              GetGpuEndTime() const MTLPP_AVAILABLE_IOS(10_3);

        void SetLabel(const ns::String& label);

        void Enqueue();
        void Commit();
        void AddScheduledHandler(std::function<void(const CommandBuffer&)> handler);
        void AddCompletedHandler(std::function<void(const CommandBuffer&)> handler);
        void Present(const Drawable& drawable);
        void PresentAtTime(const Drawable& drawable, double presentationTime);
        void PresentAfterMinimumDuration(const Drawable& drawable, double duration) MTLPP_AVAILABLE_IOS(10_3);
        void WaitUntilScheduled();
        void WaitUntilCompleted();
        BlitCommandEncoder BlitCommandEncoder();
        RenderCommandEncoder RenderCommandEncoder(const RenderPassDescriptor& renderPassDescriptor);
        ComputeCommandEncoder ComputeCommandEncoder();
        ParallelRenderCommandEncoder ParallelRenderCommandEncoder(const RenderPassDescriptor& renderPassDescriptor);
		
		void PushDebugGroup(const ns::String& string) MTLPP_AVAILABLE(10_13, 11_0);
		void PopDebugGroup() MTLPP_AVAILABLE(10_13, 11_0);
    }
    MTLPP_AVAILABLE(10_11, 8_0);
}
