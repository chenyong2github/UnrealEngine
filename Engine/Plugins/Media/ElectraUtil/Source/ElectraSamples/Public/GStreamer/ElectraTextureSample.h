// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IMediaTextureSample.h"
#include "MediaObjectPool.h"
#include "RHI.h"
#include "RHIUtilities.h"

class MEDIArendererVideoUE;
class FRHITexture2D;

class FElectraTextureSampleUnix 
	: public IMediaTextureSample
	, public IMediaPoolable
{
public:
	FElectraTextureSampleUnix()
		: Time(FTimespan::Zero())
	{
	}

public:
	void Initialize(FIntPoint InDisplaySize, FIntPoint InTotalSize, const FMediaTimeStamp & InTime, FTimespan InDuration)
	{
		Time = InTime;
		DisplaySize = InDisplaySize;
		TotalSize = InTotalSize;
		Duration = InDuration;
	}

	void CreateTexture()
	{
		check(IsInRenderingThread());

		const ETextureCreateFlags CreateFlags = TexCreate_Dynamic | TexCreate_SRGB;

		TRefCountPtr<FRHITexture2D> DummyTexture2DRHI;
		FRHIResourceCreateInfo CreateInfo(TEXT("DummyTexture2D"));

		RHICreateTargetableShaderResource2D(
			TotalSize.X,
			TotalSize.Y,
			PF_B8G8R8A8,
			1,
			CreateFlags,
			TexCreate_RenderTargetable,
			false,
			CreateInfo,
			Texture,
			DummyTexture2DRHI
		);
	}	

public:
	//~ IMediaTextureSample interface
	virtual const void* GetBuffer() override
	{
		return nullptr;
	}

	virtual FIntPoint GetDim() const override
	{
		return TotalSize;
	}

	virtual FTimespan GetDuration() const override
	{
		return Duration;
	}

	virtual EMediaTextureSampleFormat GetFormat() const override
	{
		return EMediaTextureSampleFormat::CharBGRA;
	}

	virtual FIntPoint GetOutputDim() const override
	{
		return DisplaySize;
	}

	virtual uint32 GetStride() const override
	{
		if (!Texture.IsValid())
		{
			return 0;
		}

		return Texture->GetSizeX() * 4;
	}


	virtual FRHITexture* GetTexture() const override
	{
		return Texture.GetReference();
	}

	virtual FMediaTimeStamp GetTime() const override
	{
		return Time;
	}

	virtual bool IsCacheable() const override
	{
		// ??? 
		return true;
	}

	virtual bool IsOutputSrgb() const override
	{
		return true;
	}


public:
	//~ IMediaPoolable interface
#if !UE_SERVER
	virtual void InitializePoolable() override;
	virtual void ShutdownPoolable() override;
#endif

public:
	TRefCountPtr<FRHITexture2D> GetTextureRef() const
	{
		return Texture;
	}

private:
	TRefCountPtr<FRHITexture2D> Texture;
	FMediaTimeStamp Time;
	FTimespan Duration;
	FIntPoint TotalSize;
	FIntPoint DisplaySize;
};

using FElectraTextureSamplePtr = TSharedPtr<FElectraTextureSampleUnix, ESPMode::ThreadSafe>;

class FElectraTextureSamplePool : public TMediaObjectPool<FElectraTextureSampleUnix>
{
public:
	void PrepareForDecoderShutdown() {}
};
