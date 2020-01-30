// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"
#include "DMXProtocolArtNetModule.h"

#include "DMXProtocolArtNet.h"
#include "DMXProtocolPortArtNet.h"

#include "Managers/DMXProtocolDeviceManager.h"
#include "Managers/DMXProtocolInterfaceManager.h"
#include "Managers/DMXProtocolPortManager.h"
#include "Managers/DMXProtocolUniverseManager.h"

#include "DMXProtocolInterfaceArtNet.h"
#include "DMXProtocolDeviceArtNet.h"
#include "DMXProtocolUniverseArtNet.h"
#include "DMXProtocolPortArtNet.h"
#include "DMXProtocolArtNetConstants.h"

struct FDMXProtocolLauncher
{
	FDMXProtocolLauncher(uint8 InDeviceID, uint8 InPortID)
		: DeviceID(InDeviceID)
		, PortID(InPortID)
		, InterfaceID(0)
	{
		// Init Protocol and Managers
		DMXProtocol = IDMXProtocol::Get<FDMXProtocolArtNet>(FDMXProtocolArtNetModule::NAME_Artnet);

		InterfaceArtNet = MakeShared<FDMXProtocolInterfaceArtNet>(DMXProtocol);
		DMXProtocol->GetInterfaceManager()->AddInterface(InterfaceID, InterfaceArtNet);

		FJsonObject DeviceSettings;
		ProtocolDevice = MakeShared<FDMXProtocolDeviceArtNet>(DMXProtocol, InterfaceArtNet, DeviceSettings, DeviceID);
		DMXProtocol->GetDeviceManager()->AddDevice(ProtocolDevice);

		FJsonObject OutputPortSettings;
		OutputPortSettings.SetNumberField(TEXT("Net"), 0);
		OutputPortSettings.SetNumberField(TEXT("Subnet"), 1);
		OutputPortSettings.SetNumberField(TEXT("Universe"), 0);
		ProtocolOutputPort = MakeShared<FDMXProtocolPortArtNet>(DMXProtocol, ProtocolDevice, OutputPortSettings, PortID, DMX_PORT_OUTPUT);
		DMXProtocol->GetPortManager()->AddOutputPort(ProtocolDevice, ProtocolOutputPort);

		ProtocolUniverse = MakeShared<FDMXProtocolUniverseArtNet>(DMXProtocol, ProtocolOutputPort);
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
	TSharedPtr<FDMXProtocolInterfaceArtNet> InterfaceArtNet;
	TSharedPtr<FDMXProtocolDeviceArtNet> ProtocolDevice;
	TSharedPtr<FDMXProtocolPortArtNet> ProtocolOutputPort;
	TSharedPtr<FDMXProtocolUniverseArtNet> ProtocolUniverse;
	FDMXProtocolArtNet* DMXProtocol;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDMXPrtotocolArtNetBasicFlowTest, "VirtualProduction.DMX.ArtNet.BasicFlow", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)

static void SensDMXFragment(FAutomationTestBase& Test, const FDMXProtocolLauncher& DMXProtocolLauncher)
{
	FDMXProtocolPortArtNet* ArtNetPort = static_cast<FDMXProtocolPortArtNet*>(DMXProtocolLauncher.DMXProtocol->GetPortManager()->GetPortByDeviceAndID(DMXProtocolLauncher.ProtocolDevice, DMXProtocolLauncher.PortID, EDMXPortDirection::DMX_PORT_OUTPUT));

	Test.TestNotNull(TEXT("ArtNetPort is valid"), ArtNetPort);

	Test.TestEqual(TEXT("Universe ID should be equal 16"), ArtNetPort->GetUniverseID(), 16);

	// Send DMX fragment 1
	const uint8 FixtureChannels[6] = { 30, 31, 32, 36, 37, 38 };
	const uint8 FixtureValues[6] = { 255, 100, 20, 77, 88, 99 };
	IDMXFragmentMap DMXFragmentMap;
	DMXFragmentMap.Add(FixtureChannels[0], FixtureValues[0]);
	DMXFragmentMap.Add(FixtureChannels[1], FixtureValues[1]);
	DMXFragmentMap.Add(FixtureChannels[2], FixtureValues[2]);
	DMXProtocolLauncher.DMXProtocol->SetDMXFragment(ArtNetPort->GetUniverseID(), DMXFragmentMap);

	// Wait before recieve DMX
	FPlatformProcess::Sleep(0.2f);
	TSharedPtr<IDMXProtocolUniverse> Universe = ArtNetPort->GetCachedUniverse().Pin();
	Test.TestTrue(TEXT("Universe should be valid"), Universe.IsValid());
	TSharedPtr<FDMXBuffer> InputDMXBuffer = Universe->GetInputDMXBuffer();

	Test.TestEqual(TEXT("Incoming buffer should be same"), FixtureValues[0], InputDMXBuffer->GetDMXData()[FixtureChannels[0]]);
	Test.TestEqual(TEXT("Incoming buffer should be same"), FixtureValues[1], InputDMXBuffer->GetDMXData()[FixtureChannels[1]]);
	Test.TestEqual(TEXT("Incoming buffer should be same"), FixtureValues[2], InputDMXBuffer->GetDMXData()[FixtureChannels[2]]);

	// Send DMX fragment 2
	DMXFragmentMap.Add(FixtureChannels[3], FixtureValues[3]);
	DMXFragmentMap.Add(FixtureChannels[4], FixtureValues[4]);
	DMXFragmentMap.Add(FixtureChannels[5], FixtureValues[5]);
	DMXProtocolLauncher.DMXProtocol->SetDMXFragment(ArtNetPort->GetUniverseID(), DMXFragmentMap);

	FPlatformProcess::Sleep(0.2f);

	Test.TestEqual(TEXT("Old value DMX input should be the same"), FixtureValues[0], InputDMXBuffer->GetDMXData()[FixtureChannels[0]]);
	Test.TestEqual(TEXT("Incoming buffer should be same"), FixtureValues[4], InputDMXBuffer->GetDMXData()[FixtureChannels[4]]);
	Test.TestEqual(TEXT("Incoming buffer should be same"), FixtureValues[5], InputDMXBuffer->GetDMXData()[FixtureChannels[5]]);

	// @TODO Implement test cases for Pool and RDM
	// Send DMX pull
	DMXProtocolLauncher.DMXProtocol->TransmitPoll();

	// Single TOD Request
	{
		// Send TOD to non existent device
		{
			FJsonObject DeviceSettings;
			TSharedPtr<FDMXProtocolDeviceArtNet> NonExistentDevice = MakeShared<FDMXProtocolDeviceArtNet>(DMXProtocolLauncher.DMXProtocol, DMXProtocolLauncher.InterfaceArtNet, DeviceSettings, 987);
			DMXProtocolLauncher.DMXProtocol->TransmitTodRequest(NonExistentDevice);
			FPlatformProcess::Sleep(0.2f);
			const FDMXProtocolArtNetTodRequest& IncomingTodRequest = DMXProtocolLauncher.DMXProtocol->GetIncomingTodRequest();
			Test.TestEqual(TEXT("TodRequest.AdCount should be 0"), IncomingTodRequest.AdCount, uint8(0));
		}

		// Send TOD to existing device
		{
			DMXProtocolLauncher.DMXProtocol->TransmitTodRequest(DMXProtocolLauncher.ProtocolDevice);
			FPlatformProcess::Sleep(0.2f);
			const FDMXProtocolArtNetTodRequest& IncomingTodRequest = DMXProtocolLauncher.DMXProtocol->GetIncomingTodRequest();
			Test.TestNotEqual(TEXT("TodRequest.AdCount should not be 0"), IncomingTodRequest.AdCount, uint8(0));
		}
	}

	// Test ArtNet RDM
	DMXProtocolLauncher.DMXProtocol->TransmitTodRequestToAll();
	ArtNetPort->AddTODUID(FRDMUID({ 0x00, 0x01, 0x02, 0x03, 0x04, 0x05 }));
	ArtNetPort->AddTODUID(FRDMUID({ 0x06, 0x07, 0x08, 0x09, 0x10, 0x11 }));

	FRDMUID RDMUID3;
	uint8 RDMBuffer3[RDM_UID_WIDTH] = { 0x12, 0x13, 0x14, 0x15, 0x16, 0x17 };
	FMemory::Memcpy(RDMUID3.Buffer, RDMBuffer3, RDM_UID_WIDTH);
	ArtNetPort->AddTODUID(RDMUID3);

	// Test wrong size of RDM Buffer C array
	uint8 RDMBuffer4[3] = { 0x18, 0x19, 0x20 };
	ArtNetPort->AddTODUID(FRDMUID(RDMBuffer4));

	// Test wrong size of RDM Buffer C array
	uint8 RDMBuffer5[10] = { 0x26, 0x27, 0x28, 0x29, 0x30, 0x31, 0x32, 0x32, 0x33 };
	ArtNetPort->AddTODUID(FRDMUID(RDMBuffer5));

	// Test wrong size of RDM Buffer TArray
	ArtNetPort->AddTODUID(FRDMUID({ 0x34, 0x35 }));

	DMXProtocolLauncher.DMXProtocol->TransmitTodData(DMXProtocolLauncher.ProtocolDevice, DMXProtocolLauncher.PortID);
	DMXProtocolLauncher.DMXProtocol->TransmitTodControl(DMXProtocolLauncher.ProtocolDevice, DMXProtocolLauncher.PortID, 0x02);

	TArray<uint8> RDMData;
	RDMData.AddZeroed(ARTNET_MAX_RDM_DATA);
	RDMData[47] = 0x00;
	RDMData[48] = 0xcc;
	DMXProtocolLauncher.DMXProtocol->TransmitRDM(DMXProtocolLauncher.ProtocolDevice, DMXProtocolLauncher.PortID, RDMData);
}

bool FDMXPrtotocolArtNetBasicFlowTest::RunTest(const FString& Parameters)
{
	FDMXProtocolLauncher DMXProtocolLauncher(0, 0);
	// Test Sending DMX and RDM
	SensDMXFragment(*this, DMXProtocolLauncher);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDMXPrtotocolArtNetConsoleCommandsTest, "VirtualProduction.DMX.ArtNet.ConsoleCommands", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDMXPrtotocolArtNetConsoleCommandsTest::RunTest(const FString& Parameters)
{
	FDMXProtocolLauncher DMXProtocolLauncher(0, 0);

	// DMX.ArtNet.SendDMX[UniverseID] Channel:Value Channel : Value Channel : Value ...
	uint32 Values[3][2] =
	{
		{25, 125},
		{35, 25},
		{45, 75},
	};

	FString Command(
		FString::Printf(TEXT("DMX.ArtNet.SendDMX %d %d:%d %d:%d %d:%d"),
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