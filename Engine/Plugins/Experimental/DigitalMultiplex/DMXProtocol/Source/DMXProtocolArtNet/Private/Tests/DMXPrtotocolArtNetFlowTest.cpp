// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"
#include "DMXProtocolArtNetModule.h"

#include "DMXProtocolArtNet.h"
#include "Managers/DMXProtocolUniverseManager.h"
#include "DMXProtocolUniverseArtNet.h"

#include "DMXProtocolUniverseArtNet.h"
#include "DMXProtocolArtNetConstants.h"


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDMXPrtotocolArtNetBasicFlowTest, "VirtualProduction.DMX.ArtNet.BasicFlow", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDMXPrtotocolArtNetBasicFlowTest::RunTest(const FString& Parameters)
{
	static const uint16 UniverseValue = 0;

	// Init Protocol and Managers
	FDMXProtocolArtNet* DMXProtocol = static_cast<FDMXProtocolArtNet*>(IDMXProtocol::Get(FDMXProtocolArtNetModule::NAME_Artnet).Get());

	FJsonObject UniverseSettings;
	UniverseSettings.SetNumberField(TEXT("UniverseID"), UniverseValue);
	UniverseSettings.SetNumberField(TEXT("PortID"), 0); // TODO set PortID
	DMXProtocol->AddUniverse(UniverseSettings);

	TSharedPtr<IDMXProtocolUniverse, ESPMode::ThreadSafe> Universe = DMXProtocol->GetUniverseById(UniverseValue);
	TestTrue(TEXT("Universe is valid"), Universe.IsValid());
	FDMXProtocolUniverseArtNet* UniverseArtNet = static_cast<FDMXProtocolUniverseArtNet*>(Universe.Get());

	// Send DMX fragment 1
	const uint8 FixtureChannels[6] = { 30, 31, 32, 36, 37, 38 };
	const uint8 FixtureValues[6] = { 255, 100, 20, 77, 88, 99 };
	IDMXFragmentMap DMXFragmentMap;
	DMXFragmentMap.Add(FixtureChannels[0], FixtureValues[0]);
	DMXFragmentMap.Add(FixtureChannels[1], FixtureValues[1]);
	DMXFragmentMap.Add(FixtureChannels[2], FixtureValues[2]);
	DMXProtocol->SendDMXFragment(UniverseValue, DMXFragmentMap);

	// Wait before recieve DMX
	FPlatformProcess::Sleep(0.2f);
	FDMXBufferPtr InputDMXBuffer = UniverseArtNet->GetInputDMXBuffer();

	TestEqual(TEXT("Incoming buffer should be same"), FixtureValues[0], InputDMXBuffer->GetDMXDataAddress(FixtureChannels[0] - 1));
	TestEqual(TEXT("Incoming buffer should be same"), FixtureValues[1], InputDMXBuffer->GetDMXDataAddress(FixtureChannels[1] - 1));
	TestEqual(TEXT("Incoming buffer should be same"), FixtureValues[2], InputDMXBuffer->GetDMXDataAddress(FixtureChannels[2] - 1));

	// Send DMX fragment 2
	DMXFragmentMap.Add(FixtureChannels[3], FixtureValues[3]);
	DMXFragmentMap.Add(FixtureChannels[4], FixtureValues[4]);
	DMXFragmentMap.Add(FixtureChannels[5], FixtureValues[5]);
	DMXProtocol->SendDMXFragment(UniverseValue, DMXFragmentMap);

	FPlatformProcess::Sleep(0.2f);

	TestEqual(TEXT("Old value DMX input should be the same"), FixtureValues[0], InputDMXBuffer->GetDMXDataAddress(FixtureChannels[0] - 1));
	TestEqual(TEXT("Incoming buffer should be same"), FixtureValues[4], InputDMXBuffer->GetDMXDataAddress(FixtureChannels[4] - 1));
	TestEqual(TEXT("Incoming buffer should be same"), FixtureValues[5], InputDMXBuffer->GetDMXDataAddress(FixtureChannels[5] - 1));

	// @TODO Implement test cases for Pool and RDM
	// Send DMX pull
	DMXProtocol->TransmitPoll();
	FPlatformProcess::Sleep(0.2f);
	const FDMXProtocolArtNetPollPacket& PollPacket = DMXProtocol->GetIncomingPoll();
	TestEqual(TEXT("OpCode should be ARTNET_POLL"), ARTNET_POLL, PollPacket.OpCode);

	// Send TOD 
	DMXProtocol->TransmitTodRequest(UniverseArtNet->GetPortAddress());
	FPlatformProcess::Sleep(0.2f);
	const FDMXProtocolArtNetTodRequest& IncomingTodRequest = DMXProtocol->GetIncomingTodRequest();
	TestEqual(TEXT("TodRequest.AdCount should not be 1"), IncomingTodRequest.AdCount, uint8(1));

	// Test ArtNet RDM
	DMXProtocol->TransmitTodRequestToAll();
	UniverseArtNet->AddTODUID(FRDMUID({ 0x00, 0x01, 0x02, 0x03, 0x04, 0x05 }));
	UniverseArtNet->AddTODUID(FRDMUID({ 0x06, 0x07, 0x08, 0x09, 0x10, 0x11 }));

	FRDMUID RDMUID3;
	uint8 RDMBuffer3[RDM_UID_WIDTH] = { 0x12, 0x13, 0x14, 0x15, 0x16, 0x17 };
	FMemory::Memcpy(RDMUID3.Buffer, RDMBuffer3, RDM_UID_WIDTH);
	UniverseArtNet->AddTODUID(RDMUID3);

	// Test wrong size of RDM Buffer C array
	uint8 RDMBuffer4[3] = { 0x18, 0x19, 0x20 };
	UniverseArtNet->AddTODUID(FRDMUID(RDMBuffer4));

	// Test wrong size of RDM Buffer C array
	uint8 RDMBuffer5[10] = { 0x26, 0x27, 0x28, 0x29, 0x30, 0x31, 0x32, 0x32, 0x33 };
	UniverseArtNet->AddTODUID(FRDMUID(RDMBuffer5));

	// Test wrong size of RDM Buffer TArray
	UniverseArtNet->AddTODUID(FRDMUID({ 0x34, 0x35 }));

	DMXProtocol->TransmitTodData(UniverseArtNet->GetUniverseID());
	DMXProtocol->TransmitTodControl(UniverseArtNet->GetUniverseID(), 0x02);

	TArray<uint8> RDMData;
	RDMData.AddZeroed(ARTNET_MAX_RDM_DATA);
	RDMData[47] = 0x00;
	RDMData[48] = 0xcc;
	DMXProtocol->TransmitRDM(UniverseArtNet->GetUniverseID(), RDMData);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDMXPrtotocolArtNetConsoleCommandsTest, "VirtualProduction.DMX.ArtNet.ConsoleCommands", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDMXPrtotocolArtNetConsoleCommandsTest::RunTest(const FString& Parameters)
{
	static const uint16 UniverseValue = 1891;

	FDMXProtocolArtNet* DMXProtocol = static_cast<FDMXProtocolArtNet*>(IDMXProtocol::Get(FDMXProtocolArtNetModule::NAME_Artnet).Get());

	FJsonObject UniverseSettings;
	UniverseSettings.SetNumberField(TEXT("UniverseID"), UniverseValue);
	UniverseSettings.SetNumberField(TEXT("PortID"), 0); // TODO set PortID
	DMXProtocol->AddUniverse(UniverseSettings);
	TSharedPtr<IDMXProtocolUniverse, ESPMode::ThreadSafe> Universe = DMXProtocol->GetUniverseById(UniverseValue);


	// DMX.ArtNet.SendDMX[UniverseID] Channel:Value Channel : Value Channel : Value ...
	uint32 Values[3][2] =
	{
		{25, 125},
		{35, 25},
		{45, 75},
	};

	FString Command(
		FString::Printf(TEXT("DMX.ArtNet.SendDMX %d %d:%d %d:%d %d:%d"),
			UniverseValue,
			Values[0][0], Values[0][1],
			Values[1][0], Values[1][1],
			Values[2][0], Values[2][1]
		));

	GEngine->Exec(GWorld, *Command);

	FPlatformProcess::Sleep(0.2f);

	FDMXBufferPtr InputDMXBuffer = Universe->GetInputDMXBuffer();
	TestEqual(TEXT("Incoming buffer should be same"), Values[0][1], InputDMXBuffer->GetDMXDataAddress(Values[0][0] - 1));
	TestEqual(TEXT("Incoming buffer should be same"), Values[1][1], InputDMXBuffer->GetDMXDataAddress(Values[1][0] - 1));
	TestEqual(TEXT("Incoming buffer should be same"), Values[2][1], InputDMXBuffer->GetDMXDataAddress(Values[2][0] - 1));

	return true;
}