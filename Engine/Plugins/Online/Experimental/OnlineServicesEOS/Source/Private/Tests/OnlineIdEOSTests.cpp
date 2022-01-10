// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "OnlineIdEOS.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAccountIdReplicationTest, 
	"System.Engine.Online.EosAccountIdReplicationTest", 
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FAccountIdReplicationTest::RunTest(const FString& Parameters)
{
	using namespace UE::Online;
	const EOS_EpicAccountId EasId = EOS_EpicAccountId_FromString("d03352b601a44af7b02ed1faab668e0f");
	const EOS_ProductUserId EosId = EOS_ProductUserId_FromString("31a0b16a1f0743978458e3733bb3be14");

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

	return true;
}