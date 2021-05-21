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
	virtual void ClearDefaultStartTime() override;
	virtual FTimeValue GetMinBufferTime() const override;
	virtual void GetTrackMetadata(TArray<FTrackMetadata>& OutMetadata, EStreamType StreamType) const override;
	virtual void UpdateDynamicRefetchCounter() override;
	virtual IStreamReader* CreateStreamReaderHandler() override;

	virtual FResult FindPlayPeriod(TSharedPtrTS<IPlayPeriod>& OutPlayPeriod, const FPlayStartPosition& StartPosition, ESearchType SearchType) override;
	virtual FResult FindNextPlayPeriod(TSharedPtrTS<IPlayPeriod>& OutPlayPeriod, TSharedPtrTS<const IStreamSegment> CurrentSegment) override;

private:
	ELECTRA_IMPL_DEFAULT_ERROR_METHODS(DASHManifest);

	FManifestDASH(IPlayerSessionServices* SessionServices, TSharedPtrTS<FManifestDASHInternal> Manifest);

	IPlayerSessionServices* 					PlayerSessionServices = nullptr;
	TSharedPtrTS<FManifestDASHInternal>			CurrentManifest;
	int64										CurrentPeriodAndAdaptationXLinkResolveID = 1;
};



} // namespace Electra


