// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlayerCore.h"
#include "Renderer/RendererBase.h"


namespace Electra
{
	class IAccessUnitBufferListener;
	class IDecoderOutputBufferListener;


	/**
	 *
	**/
	class IDecoderBase
	{
	public:
		virtual ~IDecoderBase() = default;
		virtual void SetRenderer(TSharedPtr<IMediaRenderer, ESPMode::ThreadSafe> InRenderer) = 0;
	};


	/**
	 *
	**/
	class IDecoderAUBufferDiags
	{
	public:
		virtual ~IDecoderAUBufferDiags() = default;
		virtual void SetAUInputBufferListener(IAccessUnitBufferListener* Listener) = 0;
	};


	/**
	 *
	**/
	class IDecoderReadyBufferDiags
	{
	public:
		virtual ~IDecoderReadyBufferDiags() = default;
		virtual void SetReadyBufferListener(IDecoderOutputBufferListener* Listener) = 0;
	};


} // namespace Electra

