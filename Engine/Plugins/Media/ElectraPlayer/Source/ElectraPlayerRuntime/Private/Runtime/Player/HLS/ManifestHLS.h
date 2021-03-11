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
	static TSharedPtrTS<FManifestHLS> Create(IPlayerSessionServices* SessionServices, IPlaylistReaderHLS* PlaylistReader, TSharedPtrTS<FManifestHLSInternal> Manifest);

	virtual ~FManifestHLS();
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
	virtual IStreamReader *CreateStreamReaderHandler() override;

	virtual FResult FindPlayPeriod(TSharedPtrTS<IPlayPeriod>& OutPlayPeriod, const FPlayStartPosition& StartPosition, ESearchType SearchType) override;

private:
	FManifestHLS(IPlayerSessionServices* SessionServices, IPlaylistReaderHLS* PlaylistReader, TSharedPtrTS<FManifestHLSInternal> Manifest);

	TSharedPtrTS<FManifestHLSInternal>			InternalManifest;
	IPlayerSessionServices* 					SessionServices;
	IPlaylistReaderHLS*							PlaylistReader;

};



} // namespace Electra


