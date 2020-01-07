// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WebRtcIncludes.h"
#include "Codecs/WmfIncludes.h"
#include "Utils.h"
#include "WebRtcLogging.h"

// Engine

#include "Logging/LogMacros.h"
#include "Misc/AssertionMacros.h"
#include "Misc/Optional.h"
#include "Misc/CoreDelegates.h"
#include "Misc/ScopeLock.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Timespan.h"

#include "Async/Async.h"

#include "Modules/ModuleManager.h"

#include "IWebSocket.h"
#include "WebSocketsModule.h"

#include "RHI.h"
#include "RHIResources.h"
#include "DynamicRHI.h"
#include "RHIStaticStates.h"
#include "ShaderCore.h"
#include "RendererInterface.h"
#include "PipelineStateCache.h"
#include "ScreenRendering.h"
#include "CommonRenderResources.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "RendererInterface.h"

#include "Dom/JsonObject.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include "HAL/Thread.h"
#include "HAL/Platform.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/ThreadSafeBool.h"
#include "HAL/ThreadSafeCounter.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/CriticalSection.h"

#include "Containers/StringConv.h"
#include "Containers/UnrealString.h"
#include "Containers/Queue.h"
#include "Containers/Array.h"

#include "Templates/UniquePtr.h"
#include "Templates/Function.h"
#include "Templates/UnrealTemplate.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"
#include "Templates/Atomic.h"

#include "AudioMixerDevice.h"

#include "Sockets.h"
#include "Engine/Engine.h"
#include "Framework/Application/SlateUser.h"
#include "Windows/WindowsPlatformMisc.h"
#include "Internationalization/Internationalization.h"
#include "Delegates/IDelegateInstance.h"
#include "Stats/Stats.h"
#include "Math/UnrealMathUtility.h"

#include "IMediaTracks.h"
#include "IMediaCache.h"
#include "IMediaPlayer.h"
#include "IMediaView.h"
#include "IMediaControls.h"
#include "IMediaEventSink.h"
#include "IMediaOptions.h"
#include "MediaSamples.h"
#include "IMediaAudioSample.h"
#include "IMediaTextureSample.h"
