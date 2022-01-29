// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "IEOSSDKManager.h"
#include "Online/OnlineIdEOS.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAccountIdReplicationTest, 
	"System.Engine.Online.EosAccountIdReplicationTest", 
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)
bool FAccountIdReplicationTest::RunTest(const FString& Parameters)
{
	IEOSSDKManager* SDKManager = IEOSSDKManager::Get();
	if(SDKManager && SDKManager->Initialize() == EOS_EResult::EOS_Success)
	{
		using namespace UE::Online;

		const EOS_EpicAccountId EasId = EOS_EpicAccountId_FromString("d03352b601a44af7b02ed1faab668e0f");
		check(EOS_EpicAccountId_IsValid(EasId));
		const EOS_ProductUserId EosId = EOS_ProductUserId_FromString("31a0b16a1f0743978458e3733bb3be14");
		check(EOS_ProductUserId_IsValid(EosId));

		{
			FOnlineAccountIdRegistryEOS Registry;
			const FOnlineAccountIdHandle Handle = Registry.Create(EasId, EosId);
			TArray<uint8> RepData = Registry.ToReplicationData(Handle);
			const FOnlineAccountIdHandle Handle2 = Registry.FromReplicationData(RepData);
			UTEST_EQUAL(TEXT(""), Handle, Handle2);
		}

		{
			FOnlineAccountIdRegistryEOS Registry;
			const FOnlineAccountIdHandle Handle = Registry.Create(EasId, nullptr);
			TArray<uint8> RepData = Registry.ToReplicationData(Handle);
			const FOnlineAccountIdHandle Handle2 = Registry.FromReplicationData(RepData);
			UTEST_EQUAL(TEXT(""), Handle, Handle2);
		}

		{
			FOnlineAccountIdRegistryEOS Registry;
			const FOnlineAccountIdHandle Handle = Registry.Create(nullptr, EosId);
			TArray<uint8> RepData = Registry.ToReplicationData(Handle);
			const FOnlineAccountIdHandle Handle2 = Registry.FromReplicationData(RepData);
			UTEST_EQUAL(TEXT(""), Handle, Handle2);
		}
	}

	return true;
}