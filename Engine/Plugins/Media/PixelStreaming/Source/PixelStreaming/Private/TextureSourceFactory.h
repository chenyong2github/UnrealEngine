// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Map.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "IPixelStreamingTextureSourceFactory.h"
#include "TextureSourceBackbuffer.h"
#include "TextureSourceComputeI420.h"
#include "TextureSourceCPUI420.h"
#include "Templates/UniquePtr.h"
#include "Settings.h"

namespace UE::PixelStreaming
{
	/*
	* A factory where "TextureSourceCreators" are registered as lambdas with unique FNames.
	* The premise is that outside implementers can register texture source creators using the Pixel Streaming module.
	*/
	class FTextureSourceFactory : public IPixelStreamingTextureSourceFactory
	{
	public:
		FTextureSourceFactory() :
			TextureSourceCreators()
		{
			RegisterInternalSources();
		}

		virtual ~FTextureSourceFactory() override {}

		virtual TUniquePtr<FPixelStreamingTextureSource> CreateTextureSource(FName SourceType) override
		{
			verifyf(TextureSourceCreators.Contains(SourceType), TEXT("There was no texture source registered for the type: %s"), *SourceType.ToString())
			TFunction<TUniquePtr<FPixelStreamingTextureSource>()>& Creator = TextureSourceCreators[SourceType];
			return Creator();
		}

		void RegisterInternalSources()
		{
			// Backbuffer
			RegisterTextureSourceType(FName("Backbuffer"), []() {
				TUniquePtr<FPixelStreamingTextureSource> TextureSource;

				if (Settings::IsCodecVPX())
				{
					if (Settings::CVarPixelStreamingVPXUseCompute.GetValueOnAnyThread())
					{
						TextureSource = MakeUnique<FTextureSourceComputeI420>();
					}
					else
					{
						TextureSource = MakeUnique<FTextureSourceCPUI420>();
					}
				}
				else
				{
					TextureSource = MakeUnique<FTextureSourceBackbuffer>();
				}

				return MoveTemp(TextureSource);
			});

			// Some other internal texture source here...
		}

		virtual void RegisterTextureSourceType(FName SourceType, TFunction<TUniquePtr<FPixelStreamingTextureSource>()> CreatorFunc) override
		{
			TextureSourceCreators.Add(SourceType, CreatorFunc);
		}

		virtual void UnregisterTextureSourceType(FName SourceType) override
		{
			TextureSourceCreators.Remove(SourceType);
		}

	private:
		TMap<FName, TFunction<TUniquePtr<FPixelStreamingTextureSource>()>> TextureSourceCreators;
	};

} // namespace UE::PixelStreaming