// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "DirectLink/DirectLinkCommon.h"

namespace DirectLink
{

struct FCommunicationStatus
{
	bool IsTransmitting() const { return bIsSending || bIsReceiving; }
	bool IsProgressKnown() const { return TaskTotal > 0; };
	float GetProgress() const { return IsProgressKnown() ? float(FMath::Clamp(TaskCompleted, 0, TaskTotal)) / TaskTotal : 0.0f; };

public:
	bool bIsSending = false;
	bool bIsReceiving = false;
	int32 TaskTotal = 0;
	int32 TaskCompleted = 0;
};

class DATASMITHCORE_API IStreamCommunicationInterface
{
public:
	virtual ~IStreamCommunicationInterface() = default;
	virtual FCommunicationStatus GetCommunicationStatus() const = 0;
};


} // namespace DirectLink
