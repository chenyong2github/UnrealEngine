// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Player/Manifest.h"

namespace Electra
{
class FManifestDASHInternal;
class FPlaylistReaderDASH;
class FDASHTimeline;


class FManifestDASH : public IManifest
{
public:
	static TSharedPtrTS<FManifestDASH> Create(IPlayerSessionServices* SessionServices, const FParamDict& Options, TWeakPtrTS<FPlaylistReaderDASH> PlaylistReader, TSharedPtrTS<FManifestDASHInternal> Manifest);
	void UpdateTimeline();

	virtual ~FManifestDASH();
	virtual EType GetPresentationType() const override;
	virtual TSharedPtrTS<IPlaybackAssetTimeline> GetTimeline() const override;
	virtual int64 GetDefaultStartingBitrate() const override;
	virtual FTimeValue GetMinBufferTime() const override;
	virtual void GetStreamMetadata(TArray<FStreamMetadata>& OutMetadata, EStreamType StreamType) const override;
	virtual void UpdateDynamicRefetchCounter() override;
	virtual IStreamReader* CreateStreamReaderHandler() override;

	virtual FResult FindPlayPeriod(TSharedPtrTS<IPlayPeriod>& OutPlayPeriod, const FPlayStartPosition& StartPosition, ESearchType SearchType) override;

private:
	ELECTRA_IMPL_DEFAULT_ERROR_METHODS(DASHManifest);

	FManifestDASH(IPlayerSessionServices* SessionServices, const FParamDict& InOptions, TWeakPtrTS<FPlaylistReaderDASH> PlaylistReader, TSharedPtrTS<FManifestDASHInternal> Manifest);

	FParamDict									Options;
	TWeakPtrTS<FManifestDASHInternal>			InternalManifest;
	IPlayerSessionServices* 					PlayerSessionServices = nullptr;
	TWeakPtrTS<FPlaylistReaderDASH>				PlaylistReader;
	TSharedPtrTS<FDASHTimeline>					CurrentTimeline;
	mutable TArray<FStreamMetadata>				CurrentMetadataVideo;
	mutable TArray<FStreamMetadata>				CurrentMetadataAudio;
	mutable TArray<FStreamMetadata>				CurrentMetadataSubtitle;
	mutable bool								bHaveCurrentMetadata = false;
	int64										CurrentPeriodAndAdaptationXLinkResolveID = 1;
};



} // namespace Electra


