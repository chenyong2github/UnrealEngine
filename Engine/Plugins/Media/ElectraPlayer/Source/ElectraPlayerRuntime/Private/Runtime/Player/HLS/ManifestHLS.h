// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Player/Manifest.h"

namespace Electra
{
struct FManifestHLSInternal;
class IPlaylistReaderHLS;


class FManifestHLS : public IManifest
{
public:
	static TSharedPtrTS<FManifestHLS> Create(IPlayerSessionServices* SessionServices, const FParamDict& Options, IPlaylistReaderHLS* PlaylistReader, TSharedPtrTS<FManifestHLSInternal> Manifest);

	virtual ~FManifestHLS();
	virtual EType GetPresentationType() const override;
	virtual TSharedPtrTS<IPlaybackAssetTimeline> GetTimeline() const override;
	virtual int64 GetDefaultStartingBitrate() const override;
	virtual FTimeValue GetMinBufferTime() const override;
	virtual void GetStreamMetadata(TArray<FStreamMetadata>& OutMetadata, EStreamType StreamType) const override;
	virtual IStreamReader *CreateStreamReaderHandler() override;

	virtual FResult FindPlayPeriod(TSharedPtrTS<IPlayPeriod>& OutPlayPeriod, const FPlayStartPosition& StartPosition, ESearchType SearchType) override;

private:
	FManifestHLS(IPlayerSessionServices* SessionServices, const FParamDict& InOptions, IPlaylistReaderHLS* PlaylistReader, TSharedPtrTS<FManifestHLSInternal> Manifest);

	FParamDict									Options;
	TSharedPtrTS<FManifestHLSInternal>			InternalManifest;
	IPlayerSessionServices* 					SessionServices;
	IPlaylistReaderHLS*							PlaylistReader;

};



} // namespace Electra


