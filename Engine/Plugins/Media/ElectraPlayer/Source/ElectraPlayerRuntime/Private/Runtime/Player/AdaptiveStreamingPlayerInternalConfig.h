// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Decoder/VideoDecoderH264.h"
#include "Decoder/AudioDecoderAAC.h"
#include "StreamAccessUnitBuffer.h"
#include "Player/AdaptiveStreamingPlayerABR.h"


namespace Electra
{


	namespace AdaptiveStreamingPlayerConfig
	{


		struct FConfiguration
		{
			FConfiguration()
			{
				// TODO: set certain configuration items for this type of player that make sense (like image size auto config)

				WorkerThread.Priority = TPri_Normal;
				WorkerThread.StackSize = 32768;
				WorkerThread.CoreAffinity = -1;

				// Set default values to maximum permitted values.
				StreamBufferConfigVideo.MaxDataSize = 16 << 20;
				StreamBufferConfigVideo.MaxDuration.SetFromSeconds(20.0);

				StreamBufferConfigAudio.MaxDataSize = 2 << 20;
				StreamBufferConfigAudio.MaxDuration.SetFromSeconds(20.0);

				bHoldLastFrameDuringSeek = true;

				MaxPrerollDurationUntilForcedStart = 3.0;

				InitialBufferMinTimeAvailBeforePlayback = 5.0;
				SeekBufferMinTimeAvailBeforePlayback = 5.0;
				RebufferMinTimeAvailBeforePlayback = 5.0;

				// And apply the following scaling factors to determine the individual state buffering amounts
				ScaleMPDInitialBufferMinTimeBeforePlayback = 1.0;
				ScaleMPDSeekBufferMinTimeAvailBeforePlayback = 1.0;
				ScaleMPDRebufferMinTimeAvailBeforePlayback = 1.0;
			}

			FMediaRunnable::Param								WorkerThread;

			FAccessUnitBuffer::FConfiguration					StreamBufferConfigVideo;
			FAccessUnitBuffer::FConfiguration					StreamBufferConfigAudio;
			FAccessUnitBuffer::FConfiguration					StreamBufferConfigText;

			IAdaptiveStreamSelector::FConfiguration				StreamSelectorConfig;

			IVideoDecoderH264::FInstanceConfiguration			DecoderCfg264;
			IAudioDecoderAAC::FInstanceConfiguration			DecoderCfgAAC;

			bool												bHoldLastFrameDuringSeek;

			double												MaxPrerollDurationUntilForcedStart;

			double												InitialBufferMinTimeAvailBeforePlayback;
			double												SeekBufferMinTimeAvailBeforePlayback;
			double												RebufferMinTimeAvailBeforePlayback;

			double												ScaleMPDInitialBufferMinTimeBeforePlayback;
			double												ScaleMPDSeekBufferMinTimeAvailBeforePlayback;
			double												ScaleMPDRebufferMinTimeAvailBeforePlayback;
		};

	} // namespace AdaptiveStreamingPlayerConfig

} // namespace Electra


