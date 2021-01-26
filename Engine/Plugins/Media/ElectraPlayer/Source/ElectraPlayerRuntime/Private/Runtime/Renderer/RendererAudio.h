// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "Containers/Queue.h"

#include "Renderer/RendererBase.h"
#include "Core/MediaThreads.h"
#include "PlayerTime.h"

#include "MediaAudioDecoderOutput.h"

struct FTimespan;
class FElectraPlayer;

class FElectraPlayerPlatformAudioDecoderOutputFactory
{
public:
	static IAudioDecoderOutput* Create();
};


class FElectraRendererAudio : public Electra::IMediaRenderer, public TSharedFromThis<FElectraRendererAudio, ESPMode::ThreadSafe>
{
public:
	struct SystemConfiguration
	{
		SystemConfiguration();
	};
	static bool Startup(const SystemConfiguration& Configuration);
	static void Shutdown(void);

	FElectraRendererAudio(TSharedPtr<FElectraPlayer, ESPMode::ThreadSafe> InPlayer);
	virtual ~FElectraRendererAudio();

	struct InstanceConfiguration
	{
		InstanceConfiguration()
		{
			// todo
		}
	};

	enum OpenError
	{
		eOpen_Ok,
		eOpen_Error,
	};

	OpenError Open(const InstanceConfiguration& Config);
	void Close(void);

	//-------------------------------------------------------------------------
	// Methods for UE interface
	//
	void DetachPlayer();


	//-------------------------------------------------------------------------
	// Methods for Electra::IMediaRenderer
	//

	// Returns the properties of the buffer pool. Those properties should not change
	virtual const Electra::FParamDict& GetBufferPoolProperties() const override;

	// Create a buffer pool from where a decoder can get the block of memory to decode into.
	virtual UEMediaError CreateBufferPool(const Electra::FParamDict& Parameters) override;

	// Asks for a sample buffer from the buffer pool created previously through CreateBufferPool()
	virtual UEMediaError AcquireBuffer(IBuffer*& OutBuffer, int32 TimeoutInMicroseconds, const Electra::FParamDict& InParameters) override;

	// Releases the buffer for rendering and subsequent return to the buffer pool
	virtual UEMediaError ReturnBuffer(IBuffer* Buffer, bool bRender, const Electra::FParamDict& InSampleProperties) override;

	// Informs that the decoder is done with this pool. NO FREE!!!
	virtual UEMediaError ReleaseBufferPool() override;

	virtual bool CanReceiveOutputFrames(uint64 NumFrames) const override;

	// Receives the render clock we need to update with the most recently rendered sample's timestamp.
	virtual void SetRenderClock(TSharedPtr<Electra::IMediaRenderClock, ESPMode::ThreadSafe> RenderClock) override;

	// Sets the next expected sample's approximate presentation time stamp
	virtual void SetNextApproximatePresentationTime(const Electra::FTimeValue& NextApproxPTS) override;

	// Flushes all pending buffers not yet rendered
	virtual UEMediaError Flush(const Electra::FParamDict& InOptions) override;

	// Begins rendering of the first sample buffer
	virtual void StartRendering(const Electra::FParamDict& InOptions) override;

	// Stops rendering of sample buffers
	virtual void StopRendering(const Electra::FParamDict& InOptions) override;


	//! Called when audio sample is returned to pool
	virtual void SampleReleasedToPool(FTimespan DurationToRelease) override;

	//! Set used clock to some external value
	void SetRenderClock(const FTimespan& RenderTime);

private:

	TWeakPtr<FElectraPlayer, ESPMode::ThreadSafe> Player;
	TDecoderOutputObjectPool<IAudioDecoderOutput, FElectraPlayerPlatformAudioDecoderOutputFactory> DecoderOutputPool;

	uint32					 MaxBufferSize;
	int32				     NumOutputAudioBuffersInUse;

	class FMediaBufferSharedPtrWrapper : public Electra::IMediaRenderer::IBuffer
	{
	public:
		explicit FMediaBufferSharedPtrWrapper(IAudioDecoderOutputPtr InDecoderOutput)
			: DecoderOutput(InDecoderOutput)
		{
		}

		~FMediaBufferSharedPtrWrapper()
		{
		}

		virtual const Electra::FParamDict& GetBufferProperties() const override
		{
			return BufferProperties;
		}
		Electra::FParamDict BufferProperties;
		IAudioDecoderOutputPtr DecoderOutput;
	};

	// ------------------------------------------------------------------------------------------------------

	Electra::FParamDict							BufferPoolProperties;
	int32										NumBuffers;
	int32										NumBuffersAcquiredForDecoder;

	TSharedPtr<Electra::IMediaRenderClock, ESPMode::ThreadSafe>	RenderClock;
};


