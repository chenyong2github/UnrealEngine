// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DirectLink/DirectLinkCommon.h"
#include "DirectLink/Network/DirectLinkISceneProvider.h"


class FDatasmithSceneReceiver;

class FTestSceneProvider : public DirectLink::ISceneProvider
{
public:
	virtual bool CanOpenNewConnection(const FSourceInformation& Source) override;
	virtual TSharedPtr<DirectLink::ISceneReceiver> GetSceneReceiver(const FSourceInformation& Source) override;

	TMap<FGuid, TSharedPtr<FDatasmithSceneReceiver>> SceneReceivers;
};
