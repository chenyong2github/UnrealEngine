// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ITimedDataInput.h"

class FLiveLinkClient;
enum class ELiveLinkSourceMode : uint8;

class FLiveLinkTimedDataInput : public ITimedDataInput
{
public:
	FLiveLinkTimedDataInput(FLiveLinkClient* Client, FGuid Source);
	FLiveLinkTimedDataInput(const FLiveLinkTimedDataInput&) = delete;
	FLiveLinkTimedDataInput& operator=(const FLiveLinkTimedDataInput&) = delete;
	virtual ~FLiveLinkTimedDataInput();

	//~ Begin ITimedDataInput API
	virtual FText GetDisplayName() const override;
	virtual TArray<ITimedDataInputChannel*> GetChannels() const override;
	virtual ETimedDataInputEvaluationType GetEvaluationType() const override;
	virtual void SetEvaluationType(ETimedDataInputEvaluationType Evaluation) override;
	virtual double GetEvaluationOffsetInSeconds() const override;
	virtual void SetEvaluationOffsetInSeconds(double Offset) override;
	virtual FFrameRate GetFrameRate() const override;
	virtual int32 GetDataBufferSize() const override;
	virtual void SetDataBufferSize(int32 BufferSize) override;
	virtual bool IsDataBufferSizeControlledByInput() const override { return true; }
	virtual void AddChannel(ITimedDataInputChannel* Channel) override { Channels.Add(Channel); }
	virtual void RemoveChannel(ITimedDataInputChannel* Channel) override { Channels.RemoveSingleSwap(Channel); }
#if WITH_EDITOR
	virtual const FSlateBrush* GetDisplayIcon() const override;
#endif
	//~ End ITimedDataInput API


private:
	FLiveLinkClient* LiveLinkClient;
	TArray<ITimedDataInputChannel*> Channels;
	FGuid Source;
};
