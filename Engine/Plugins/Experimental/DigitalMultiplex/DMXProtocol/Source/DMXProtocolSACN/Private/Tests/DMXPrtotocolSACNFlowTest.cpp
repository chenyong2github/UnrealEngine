// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "DMXProtocolSACNModule.h"

#include "DMXProtocolSACN.h"

#include "DMXProtocolSACN.h"
#include "DMXProtocolUniverseSACN.h"

#if WITH_DEV_AUTOMATION_TESTS

struct DMXPrtotocolSACNHelper
{
	DMXPrtotocolSACNHelper(uint16 InUniverseID, FAutomationTestBase* InTest)
		: UniverseID(InUniverseID)
		, Test(InTest)
	{
		// Init Protocol and Managers
		DMXProtocol = static_cast<FDMXProtocolSACN*>((IDMXProtocol::Get(FDMXProtocolSACNModule::NAME_SACN)).Get());

		// Init Universe
		FJsonObject UniverseSettings;
		UniverseSettings.SetNumberField(TEXT("UniverseID"), InUniverseID);
		Universe = DMXProtocol->AddUniverse(UniverseSettings);

		// Call get buffer for start listening socket
		Universe->GetInputDMXBuffer();
	}

	~DMXPrtotocolSACNHelper()
	{
		DMXProtocol->RemoveUniverseById(UniverseID);
	}

	FDMXProtocolSACN* DMXProtocol;
	IDMXProtocolUniversePtr Universe;
	uint16 UniverseID;
	uint8 FixtureChannels[6] = { 1, 2, 3, 4, 5, 6 };
	uint8 FixtureValues[6] = { 255, 155, 50, 100, 200, 220 };

	/** Pointer to running automation test instance */
	FAutomationTestBase* Test;
};

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(DMXPrtotocolSACNBasicFlow_CheckUniverse_Part1, TSharedPtr<DMXPrtotocolSACNHelper>, Helper);
bool DMXPrtotocolSACNBasicFlow_CheckUniverse_Part1::Update()
{
	TSharedPtr<IDMXProtocolUniverse, ESPMode::ThreadSafe> Universe = Helper->DMXProtocol->GetUniverseById(Helper->UniverseID);

	Helper->Test->TestTrue(TEXT("Universe is valid"), Universe.IsValid());
	Helper->Test->TestEqual(TEXT("Universe ID should be equal"), Universe->GetUniverseID(), Helper->UniverseID);

	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(DMXPrtotocolSACNBasicFlow_SendDMX1_Part2, TSharedPtr<DMXPrtotocolSACNHelper>, Helper);
bool DMXPrtotocolSACNBasicFlow_SendDMX1_Part2::Update()
{
	IDMXFragmentMap DMXFragmentMap;
	DMXFragmentMap.Add(Helper->FixtureChannels[0], Helper->FixtureValues[0]);
	DMXFragmentMap.Add(Helper->FixtureChannels[1], Helper->FixtureValues[1]);
	DMXFragmentMap.Add(Helper->FixtureChannels[2], Helper->FixtureValues[2]);

	Helper->DMXProtocol->SendDMXFragment(Helper->Universe->GetUniverseID(), DMXFragmentMap);

	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(DMXPrtotocolSACNBasicFlow_CheckDMX1_Part3, TSharedPtr<DMXPrtotocolSACNHelper>, Helper);
bool DMXPrtotocolSACNBasicFlow_CheckDMX1_Part3::Update()
{
	FDMXBufferPtr InputDMXBuffer = Helper->Universe->GetInputDMXBuffer();

	// Test package ACNPacketIdentifier
	{
		const uint8 ACNPacketIdentifier[ACN_IDENTIFIER_SIZE] = { 'A', 'S', 'C', '-', 'E', '1', '.', '1', '7', '\0', '\0', '\0' };
		FDMXProtocolUniverseSACN* DMXProtocolUniverseSACN = static_cast<FDMXProtocolUniverseSACN*>(Helper->Universe.Get());
		FString ExpectedInentifier = FString::Printf(TEXT("%s"), ACNPacketIdentifier);
		FString IncomingInentifier = FString::Printf(TEXT("%s"), DMXProtocolUniverseSACN->GetIncomingDMXRootLayer().ACNPacketIdentifier);
		Helper->Test->TestTrue(TEXT("Incoming ACNPacketIdentifier should be the same"), ExpectedInentifier.Equals(IncomingInentifier));
	}

	Helper->Test->TestEqual(TEXT("Incoming buffer should be same"), Helper->FixtureValues[0], InputDMXBuffer->GetDMXDataAddress(Helper->FixtureChannels[0] - 1));
	Helper->Test->TestEqual(TEXT("Incoming buffer should be same"), Helper->FixtureValues[1], InputDMXBuffer->GetDMXDataAddress(Helper->FixtureChannels[1] - 1));
	Helper->Test->TestEqual(TEXT("Incoming buffer should be same"), Helper->FixtureValues[2], InputDMXBuffer->GetDMXDataAddress(Helper->FixtureChannels[2] - 1));

	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(DMXPrtotocolSACNBasicFlow_SendDMX2_Part4, TSharedPtr<DMXPrtotocolSACNHelper>, Helper);
bool DMXPrtotocolSACNBasicFlow_SendDMX2_Part4::Update()
{
	FDMXBufferPtr InputDMXBuffer = Helper->Universe->GetInputDMXBuffer();

	IDMXFragmentMap DMXFragmentMap;

	DMXFragmentMap.Add(Helper->FixtureChannels[3], Helper->FixtureValues[3]);
	DMXFragmentMap.Add(Helper->FixtureChannels[4], Helper->FixtureValues[4]);
	DMXFragmentMap.Add(Helper->FixtureChannels[5], Helper->FixtureValues[5]);
	Helper->DMXProtocol->SendDMXFragment(Helper->Universe->GetUniverseID(), DMXFragmentMap);

	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(DMXPrtotocolSACNBasicFlow_CheckDMX2_Part5, TSharedPtr<DMXPrtotocolSACNHelper>, Helper);
bool DMXPrtotocolSACNBasicFlow_CheckDMX2_Part5::Update()
{
	FDMXBufferPtr InputDMXBuffer = Helper->Universe->GetInputDMXBuffer();

	Helper->Test->TestEqual(TEXT("Old value DMX input should be the same"), Helper->FixtureValues[0], InputDMXBuffer->GetDMXDataAddress(Helper->FixtureChannels[0] - 1));
	Helper->Test->TestEqual(TEXT("Incoming buffer should be same"), Helper->FixtureValues[4], InputDMXBuffer->GetDMXDataAddress(Helper->FixtureChannels[4] - 1));
	Helper->Test->TestEqual(TEXT("Incoming buffer should be same"), Helper->FixtureValues[5], InputDMXBuffer->GetDMXDataAddress(Helper->FixtureChannels[5] - 1));

	return true;
}


DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(DMXPrtotocolSACNBasicFlow_SendDiscovery_Part6, TSharedPtr<DMXPrtotocolSACNHelper>, Helper);
bool DMXPrtotocolSACNBasicFlow_SendDiscovery_Part6::Update()
{
	TArray<uint16> Universes;
	for (int32 UniversesIndex = 0; UniversesIndex < DMX_UNIVERSE_SIZE; UniversesIndex++)
	{
		Universes.Add(UniversesIndex);
	}
	Helper->DMXProtocol->SendDiscovery(Universes);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(DMXPrtotocolSACNBasicFlowTest, "VirtualProduction.DMX.SACN.BasicFlow", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool DMXPrtotocolSACNBasicFlowTest::RunTest(const FString& Parameters)
{
	TSharedPtr<DMXPrtotocolSACNHelper> Helper = MakeShared<DMXPrtotocolSACNHelper>(34, this);

	// Wait the universe socket and buffer setup
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.2f));

	ADD_LATENT_AUTOMATION_COMMAND(DMXPrtotocolSACNBasicFlow_CheckUniverse_Part1(Helper));
	ADD_LATENT_AUTOMATION_COMMAND(DMXPrtotocolSACNBasicFlow_SendDMX1_Part2(Helper));

	// Wait after sending first DMX
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.2f));

	ADD_LATENT_AUTOMATION_COMMAND(DMXPrtotocolSACNBasicFlow_CheckDMX1_Part3(Helper));
	ADD_LATENT_AUTOMATION_COMMAND(DMXPrtotocolSACNBasicFlow_SendDMX2_Part4(Helper));

	// Wait after sending second DMX
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.2f));

	ADD_LATENT_AUTOMATION_COMMAND(DMXPrtotocolSACNBasicFlow_CheckDMX2_Part5(Helper));
	ADD_LATENT_AUTOMATION_COMMAND(DMXPrtotocolSACNBasicFlow_SendDiscovery_Part6(Helper));

	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FDMXPrtotocolSACNConsoleCommands_SendDMX1_Part1, TSharedPtr<DMXPrtotocolSACNHelper>, Helper);
