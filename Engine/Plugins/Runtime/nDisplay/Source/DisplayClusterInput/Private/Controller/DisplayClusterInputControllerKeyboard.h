// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DisplayClusterInputControllerBase.h"
#include "DisplayClusterInputTypes.h"
#include "IDisplayClusterInputModule.h"

class FGenericApplicationMessageHandler;


class FKeyboardController : public FControllerDeviceBase<EDisplayClusterInputDeviceType::VrpnKeyboard>
{
public:
	virtual ~FKeyboardController()
	{ }

public:
	// Register nDisplay second keyboard keys in the engine FKey namespace
	virtual void Initialize() override;

	// Delegated events for DisplayClusterInput vrpn connect
	virtual void ProcessStartSession() override;
	virtual void ProcessEndSession() override;
	virtual void ProcessPreTick() override;

	// Reflect vrpn keyboard to the engine
	void ReflectKeyboard(const FString& VrpnDeviceId, EDisplayClusterInputKeyboardReflectionMode ReflectionMode);


private:
	// Runtime. Add bind for key by key name (reflect option purpose)
	void ConnectKey(FChannelBinds& KeyboardData, uint32 VrpnChannel, const TCHAR* KeyName);

	//  Run-time flags for init
	bool bReflectToUE              : 1;  // Bind vrpn keyboard to UE at OnDisplayClusterStartSession pass
	bool bReflectToNDisplayCluster : 1;  // Bind vrpn keyboard to nDisplay keyboard at OnDisplayClusterStartSession pass
};
