// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"
#include "DMXProtocolSACNModule.h"

#include "DMXProtocolSACN.h"

#include "DMXProtocolSACN.h"
#include "DMXProtocolUniverseSACN.h"

static uint16 UniverseValue = 34;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(DMXPrtotocolSACNBasicFlowTest, "VirtualProduction.DMX.SACN.BasicFlow", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool DMXPrtotocolSACNBasicFlowTest::RunTest(const FString& Parameters)
{
	// Init Protocol and Managers
	FDMXProtocolSACN* DMXProtocol = static_cast<FDMXProtocolSACN*>((IDMXProtocol::Get(FDMXProtocolSACNModule::NAME_SACN)).Get());

	FJsonObject UniverseSettings;
	UniverseSettings.SetNumberField(TEXT("UniverseID"), UniverseValue);
	DMXProtocol->AddUniverse(UniverseSettings);
	TSharedPtr<IDMXProtocolUniverse, ESPMode::ThreadSafe> Universe = DMXProtocol->GetUniverseById(UniverseValue);

	TestTrue(TEXT("Universe is valid"), Universe.IsValid());
	TestEqual(TEXT("Universe ID should be equal"), Universe->GetUniverseID(), UniverseValue);

	// Send DMX fragment 1
	const uint8 FixtureChannels[6] = { 1, 2, 3, 4, 5, 6 };
	const uint8 FixtureValues[6] = { 255, 155, 50, 100, 200, 220 };
	IDMXFragmentMap DMXFragmentMap;
	DMXFragmentMap.Add(FixtureChannels[0], FixtureValues[0]);
	DMXFragmentMap.Add(FixtureChannels[1], FixtureValues[1]);
	DMXFragmentMap.Add(FixtureChannels[2], FixtureValues[2]);
	DMXProtocol->SendDMXFragment(Universe->GetUniverseID(), DMXFragmentMap);

	// Wait before receive DMX
	FPlatformProcess::Sleep(0.2f);
	TSharedPtr<FDMXBuffer> InputDMXBuffer = Universe->GetInputDMXBuffer();

	// Test package ACNPacketIdentifier
	{
		const uint8 ACNPacketIdentifier[ACN_IDENTIFIER_SIZE] = { 'A', 'S', 'C', '-', 'E', '1', '.', '1', '7', '\0', '\0', '\0' };
		FDMXProtocolUniverseSACN* DMXProtocolUniverseSACN = static_cast<FDMXProtocolUniverseSACN*>(Universe.Get());
		FString ExpectedInentifier = FString::Printf(TEXT("%s"), ACNPacketIdentifier);
		FString IncomingInentifier = FString::Printf(TEXT("%s"), DMXProtocolUniverseSACN->GetIncomingDMXRootLayer().ACNPacketIdentifier);
		TestTrue(TEXT("Incoming ACNPacketIdentifier should be the same"), ExpectedInentifier.Equals(IncomingInentifier));
	}

	TestEqual(TEXT("Incoming buffer should be same"), FixtureValues[0], InputDMXBuffer->GetDMXData()[FixtureChannels[0] - 1]);
	TestEqual(TEXT("Incoming buffer should be same"), FixtureValues[1], InputDMXBuffer->GetDMXData()[FixtureChannels[1] - 1]);
	TestEqual(TEXT("Incoming buffer should be same"), FixtureValues[2], InputDMXBuffer->GetDMXData()[FixtureChannels[2] - 1]);

	// Send DMX fragment 2
	DMXFragmentMap.Add(FixtureChannels[3], FixtureValues[3]);
	DMXFragmentMap.Add(FixtureChannels[4], FixtureValues[4]);
	DMXFragmentMap.Add(FixtureChannels[5], FixtureValues[5]);
	DMXProtocol->SendDMXFragment(Universe->GetUniverseID(), DMXFragmentMap);
	FPlatformProcess::Sleep(0.2f);

	TestEqual(TEXT("Old value DMX input should be the same"), FixtureValues[0], InputDMXBuffer->GetDMXData()[FixtureChannels[0] - 1]);
	TestEqual(TEXT("Incoming buffer should be same"), FixtureValues[4], InputDMXBuffer->GetDMXData()[FixtureChannels[4] - 1]);
	TestEqual(TEXT("Incoming buffer should be same"), FixtureValues[5], InputDMXBuffer->GetDMXData()[FixtureChannels[5] - 1]);

	// @TODO Implement test cases for Discovery
	TArray<uint16> Universes;
	for (int32 UniversesIndex = 0; UniversesIndex < DMX_UNIVERSE_SIZE; UniversesIndex++)
	{
		Universes.Add(UniversesIndex);
	}
	DMXProtocol->SendDiscovery(Universes);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDMXPrtotocolSACNConsoleCommandsTest, "VirtualProduction.DMX.SACN.ConsoleCommands", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDMXPrtotocolSACNConsoleCommandsTest::RunTest(const FString& Parameters)
{
	// Init Protocol and Managers
	FDMXProtocolSACN* DMXProtocol = static_cast<FDMXProtocolSACN*>((IDMXProtocol::Get(FDMXProtocolSACNModule::NAME_SACN)).Get());

	FJsonObject UniverseSettings;
	UniverseSettings.SetNumberField(TEXT("UniverseID"), UniverseValue);
	DMXProtocol->AddUniverse(UniverseSettings);
	TSharedPtr<IDMXProtocolUniverse, ESPMode::ThreadSafe> Universe = DMXProtocol->GetUniverseById(UniverseValue);

	// DMX.SACN.SendDMX[UniverseID] Channel:Value Channel : Value Channel : Value ...
	uint32 Values[3][2] =
	{
		{95, 105},
		{88, 41},
		{12, 1},
	};

	FString Command(
		FString::Printf(TEXT("DMX.SACN.SendDMX %d %d:%d %d:%d %d:%d"),
			Universe->GetUniverseID(),
			Values[0][0], Values[0][1],
			Values[1][0], Values[1][1],
			Values[2][0], Values[2][1]
		));

	GEngine->Exec(GWorld, *Command);

	FPlatformProcess::Sleep(0.2f);

	TSharedPtr<FDMXBuffer> InputDMXBuffer = Universe->GetInputDMXBuffer();
	TestEqual(TEXT("Incoming buffer should be same"), Values[0][1], InputDMXBuffer->GetDMXData()[Values[0][0] - 1]);
	TestEqual(TEXT("Incoming buffer should be same"), Values[1][1], InputDMXBuffer->GetDMXData()[Values[1][0] - 1]);
	TestEqual(TEXT("Incoming buffer should be same"), Values[2][1], InputDMXBuffer->GetDMXData()[Values[2][0] - 1]);

	return true;
}