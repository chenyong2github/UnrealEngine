// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/Engine.h"
#include "MagicLeapPrivilegeTypes.generated.h"

/** Privileges an app can request for from the system. */
UENUM(BlueprintType)
enum class EMagicLeapPrivilege : uint8
{
	Invalid,
	BatteryInfo,
	CameraCapture,
	ComputerVision,
	WorldReconstruction,
	InAppPurchase,
	AudioCaptureMic,
	DrmCertificates,
	Occlusion,
	LowLatencyLightwear,
	Internet,
	IdentityRead,
	BackgroundDownload,
	BackgroundUpload,
	MediaDrm,
	Media,
	MediaMetadata,
	PowerInfo,
	LocalAreaNetwork,
	VoiceInput,
	Documents,
	ConnectBackgroundMusicService,
	RegisterBackgroundMusicService,
	PcfRead,
	PwFoundObjRead = EMagicLeapPrivilege::PcfRead,
	NormalNotificationsUsage,
	MusicService,
	ControllerPose,
	GesturesSubscribe,
	GesturesConfig,
	AddressBookRead,
	AddressBookWrite,
	AddressBookBasicAccess,
	CoarseLocation,
	FineLocation,
	HandMesh,
	WifiStatusRead,
	SocialConnectionsInvitesAccess,
	SecureBrowserWindow,
};

/**
  Delegate for the result of requesting a privilege asynchronously.
  @param RequestedPrivilege The privilege that was requested.
  @param WasGranted True if the privilege was granted, false otherwise.
*/
DECLARE_DELEGATE_TwoParams(FMagicLeapPrivilegeRequestStaticDelegate, EMagicLeapPrivilege, bool);
DECLARE_DYNAMIC_DELEGATE_TwoParams(FMagicLeapPrivilegeRequestDelegate, EMagicLeapPrivilege, RequestedPrivilege, bool, WasGranted);