bool FDMXPrtotocolSACNConsoleCommands_SendDMX1_Part1::Update()
{
	// DMX.SACN.SendDMX[UniverseID] Channel:Value Channel : Value Channel : Value ...
	FString Command(
		FString::Printf(TEXT("DMX.SACN.SendDMX %d %d:%d %d:%d %d:%d"),
			Helper->Universe->GetUniverseID(),
			Helper->FixtureChannels[0], Helper->FixtureValues[0],
			Helper->FixtureChannels[1], Helper->FixtureValues[1],
			Helper->FixtureChannels[2], Helper->FixtureValues[2]
		));

	GEngine->Exec(GWorld, *Command);

	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FDMXPrtotocolSACNConsoleCommands_CheckDMX1_Part2, TSharedPtr<DMXPrtotocolSACNHelper>, Helper);
bool FDMXPrtotocolSACNConsoleCommands_CheckDMX1_Part2::Update()
{
	FDMXBufferPtr InputDMXBuffer = Helper->Universe->GetInputDMXBuffer();
	Helper->Test->TestEqual(TEXT("Incoming buffer should be same"), Helper->FixtureValues[0], InputDMXBuffer->GetDMXDataAddress(Helper->FixtureChannels[0] - 1));
	Helper->Test->TestEqual(TEXT("Incoming buffer should be same"), Helper->FixtureValues[1], InputDMXBuffer->GetDMXDataAddress(Helper->FixtureChannels[1] - 1));
	Helper->Test->TestEqual(TEXT("Incoming buffer should be same"), Helper->FixtureValues[2], InputDMXBuffer->GetDMXDataAddress(Helper->FixtureChannels[2] - 1));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDMXPrtotocolSACNConsoleCommandsTest, "VirtualProduction.DMX.SACN.ConsoleCommands", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FDMXPrtotocolSACNConsoleCommandsTest::RunTest(const FString& Parameters)
{
	TSharedPtr<DMXPrtotocolSACNHelper> Helper = MakeShared<DMXPrtotocolSACNHelper>(576, this);

	// Wait the universe socket and buffer setup
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.2f));
	ADD_LATENT_AUTOMATION_COMMAND(FDMXPrtotocolSACNConsoleCommands_SendDMX1_Part1(Helper));

	// Wait after sending first DMX
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.2f));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
