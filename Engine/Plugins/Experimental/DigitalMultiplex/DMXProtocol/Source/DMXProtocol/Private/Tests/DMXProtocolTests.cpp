// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Interfaces/IDMXProtocolFactory.h"
#include "DMXProtocolCommon.h"
#include "Dom/JsonObject.h"

#include "Interfaces/IDMXProtocol.h"
#include "DMXProtocolTypes.h"

class FDMXProtocolTest
	: public IDMXProtocol
{
public:
	//~ Begin IDMXProtocolBase implementation
	virtual bool Init() override { return true; }
	virtual bool Shutdown() override { return true; }
	virtual bool Tick(float DeltaTime) override { return true; }
	//~ End IDMXProtocolBase implementation

	//~ Begin IDMXProtocol implementation
	virtual const FName& GetProtocolName() const override { return ProtocolName;  }
	virtual TSharedPtr<FJsonObject> GetSettings() const override { return Settings; }
	virtual TSharedPtr<IDMXProtocolSender> GetSenderInterface() const override { return nullptr; }
	virtual EDMXSendResult SendDMXFragment(uint16 UniverseID, const IDMXFragmentMap& DMXFragment) override { return EDMXSendResult::Success; }
	virtual EDMXSendResult SendDMXFragmentCreate(uint16 InUniverseID, const IDMXFragmentMap& DMXFragment) override { return EDMXSendResult::Success; }
	virtual uint16 GetFinalSendUniverseID(uint16 InUniverseID) const override { return InUniverseID; }
	virtual bool IsEnabled() const override { return true; }
	virtual TSharedPtr<IDMXProtocolUniverse, ESPMode::ThreadSafe> AddUniverse(const FJsonObject& InSettings) override { return nullptr; }
	virtual void CollectUniverses(const TArray<FDMXUniverse>& Universes) override {}
	virtual bool RemoveUniverseById(uint32 InUniverseId) override { return true; }
	virtual void RemoveAllUniverses() override { }
	virtual TSharedPtr<IDMXProtocolUniverse, ESPMode::ThreadSafe> GetUniverseById(uint32 InUniverseId) const override { return nullptr; }
	virtual uint32 GetUniversesNum() const override { return 0; }
	virtual uint16 GetMinUniverseID() const override { return 0; }
	virtual uint16 GetMaxUniverses() const override { return 1; }
	virtual FOnUniverseInputUpdateEvent& GetOnUniverseInputUpdate() override { return OnUniverseInputUpdateEvent; }
	//~ End IDMXProtocol implementation

	//~ Begin IDMXProtocolRDM implementation
	virtual void SendRDMCommand(const TSharedPtr<FJsonObject>& CMD) override {}
	virtual void RDMDiscovery(const TSharedPtr<FJsonObject>& CMD) override {}
	//~ End IDMXProtocol implementation

	//~ Only the factory makes instances
	FDMXProtocolTest() = delete;
	explicit FDMXProtocolTest(FName InProtocolName, FJsonObject& InSettings)
		: ProtocolName(InProtocolName)
	{
		Settings = MakeShared<FJsonObject>(InSettings);
	}

private:
	FName ProtocolName;
	TSharedPtr<FJsonObject> Settings;
	FOnUniverseInputUpdateEvent OnUniverseInputUpdateEvent;
};


/**
 */
class FDMXProtocolFactoryTestFactory : public IDMXProtocolFactory
{
public:
	virtual IDMXProtocolPtr CreateProtocol(const FName& ProtocolName) override
	{
		FJsonObject ProtocolSettings;
		IDMXProtocolPtr ProtocolArtNetPtr = MakeShared<FDMXProtocolTest, ESPMode::ThreadSafe>(ProtocolName, ProtocolSettings);
		if (ProtocolArtNetPtr->IsEnabled())
		{
			if (!ProtocolArtNetPtr->Init())
			{
				UE_LOG_DMXPROTOCOL(Verbose, TEXT("TEST Protocol failed to initialize!"));
				ProtocolArtNetPtr->Shutdown();
				ProtocolArtNetPtr = nullptr;
			}
		}
		else
		{
			UE_LOG_DMXPROTOCOL(Verbose, TEXT("TEST Protocol disabled!"));
			ProtocolArtNetPtr->Shutdown();
			ProtocolArtNetPtr = nullptr;
		}

		return ProtocolArtNetPtr;
	}
};

