// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/OnlineServicesCommon.h"

namespace UE::Online {

class ONLINESERVICESEOS_API FOnlineServicesEOS : public FOnlineServicesCommon
{
public:
	FOnlineServicesEOS();
	virtual void RegisterComponents() override;

	static EOnlineServices GetServicesProvider() { return EOnlineServices::Epic; }
private:
};

/* UE::Online */ }
