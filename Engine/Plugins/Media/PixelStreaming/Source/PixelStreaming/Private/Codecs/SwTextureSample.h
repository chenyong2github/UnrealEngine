// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "PixelStreamingPrivate.h"
#include "Utils.h"

#include "IMediaTextureSample.h"
#include "MediaObjectPool.h"
#include "Misc/Timespan.h"
#include "Templates/SharedPointer.h"
#include "Templates/RefCounting.h"
#include "RHI.h"
#include "RHIResources.h"
#include "ShaderCore.h"
#include "D3D11State.h"
#include "D3D11Resources.h"

// IMediaTextureSample impl
// is used as video decoder output
// contains a reference to GPU texture of decoded frame ready for rendering
// along with corresponding metadata
// owns binary data so can be cached
class FSwTextureSample :
	public IMediaTextureSample,
	public IMediaPoolable
{
public:
	bool Init(FIntPoint InOutputDim)
	{
		OutputDim = InOutputDim;
		Dim.X = Align(OutputDim.X, 16);
		Dim.Y = Align(OutputDim.Y, 16) * 3 / 2;

		if (!MFSample.IsValid())
		{
			TRefCountPtr<IMFMediaBuffer> MediaBuffer;

			if (IsWindows8Plus())
			{
				check(!Texture.IsValid());
				FRHIResourceCreateInfo CreateInfo;
				Texture = RHICreateTexture2D(Dim.X, Dim.Y, PF_G8, 1, 1, TexCreate_ShaderResource, CreateInfo);
				ID3D11Texture2D* DX11Texture = static_cast<ID3D11Texture2D*>(GetD3D11TextureFromRHITexture(Texture)->GetResource());
				CHECK_HR(MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), DX11Texture, 0, false, MediaBuffer.GetInitReference()));

			}
			else // Win7
			{
				CHECK_HR(MFCreateMemoryBuffer(Dim.X * Dim.Y * sizeof(uint8), MediaBuffer.GetInitReference()));
			}

			CHECK_HR(MFCreateSample(MFSample.GetInitReference()));
			CHECK_HR(MFSample->AddBuffer(MediaBuffer.GetReference()));
		}

		return true;
	}

	bool ProcessOutputSample()
	{
		int64 SampleTime;
		CHECK_HR(MFSample->GetSampleTime(&SampleTime));
		Time = SampleTime;
		int64 SampleDuration;
		CHECK_HR(MFSample->GetSampleDuration(&SampleDuration));
		Duration = SampleDuration;

		if (!IsWindows8Plus()) // Win7
		{
			// Retrieve frame data and store it in Buffer for rendering later
			TRefCountPtr<IMFMediaBuffer> MediaBuffer;
			CHECK_HR(MFSample->GetBufferByIndex(0, MediaBuffer.GetInitReference()));
			DWORD BufferSize = 0;
			CHECK_HR(MediaBuffer->GetCurrentLength(&BufferSize));
			check(Dim.X * Dim.Y == (int32)BufferSize);

			uint8* Data = nullptr;
			Buffer.Reset(BufferSize);
			CHECK_HR(MediaBuffer->Lock(&Data, NULL, NULL));
			Buffer.Append(Data, BufferSize);
			CHECK_HR(MediaBuffer->Unlock());

			// The output IMFSample needs to be released (and recreated for the next frame) for unknown reason.
			// If not destroyed decoder throws an unknown error later on.
			MFSample.SafeRelease();
		}

		return true;
	}

	const void* GetBuffer() override
	{
		return Buffer.GetData();
	}

	FIntPoint GetDim() const override
	{
		return Dim;
	}

	FTimespan GetDuration() const override
	{
		return Duration;
	}

	EMediaTextureSampleFormat GetFormat() const override
	{
		return EMediaTextureSampleFormat::CharNV12;
	}

	FIntPoint GetOutputDim() const override
	{
		return OutputDim;
	}

	uint32 GetStride() const override
	{
		return Dim.X * sizeof(uint8);
	}

#if WITH_ENGINE
	FRHITexture* GetTexture() const override
	{
		return Texture;
	}
#endif //WITH_ENGINE

	FTimespan GetTime() const override
	{
		return Time;
	}

	bool IsCacheable() const override
	{
		return true;
	}

	bool IsOutputSrgb() const override
	{
		return true;
	}

	IMFSample* GetMFSample()
	{
		return MFSample;
	}

private:
	/** Width and height of the texture sample. */
	FIntPoint Dim;

	/** Width and height of the output. */
	FIntPoint OutputDim;

	/** Presentation for which the sample was generated. */
	FTimespan Time = FTimespan::Zero();

	/** Duration for which the sample is valid. */
	FTimespan Duration = FTimespan::Zero();

	/** The texture containing output frame. Used for Win8+. */
	FTexture2DRHIRef Texture;

	/** The output frame data buffer. Used for Win7. */
	TArray<uint8> Buffer;

	/** The MF sample. */
	TRefCountPtr<IMFSample> MFSample;
};

using FSwTextureSamplePtr = TSharedPtr<FSwTextureSample, ESPMode::ThreadSafe>;
using FSwTextureSampleRef = TSharedRef<FSwTextureSample, ESPMode::ThreadSafe>;
using FSwTextureSamplePool = TMediaObjectPool<FSwTextureSample>;
