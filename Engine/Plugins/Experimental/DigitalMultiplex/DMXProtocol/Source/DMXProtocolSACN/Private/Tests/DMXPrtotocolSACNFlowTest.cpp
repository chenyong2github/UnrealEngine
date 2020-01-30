// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"
#include "DMXProtocolSACNModule.h"

#include "DMXProtocolSACN.h"

#include "DMXProtocolSACN.h"
#include "DMXProtocolPortSACN.h"

#include "Managers/DMXProtocolDeviceManager.h"
#include "Managers/DMXProtocolInterfaceManager.h"
#include "Managers/DMXProtocolPortManager.h"
#include "Managers/DMXProtocolUniverseManager.h"

#include "DMXProtocolInterfaceSACN.h"
#include "DMXProtocolDeviceSACN.h"
#include "DMXProtocolUniverseSACN.h"
#include "DMXProtocolPortSACN.h"
#include "DMXProtocolSACNConstants.h"


struct FDMXProtocolLauncher
{
	FDMXProtocolLauncher(uint8 InDeviceID, uint8 InPortID)
		: DeviceID(InDeviceID)
		, PortID(InPortID)
		, InterfaceID(0)
		, UniverseID(3)
	{
		DMXProtocol = IDMXProtocol::Get<FDMXProtocolSACN>(FDMXProtocolSACNModule::NAME_SACN);

		InterfaceSACN = MakeShared<FDMXProtocolInterfaceSACN>(DMXProtocol);
		DMXProtocol->GetInterfaceManager()->AddInterface(0, InterfaceSACN);

		FJsonObject DeviceSettings;
		ProtocolDevice = MakeShared<FDMXProtocolDeviceSACN>(DMXProtocol, InterfaceSACN, DeviceSettings, DeviceID);
		DMXProtocol->GetDeviceManager()->AddDevice(ProtocolDevice);

		ProtocolOutputPort = MakeShared<FDMXProtocolPortSACN>(DMXProtocol, ProtocolDevice, PortID, UniverseID, DMX_PORT_OUTPUT);
		DMXProtocol->GetPortManager()->AddOutputPort(ProtocolDevice, ProtocolOutputPort);

		FJsonObject UniverseSettings;
		ProtocolUniverse = MakeShared<FDMXProtocolUniverseSACN>(DMXProtocol, ProtocolOutputPort, UniverseID);
		DMXProtocol->GetUniverseManager()->AddUniverse(ProtocolOutputPort, ProtocolUniverse);
		ProtocolOutputPort->SetUniverse(ProtocolUniverse);
	}

	~FDMXProtocolLauncher()
	{
		DMXProtocol->GetInterfaceManager()->RemoveInterface(InterfaceID);
		DMXProtocol->GetDeviceManager()->RemoveDevice(ProtocolDevice);
		DMXProtocol->GetPortManager()->RemoveOutputPort(ProtocolDevice, ProtocolOutputPort);
		DMXProtocol->GetUniverseManager()->RemoveUniverse(ProtocolOutputPort, ProtocolUniverse);
	}

	const uint8 DeviceID;
	const uint8 PortID;
	const uint8 InterfaceID;
	const uint16 UniverseID;
	TSharedPtr<FDMXProtocolInterfaceSACN> InterfaceSACN;
	TSharedPtr<FDMXProtocolDeviceSACN> ProtocolDevice;
	TSharedPtr<FDMXProtocolPortSACN> ProtocolOutputPort;
	TSharedPtr<FDMXProtocolUniverseSACN> ProtocolUniverse;
	FDMXProtocolSACN* DMXProtocol;
};


IMPLEMENT_SIMPLE_AUTOMATION_TEST(DMXPrtotocolSACNBasicFlowTest, "VirtualProduction.DMX.SACN.BasicFlow", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)

