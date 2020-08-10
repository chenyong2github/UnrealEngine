// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DirectLink/DeltaConsumer.h"
#include "DirectLink/DirectLinkCommon.h"
#include "DirectLink/Network/DirectLinkISceneProvider.h"


class FDatasmithDeltaConsumer;

class FTestSceneProvider : public DirectLink::ISceneProvider
{
public:
	virtual ESceneStatus GetSceneStatus(const DirectLink::FSceneIdentifier& Scene) override;
	virtual TSharedPtr<DirectLink::IDeltaConsumer> GetDeltaConsumer(const DirectLink::FSceneIdentifier& Scene) override;
	virtual bool CanOpenNewConnection() override;

	TMap<FGuid, TSharedPtr<FDatasmithDeltaConsumer>> Consumers;
};
