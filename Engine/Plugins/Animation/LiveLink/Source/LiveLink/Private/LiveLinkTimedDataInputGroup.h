// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ITimedDataInput.h"

class FLiveLinkClient;
enum class ELiveLinkSourceMode : uint8;

class FLiveLinkTimedDataInputGroup : public ITimedDataInputGroup
{
public:
	FLiveLinkTimedDataInputGroup(FLiveLinkClient* Client, FGuid Source);
	FLiveLinkTimedDataInputGroup(const FLiveLinkTimedDataInputGroup&) = delete;
	FLiveLinkTimedDataInputGroup& operator=(const FLiveLinkTimedDataInputGroup&) = delete;
	virtual ~FLiveLinkTimedDataInputGroup();

	//~ Begin ITimedDataInputGroup API
	virtual FText GetDisplayName() const override;
	virtual FText GetDescription() const override;

#if WITH_EDITOR
	virtual const FSlateBrush* GetDisplayIcon() const override;
#endif
	//~ End ITimedDataInputGroup API

public:
	void SetEvaluationType(ELiveLinkSourceMode SourceMode);
	void SetEvaluationOffset(ELiveLinkSourceMode SourceMode, double OffsetInSeconds);
	void SetBufferMaxSize(int32 BufferSize);

private:
	FLiveLinkClient* LiveLinkClient;
	FGuid Source;
};
