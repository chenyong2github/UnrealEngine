// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/OnlineServicesCommon.h"

namespace UE::Online {

class ONLINESERVICESNULL_API FOnlineServicesNull : public FOnlineServicesCommon
{
public:
	using Super = FOnlineServicesCommon;

	FOnlineServicesNull();
	virtual void RegisterComponents() override;
	virtual void Initialize() override;

	static EOnlineServices GetServicesProvider() { return EOnlineServices::Null; }
};

/* UE::Online */ }