static void SensDMXFragment(FAutomationTestBase& Test, FDMXProtocolSACN* DMXProtocol, TSharedPtr<FDMXProtocolDeviceSACN> InProtocolDevice, uint8 InPortID)
{
	IDMXProtocolPort* SACNPort = DMXProtocol->GetPortManager()->GetPortByDeviceAndID(InProtocolDevice, InPortID, EDMXPortDirection::DMX_PORT_OUTPUT);
	Test.TestNotNull(TEXT("SACNPort is valid"), SACNPort);
	Test.TestEqual(TEXT("Universe ID should be equal 3"), SACNPort->GetUniverseID(), 3);


	// Send DMX fragment 1
	const uint8 FixtureChannels[6] = { 0, 1, 2, 5, 6, 7 };
	const uint8 FixtureValues[6] = { 255, 155, 50, 100, 200, 220 };
	IDMXFragmentMap DMXFragmentMap;
	DMXFragmentMap.Add(FixtureChannels[0], FixtureValues[0]);
	DMXFragmentMap.Add(FixtureChannels[1], FixtureValues[1]);
	DMXFragmentMap.Add(FixtureChannels[2], FixtureValues[2]);
	DMXProtocol->SetDMXFragment(SACNPort->GetUniverseID(), DMXFragmentMap);

	// Wait before recieve DMX
	FPlatformProcess::Sleep(0.2f);
	TSharedPtr<IDMXProtocolUniverse> Universe = SACNPort->GetCachedUniverse().Pin();
	Test.TestTrue(TEXT("Universe should be valid"), Universe.IsValid());
	TSharedPtr<FDMXBuffer> InputDMXBuffer = Universe->GetInputDMXBuffer();

	// Test package ACNPacketIdentifier
	{
		const uint8 ACNPacketIdentifier[ACN_IDENTIFIER_SIZE] = { 'A', 'S', 'C', '-', 'E', '1', '.', '1', '7', '\0', '\0', '\0' };
		FDMXProtocolUniverseSACN* DMXProtocolUniverseSACN = static_cast<FDMXProtocolUniverseSACN*>(Universe.Get());
		FString ExpectedInentifier = FString::Printf(TEXT("%s"), ACNPacketIdentifier);
		FString IncomingInentifier = FString::Printf(TEXT("%s"), DMXProtocolUniverseSACN->GetIncomingDMXRootLayer().ACNPacketIdentifier);
		Test.TestTrue(TEXT("Incoming ACNPacketIdentifier should be the same"), ExpectedInentifier.Equals(IncomingInentifier));
	}

	Test.TestEqual(TEXT("Incoming buffer should be same"), FixtureValues[0], InputDMXBuffer->GetDMXData()[FixtureChannels[0]]);
	Test.TestEqual(TEXT("Incoming buffer should be same"), FixtureValues[1], InputDMXBuffer->GetDMXData()[FixtureChannels[1]]);
	Test.TestEqual(TEXT("Incoming buffer should be same"), FixtureValues[2], InputDMXBuffer->GetDMXData()[FixtureChannels[2]]);

	// Send DMX fragment 2
	DMXFragmentMap.Add(FixtureChannels[3], FixtureValues[3]);
	DMXFragmentMap.Add(FixtureChannels[4], FixtureValues[4]);
	DMXFragmentMap.Add(FixtureChannels[5], FixtureValues[5]);
	DMXProtocol->SetDMXFragment(SACNPort->GetUniverseID(), DMXFragmentMap);
	FPlatformProcess::Sleep(0.2f);

	Test.TestEqual(TEXT("Old value DMX input should be the same"), FixtureValues[0], InputDMXBuffer->GetDMXData()[FixtureChannels[0]]);
	Test.TestEqual(TEXT("Incoming buffer should be same"), FixtureValues[4], InputDMXBuffer->GetDMXData()[FixtureChannels[4]]);
	Test.TestEqual(TEXT("Incoming buffer should be same"), FixtureValues[5], InputDMXBuffer->GetDMXData()[FixtureChannels[5]]);

	// @TODO Implement test cases for Discovery
	TArray<uint16> Universes;
	for (int UniversesIndex = 0; UniversesIndex <= DMX_UNIVERSE_SIZE; UniversesIndex++)
	{
		Universes.Add(UniversesIndex);
	}
	DMXProtocol->SendDiscovery(Universes);
}

bool DMXPrtotocolSACNBasicFlowTest::RunTest(const FString& Parameters)
{
	FDMXProtocolLauncher DMXProtocolLauncher(0, 0);

	// Test Sending DMX and Discovery
	SensDMXFragment(*this, DMXProtocolLauncher.DMXProtocol, DMXProtocolLauncher.ProtocolDevice, DMXProtocolLauncher.PortID);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDMXPrtotocolSACNConsoleCommandsTest, "VirtualProduction.DMX.SACN.ConsoleCommands", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDMXPrtotocolSACNConsoleCommandsTest::RunTest(const FString& Parameters)
{
	FDMXProtocolLauncher DMXProtocolLauncher(0, 0);

	// DMX.SACN.SendDMX[UniverseID] Channel:Value Channel : Value Channel : Value ...
	uint32 Values[3][2] =
	{
		{95, 105},
		{88, 41},
		{12, 1},
	};

	FString Command(
		FString::Printf(TEXT("DMX.SACN.SendDMX %d %d:%d %d:%d %d:%d"),
			DMXProtocolLauncher.ProtocolOutputPort->GetUniverseID(),
			Values[0][0], Values[0][1],
			Values[1][0], Values[1][1],
			Values[2][0], Values[2][1]
		));

	GEngine->Exec(GWorld, *Command);

	FPlatformProcess::Sleep(0.2f);

	TSharedPtr<IDMXProtocolUniverse> Universe = DMXProtocolLauncher.ProtocolOutputPort->GetCachedUniverse().Pin();
	TSharedPtr<FDMXBuffer> InputDMXBuffer = Universe->GetInputDMXBuffer();
	TestEqual(TEXT("Incoming buffer should be same"), Values[0][1], InputDMXBuffer->GetDMXData()[Values[0][0]]);
	TestEqual(TEXT("Incoming buffer should be same"), Values[1][1], InputDMXBuffer->GetDMXData()[Values[1][0]]);
	TestEqual(TEXT("Incoming buffer should be same"), Values[2][1], InputDMXBuffer->GetDMXData()[Values[2][0]]);

	return true;
}