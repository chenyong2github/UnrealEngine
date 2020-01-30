// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapPrivilegeUtils.h"
#include "MagicLeapPrivilegeTypes.h"
#include "MagicLeapPrivilegesModule.h"

#if WITH_MLSDK
namespace MagicLeap
{
	MLPrivilegeID MAGICLEAPPRIVILEGES_API UnrealToMLPrivilege(EMagicLeapPrivilege Privilege)
	{
	#define PRIVCASE(x) case EMagicLeapPrivilege::x: { return MLPrivilegeID_##x; }
		switch(Privilege)
		{
			PRIVCASE(BatteryInfo)
			PRIVCASE(CameraCapture)
			PRIVCASE(WorldReconstruction)
			PRIVCASE(InAppPurchase)
			PRIVCASE(AudioCaptureMic)
			PRIVCASE(DrmCertificates)
			PRIVCASE(Occlusion)
			PRIVCASE(LowLatencyLightwear)
			PRIVCASE(Internet)
			PRIVCASE(IdentityRead)
			PRIVCASE(BackgroundDownload)
			PRIVCASE(BackgroundUpload)
			PRIVCASE(MediaDrm)
			PRIVCASE(Media)
			PRIVCASE(MediaMetadata)
			PRIVCASE(PowerInfo)
			PRIVCASE(LocalAreaNetwork)
			PRIVCASE(VoiceInput)
			PRIVCASE(Documents)
			PRIVCASE(ConnectBackgroundMusicService)
			PRIVCASE(RegisterBackgroundMusicService)
			PRIVCASE(PwFoundObjRead)
			PRIVCASE(NormalNotificationsUsage)
			PRIVCASE(MusicService)
			PRIVCASE(ControllerPose)
			PRIVCASE(ScreensProvider)
			PRIVCASE(GesturesSubscribe)
			PRIVCASE(GesturesConfig)
			PRIVCASE(AddressBookRead)
			PRIVCASE(AddressBookWrite)
			PRIVCASE(CoarseLocation)
			PRIVCASE(HandMesh)
			PRIVCASE(WifiStatusRead)
		default:
			UE_LOG(LogMagicLeapPrivileges, Error, TEXT("Unmapped privilege %d"), static_cast<int32>(Privilege));
			break;
		}
		return MLPrivilegeID_Invalid;
	}

	FString MAGICLEAPPRIVILEGES_API MLPrivilegeToString(MLPrivilegeID PrivilegeID)
	{
		#define PRIV_TO_STR_CASE(x) case x: { return UTF8_TO_TCHAR((#x)); }
		switch (PrivilegeID)
		{
		PRIV_TO_STR_CASE(MLPrivilegeID_Invalid)
		PRIV_TO_STR_CASE(MLPrivilegeID_BatteryInfo)
		PRIV_TO_STR_CASE(MLPrivilegeID_CameraCapture)
		PRIV_TO_STR_CASE(MLPrivilegeID_WorldReconstruction)
		PRIV_TO_STR_CASE(MLPrivilegeID_InAppPurchase)
		PRIV_TO_STR_CASE(MLPrivilegeID_AudioCaptureMic)
		PRIV_TO_STR_CASE(MLPrivilegeID_DrmCertificates)
		PRIV_TO_STR_CASE(MLPrivilegeID_Occlusion)
		PRIV_TO_STR_CASE(MLPrivilegeID_LowLatencyLightwear)
		PRIV_TO_STR_CASE(MLPrivilegeID_Internet)
		PRIV_TO_STR_CASE(MLPrivilegeID_IdentityRead)
		PRIV_TO_STR_CASE(MLPrivilegeID_BackgroundDownload)
		PRIV_TO_STR_CASE(MLPrivilegeID_BackgroundUpload)
		PRIV_TO_STR_CASE(MLPrivilegeID_MediaDrm)
		PRIV_TO_STR_CASE(MLPrivilegeID_Media)
		PRIV_TO_STR_CASE(MLPrivilegeID_MediaMetadata)
		PRIV_TO_STR_CASE(MLPrivilegeID_PowerInfo)
		PRIV_TO_STR_CASE(MLPrivilegeID_LocalAreaNetwork)
		PRIV_TO_STR_CASE(MLPrivilegeID_VoiceInput)
		PRIV_TO_STR_CASE(MLPrivilegeID_Documents)
		PRIV_TO_STR_CASE(MLPrivilegeID_ConnectBackgroundMusicService)
		PRIV_TO_STR_CASE(MLPrivilegeID_RegisterBackgroundMusicService)
		PRIV_TO_STR_CASE(MLPrivilegeID_PwFoundObjRead)
		PRIV_TO_STR_CASE(MLPrivilegeID_NormalNotificationsUsage)
		PRIV_TO_STR_CASE(MLPrivilegeID_MusicService)
		PRIV_TO_STR_CASE(MLPrivilegeID_ControllerPose)
		PRIV_TO_STR_CASE(MLPrivilegeID_ScreensProvider)
		PRIV_TO_STR_CASE(MLPrivilegeID_GesturesSubscribe)
		PRIV_TO_STR_CASE(MLPrivilegeID_GesturesConfig)
		PRIV_TO_STR_CASE(MLPrivilegeID_AddressBookRead)
		PRIV_TO_STR_CASE(MLPrivilegeID_AddressBookWrite)
		PRIV_TO_STR_CASE(MLPrivilegeID_CoarseLocation)
		// TODO: @njain uncomment after enum is added to c-api
		// PRIV_TO_STR_CASE(MLPrivilegeID_HandMesh)
		PRIV_TO_STR_CASE(MLPrivilegeID_WifiStatusRead)
		default:
			UE_LOG(LogMagicLeapPrivileges, Error, TEXT("Unmapped privilege %d"), static_cast<int32>(PrivilegeID));
			break;
		}

		return UTF8_TO_TCHAR("");
	}

	FString MAGICLEAPPRIVILEGES_API MLPrivilegeToString(EMagicLeapPrivilege PrivilegeID)
	{
		return MLPrivilegeToString(UnrealToMLPrivilege(PrivilegeID));
	}
}
#endif //WITH_MLSDK