namespace DMXProtocolTestHelper
{
	static const FName NAME_ArtnetTest = TEXT("ARTNET_TEST");
	static const FName NAME_SACNTest = TEXT("SACN_TEST");

	void GetDMXProtocolNamesForTesting(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands, const FString& PostTestName = TEXT(""))
	{
		TArray< FName > DMXProtocolList;
		DMXProtocolList.AddUnique(NAME_ArtnetTest);
		DMXProtocolList.AddUnique(NAME_SACNTest);

		for (FName& DMXProtocolName : DMXProtocolList)
		{
			FString PostName = FString::Printf(TEXT(".%s"), *PostTestName);
			FString PrettyName = FString::Printf(TEXT("%s%s"),
				*DMXProtocolName.ToString(),
				PostTestName.IsEmpty() ? TEXT("") : *PostName);
			OutBeautifiedNames.Add(PrettyName);
			OutTestCommands.Add(DMXProtocolName.ToString());
		}
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FDMXProtocolFactoryTestCommand, FName, ProtocolName);

bool FDMXProtocolFactoryTestCommand::Update()
{
	//UE_LOG(LogTemp, Warning, TEXT("ProtocolName '%s'"), *ProtocolName.ToString());

	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FDMXProtocolFactoryTest, "VirtualProduction.DMX.Protocol.Factory", (EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter))

void FDMXProtocolFactoryTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	DMXProtocolTestHelper::GetDMXProtocolNamesForTesting(
		OutBeautifiedNames,
		OutTestCommands,
		TEXT("Functional test of the Protocol factory")
	);
}

bool FDMXProtocolFactoryTest::RunTest(const FString& Parameters)
{
	// parameter is the provider we want to use
	FName ProtocolName = FName(*Parameters);
	ADD_LATENT_AUTOMATION_COMMAND(FDMXProtocolFactoryTestCommand(ProtocolName));

	FDMXProtocolModule& DMXProtocolModule = FModuleManager::GetModuleChecked<FDMXProtocolModule>("DMXProtocol");

	// Store the protocol pointer
	IDMXProtocolPtr CachedProtocol = nullptr;

	// Try to register 3 times
	TArray<TUniquePtr<IDMXProtocolFactory>> Factories;
	Factories.Add(MakeUnique<FDMXProtocolFactoryTestFactory>());
	Factories.Add(MakeUnique<FDMXProtocolFactoryTestFactory>());
	Factories.Add(MakeUnique<FDMXProtocolFactoryTestFactory>());
	for (int32 Index = 0; Index < Factories.Num(); Index++)
	{
		// Create and register our singleton factory with the main online subsystem for easy access
		DMXProtocolModule.RegisterProtocol(ProtocolName, Factories[Index].Get());
		if (CachedProtocol == nullptr)
		{
			CachedProtocol = IDMXProtocol::Get(ProtocolName);
		}

		TestTrue(TEXT("Protocol should exists"), IDMXProtocol::Get(ProtocolName).IsValid());
		TestEqual(TEXT("Should return same protocol instance"), CachedProtocol, IDMXProtocol::Get(ProtocolName));
	}
	Factories.Empty();

	// Protocol removal test
	{
		TUniquePtr<IDMXProtocolFactory> Factory = MakeUnique<FDMXProtocolFactoryTestFactory>();
		DMXProtocolModule.RegisterProtocol(ProtocolName, Factory.Get());
		DMXProtocolModule.UnregisterProtocol(ProtocolName);
		TestFalse(TEXT("Protocol should not exists"), IDMXProtocol::Get(ProtocolName).IsValid());
	}


	return true;
}