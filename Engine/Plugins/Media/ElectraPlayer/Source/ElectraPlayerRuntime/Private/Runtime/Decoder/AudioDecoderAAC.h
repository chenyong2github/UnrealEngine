// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlayerCore.h"

#include "StreamAccessUnitBuffer.h"
#include "Decoder/DecoderBase.h"



namespace Electra
{
	class IPlayerSessionServices;


	/**
	 * LC-AAC audio decoder.
	**/
	class IAudioDecoderAAC : public IDecoderBase, public IAccessUnitBufferInterface, public IDecoderAUBufferDiags, public IDecoderReadyBufferDiags
	{
	public:
		struct FSystemConfiguration
		{
			FSystemConfiguration();

			struct FThreadConfig
			{
				FMediaRunnable::Param		Decoder;				//!< Decoder thread settings.
				FMediaRunnable::Param		PassOn;					//!< Settings for thread passing decoded samples to the renderer.
			};
			FThreadConfig	ThreadConfig;
		};

		struct FInstanceConfiguration
		{
			FInstanceConfiguration();

			FSystemConfiguration::FThreadConfig	ThreadConfig;					//!< Thread configuration (defaults to values set in SystemConfiguration)
		};

		static bool Startup(const FSystemConfiguration& Config);
		static void Shutdown();

		static IAudioDecoderAAC* Create();

		virtual ~IAudioDecoderAAC() = default;

		virtual void SetPlayerSessionServices(IPlayerSessionServices* SessionServices) = 0;

		virtual void Open(const FInstanceConfiguration& Config) = 0;
		virtual void Close() = 0;


		//-------------------------------------------------------------------------
		// Methods from IDecoderBase
		//
		virtual void SetRenderer(TSharedPtr<IMediaRenderer, ESPMode::ThreadSafe> InRenderer) = 0;

		//-------------------------------------------------------------------------
		// Methods from IAccessUnitBufferInterface
		//
		//! Attempts to push an access unit to the decoder. Ownership of the access unit is transferred if the push is successful.
		virtual EAUpushResult AUdataPushAU(FAccessUnit* AccessUnit) = 0;
		//! Notifies the decoder that there will be no further access units.
		virtual void AUdataPushEOD() = 0;
		//! Instructs the decoder to flush all pending input and all already decoded output.
		virtual void AUdataFlushEverything() = 0;

		//-------------------------------------------------------------------------
		// Methods from IDecoderAUBufferDiags
		//
		//! Registers an AU input buffer listener.
		virtual void SetAUInputBufferListener(IAccessUnitBufferListener* Listener) = 0;

		//-------------------------------------------------------------------------
		// Methods from IDecoderReadyBufferDiags
		//
		virtual void SetReadyBufferListener(IDecoderOutputBufferListener* Listener) = 0;
	};


} // namespace Electra

