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
	static TSharedPtrTS<FManifestDASH> Create(IPlayerSessionServices* SessionServices, TSharedPtrTS<FManifestDASHInternal> Manifest);
	void UpdateInternalManifest(TSharedPtrTS<FManifestDASHInternal> UpdatedManifest);

	virtual ~FManifestDASH();
	virtual EType GetPresentationType() const override;
	virtual FTimeValue GetAnchorTime() const override;
	virtual FTimeRange GetTotalTimeRange() const override;
	virtual FTimeRange GetSeekableTimeRange() const override;
	virtual void GetSeekablePositions(TArray<FTimespan>& OutPositions) const override;
	virtual FTimeValue GetDuration() const override;
	virtual FTimeValue GetDefaultStartTime() const override;
	virtual int64 GetDefaultStartingBitrate() const override;
	virtual FTimeValue GetMinBufferTime() const override;
	virtual void GetStreamMetadata(TArray<FStreamMetadata>& OutMetadata, EStreamType StreamType) const override;
	virtual void UpdateDynamicRefetchCounter() override;
	virtual IStreamReader* CreateStreamReaderHandler() override;

	virtual FResult FindPlayPeriod(TSharedPtrTS<IPlayPeriod>& OutPlayPeriod, const FPlayStartPosition& StartPosition, ESearchType SearchType) override;

private:
	ELECTRA_IMPL_DEFAULT_ERROR_METHODS(DASHManifest);

	FManifestDASH(IPlayerSessionServices* SessionServices, TSharedPtrTS<FManifestDASHInternal> Manifest);

	IPlayerSessionServices* 					PlayerSessionServices = nullptr;
	TSharedPtrTS<FManifestDASHInternal>			CurrentManifest;
	mutable TArray<FStreamMetadata>				CurrentMetadataVideo;
	mutable TArray<FStreamMetadata>				CurrentMetadataAudio;
	mutable TArray<FStreamMetadata>				CurrentMetadataSubtitle;
	mutable bool								bHaveCurrentMetadata = false;
	int64										CurrentPeriodAndAdaptationXLinkResolveID = 1;
};



} // namespace Electra


