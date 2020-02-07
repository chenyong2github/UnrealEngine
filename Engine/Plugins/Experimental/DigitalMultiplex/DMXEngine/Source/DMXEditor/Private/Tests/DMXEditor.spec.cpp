// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Factories/DMXEditorFactoryNew.h"
#include "Library/DMXLibrary.h"
#include "Interfaces/IDMXProtocol.h"
#include "Interfaces/IDMXProtocolUniverse.h"
#include "DMXProtocolArtNetModule.h"
#include "DMXProtocolSACNModule.h"
#include "Library/DMXLibrary.h"
#include "Library/DMXEntityFader.h"
#include "Library/DMXEntityController.h"
#include "DMXEditor.h"
#include "DMXProtocolTypes.h"
#include "DMXEditorUtils.h"

#include "Widgets/OutputFader/SDMXOutputFaderList.h"
#include "Widgets/OutputFader/SDMXFader.h"
#include "Widgets/SDMXInputConsole.h"
#include "Widgets/SDMXInputInfoSelecter.h"
#include "Widgets/SDMXInputInfo.h"
#include "Widgets/Common/SSpinBoxVertical.h"

#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSpinBox.h"

#include "Utils.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "AssetData.h"
#include "Editor.h"

#include "AssetRegistryModule.h"
#include "AssetRegistryState.h"
#include "AssetData.h"

#if WITH_DEV_AUTOMATION_TESTS

struct FDMXEditorHelper
{
	static void ResetUniverses()
	{
		const TMap<FName, TSharedPtr<IDMXProtocol>>& Protocols = IDMXProtocol::GetProtocols();
		for (TMap<FName, TSharedPtr<IDMXProtocol>>::TConstIterator ProtocolIt = Protocols.CreateConstIterator(); ProtocolIt; ++ProtocolIt)
		{
			if (TSharedPtr<IDMXProtocol> ProtocolPtr = ProtocolIt->Value)
			{
				ProtocolPtr->RemoveAllUniverses();
			}
		}
	}

	FDMXEditorHelper()
	{
		DMXEditor = MakeShared<FDMXEditor>();
		UPackage* Package = GetTransientPackage();
		UDMXEditorFactoryNew* Factory = NewObject<UDMXEditorFactoryNew>(Package, MakeUniqueObjectName(GetTransientPackage(), UDMXEditorFactoryNew::StaticClass()));
		const FName NewLibraryName = MakeUniqueObjectName(Package, UDMXLibrary::StaticClass(), *FString::Printf(TEXT("%d_LIB"), FMath::RandRange(0, 1000)));

		DMXLibrary = ImportObject<UDMXLibrary>(Package, *NewLibraryName.ToString(), RF_Public | RF_Standalone | RF_Transient, *NewLibraryName.ToString(), nullptr, Factory);

		FName NewTemplateName = MakeUniqueObjectName(DMXLibrary, UDMXEntityFader::StaticClass());
		NewFaderTemplate = FDMXEditorUtils::CreateFaderTemplate(DMXLibrary);
		FaderEntity = Cast<UDMXEntityFader>(DMXLibrary->GetOrCreateEntityObject(TEXT(""), UDMXEntityFader::StaticClass()));
		FaderEntity->SetName(NewFaderTemplate->GetDisplayName());
	}

	TSharedPtr<FDMXEditor> DMXEditor;
	UDMXLibrary* DMXLibrary;
	UDMXEntityFader* FaderEntity;
	UDMXEntityFader* NewFaderTemplate;
};


static const uint8 TestChannelValue = 155;
static const uint8 ExistingUniverse = 1;
static const uint8 NonExistingUniverse = 12;
static const uint8 ChannelValue = 50;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDMXEditorFadersArtNetTest, "VirtualProduction.DMX.Editor.Faders.ArtNet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter);

bool FDMXEditorFadersArtNetTest::RunTest(const FString& Parameters)
{
	FDMXEditorHelper::ResetUniverses();

	// Create Helper
	TSharedPtr<FDMXEditorHelper> Helper = MakeShared<FDMXEditorHelper>();

	TSharedPtr<IDMXProtocol> DMXProtocol = IDMXProtocol::Get(FDMXProtocolArtNetModule::NAME_Artnet);

	// Add first universe
	{
		FDMXUniverse DMXUniverse;
		DMXUniverse.Channel = 10;
		DMXUniverse.UniverseNumber = 1;
		DMXUniverse.DMXProtocolDirectionality = EDMXProtocolDirectionality::EOutput;
		Helper->FaderEntity->Universes.Add(DMXUniverse);
	}

	// Add second universe
	{
		FDMXUniverse DMXUniverse;
		DMXUniverse.Channel = 20;
		DMXUniverse.UniverseNumber = 2;
		DMXUniverse.DMXProtocolDirectionality = EDMXProtocolDirectionality::EOutput;
		Helper->FaderEntity->Universes.Add(DMXUniverse);
	}
	Helper->FaderEntity->DeviceProtocol = FDMXProtocolName(FDMXProtocolArtNetModule::NAME_Artnet);
	Helper->FaderEntity->PostEditChange();

	Helper->DMXEditor->InitEditor(EToolkitMode::Standalone, nullptr, Helper->DMXLibrary);

	TSharedPtr<SDMXOutputFaderList> DMXOutputFaderList = SNew(SDMXOutputFaderList)
		.DMXEditor(Helper->DMXEditor)
		.FaderTemplate(Helper->NewFaderTemplate);

	TestTrue(TEXT("Should be only one fader now"), DMXOutputFaderList->GetFaderWidgets().Num() == 1);

	DMXOutputFaderList->GetFaderWidgets()[0]->GetFaderBoxVertical()->SetValue(ChannelValue);

	// Check the data inside universe buffers
	TArray<TSharedPtr<IDMXProtocolUniverse, ESPMode::ThreadSafe>> Universes;
	Universes.Add(DMXProtocol->GetUniverseById(Helper->FaderEntity->Universes[0].UniverseNumber));
	Universes.Add(DMXProtocol->GetUniverseById(Helper->FaderEntity->Universes[1].UniverseNumber));

	TestTrue(TEXT("Universe should be exists"),
		Universes[0].IsValid());
	TestTrue(TEXT("Universe should be exists"),
		Universes[1].IsValid());

	TestEqual(TEXT("Buffer value should be same"),
		Universes[0]->GetOutputDMXBuffer()->GetDMXData()[Helper->FaderEntity->Universes[0].Channel], ChannelValue);
	TestEqual(TEXT("Buffer value should be same"),
		Universes[1]->GetOutputDMXBuffer()->GetDMXData()[Helper->FaderEntity->Universes[1].Channel], ChannelValue);

	Helper->DMXEditor->CloseWindow();

	FDMXEditorHelper::ResetUniverses();

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDMXEditorFadersSACNTest, "VirtualProduction.DMX.Editor.Faders.sACN",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter);

bool FDMXEditorFadersSACNTest::RunTest(const FString& Parameters)
{
	FDMXEditorHelper::ResetUniverses();

	// Create Helper
	TSharedPtr<FDMXEditorHelper> Helper = MakeShared<FDMXEditorHelper>();

	TSharedPtr<IDMXProtocol> DMXProtocol = IDMXProtocol::Get(FDMXProtocolSACNModule::NAME_SACN);

	// Add first universe
	{
		FDMXUniverse DMXUniverse;
		DMXUniverse.Channel = 10;
		DMXUniverse.UniverseNumber = 31;
		DMXUniverse.DMXProtocolDirectionality = EDMXProtocolDirectionality::EOutput;
		Helper->FaderEntity->Universes.Add(DMXUniverse);
	}

	// Add second universe
	{
		FDMXUniverse DMXUniverse;
		DMXUniverse.Channel = 20;
		DMXUniverse.UniverseNumber = 51;
		DMXUniverse.DMXProtocolDirectionality = EDMXProtocolDirectionality::EOutput;
		Helper->FaderEntity->Universes.Add(DMXUniverse);
	}

	Helper->FaderEntity->DeviceProtocol = FDMXProtocolName(FDMXProtocolSACNModule::NAME_SACN);
	Helper->FaderEntity->PostEditChange();

	Helper->DMXEditor->InitEditor(EToolkitMode::Standalone, nullptr, Helper->DMXLibrary);

	TSharedPtr<SDMXOutputFaderList> DMXOutputFaderList = SNew(SDMXOutputFaderList)
		.DMXEditor(Helper->DMXEditor)
		.FaderTemplate(Helper->NewFaderTemplate);

	TestTrue(TEXT("Should be only one fader now"), DMXOutputFaderList->GetFaderWidgets().Num() == 1);

	DMXOutputFaderList->GetFaderWidgets()[0]->GetFaderBoxVertical()->SetValue(ChannelValue);

	// Check the data inside universe buffers
	TArray<TSharedPtr<IDMXProtocolUniverse, ESPMode::ThreadSafe>> Universes;
	Universes.Add(DMXProtocol->GetUniverseById(Helper->FaderEntity->Universes[0].UniverseNumber));
	Universes.Add(DMXProtocol->GetUniverseById(Helper->FaderEntity->Universes[1].UniverseNumber));

	TestTrue(TEXT("Universe should be exists"),
		Universes[0].IsValid());
	TestTrue(TEXT("Universe should be exists"),
		Universes[1].IsValid());

	TestEqual(TEXT("Buffer value should be same"),
		Universes[0]->GetOutputDMXBuffer()->GetDMXData()[Helper->FaderEntity->Universes[0].Channel], ChannelValue);
	TestEqual(TEXT("Buffer value should be same"),
		Universes[1]->GetOutputDMXBuffer()->GetDMXData()[Helper->FaderEntity->Universes[1].Channel], ChannelValue);

	Helper->DMXEditor->CloseWindow();

	FDMXEditorHelper::ResetUniverses();

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDMXEditorControllersEmptyTest, "VirtualProduction.DMX.Editor.Controllers.Empty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter);

bool FDMXEditorControllersEmptyTest::RunTest(const FString& Parameters)
{
	FDMXEditorHelper::ResetUniverses();

	FDMXEditorHelper Helpers[] =
	{
		FDMXEditorHelper(),
		FDMXEditorHelper()
	};

	Helpers[0].DMXEditor->InitEditor(EToolkitMode::Standalone, nullptr, Helpers[0].DMXLibrary);
	Helpers[1].DMXEditor->InitEditor(EToolkitMode::Standalone, nullptr, Helpers[1].DMXLibrary);

	const TMap<FName, TSharedPtr<IDMXProtocol>>& Protocols = IDMXProtocol::GetProtocols();
	for (TMap<FName, TSharedPtr<IDMXProtocol>>::TConstIterator ProtocolIt = Protocols.CreateConstIterator(); ProtocolIt; ++ProtocolIt)
	{
		if (TSharedPtr<IDMXProtocol> ProtocolPtr = ProtocolIt->Value)
		{
			TestEqual(TEXT("Verify number of universes Is 0"), 0, ProtocolPtr->GetUniversesNum());
		}
	}

	// Check our files
	static const FName AssetRegistryName(TEXT("AssetRegistry"));
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(AssetRegistryName);
	TArray<FAssetData> Items;
	AssetRegistryModule.Get().GetAssetsByClass(UDMXLibrary::StaticClass()->GetFName(), Items);

	uint32 OurAssetsCount = 0;
	if (Items.Num() > 0)
	{
		for (const FAssetData& Item : Items)
		{
			UObject* Asset = FindObject<UObject>(nullptr, *Item.ObjectPath.ToString());
			if (Asset != nullptr)
			{
				if (Asset == Helpers[0].DMXLibrary || Asset == Helpers[1].DMXLibrary)
				{
					OurAssetsCount++;
				}
			}
		}
	}
	TestEqual(TEXT("Verify number of assets Is 0"), 0, OurAssetsCount);

	Helpers[0].DMXEditor->CloseWindow();
	Helpers[1].DMXEditor->CloseWindow();

	FDMXEditorHelper::ResetUniverses();

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDMXEditorControllersNonEmptyTest, "VirtualProduction.DMX.Editor.Controllers.NonEmpty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter);

bool FDMXEditorControllersNonEmptyTest::RunTest(const FString& Parameters)
{
	FDMXEditorHelper::ResetUniverses();

	FDMXEditorHelper Helpers[] =
	{
		FDMXEditorHelper(),
		FDMXEditorHelper()
	};


	// Add one Universe for editor 0
	{
		FName NewLibraryName = MakeUniqueObjectName(GetTransientPackage(), UDMXLibrary::StaticClass());
		UDMXEntityController* Controller = Cast<UDMXEntityController>(Helpers[0].DMXLibrary->GetOrCreateEntityObject(NewLibraryName.ToString(), UDMXEntityController::StaticClass()));
		FDMXUniverse Universe;
		Universe.UniverseNumber = 2;
		Controller->DeviceProtocol = FDMXProtocolName(FDMXProtocolArtNetModule::NAME_Artnet);
		Controller->Universes.Add(Universe);
		Controller->PostEditChange();
	}

	// Add same Universe for editor 0
	{
		FName NewLibraryName = MakeUniqueObjectName(GetTransientPackage(), UDMXLibrary::StaticClass());
		UDMXEntityController* Controller = Cast<UDMXEntityController>(Helpers[0].DMXLibrary->GetOrCreateEntityObject(NewLibraryName.ToString(), UDMXEntityController::StaticClass()));
		FDMXUniverse Universe;
		Universe.UniverseNumber = 2;
		Controller->DeviceProtocol = FDMXProtocolName(FDMXProtocolArtNetModule::NAME_Artnet);
		Controller->Universes.Add(Universe);
		Controller->PostEditChange();
	}

	// Add Universe for editor 1
	{
		FName NewLibraryName = MakeUniqueObjectName(GetTransientPackage(), UDMXLibrary::StaticClass());
		UDMXEntityController* Controller = Cast<UDMXEntityController>(Helpers[1].DMXLibrary->GetOrCreateEntityObject(NewLibraryName.ToString(), UDMXEntityController::StaticClass()));
		FDMXUniverse Universe;
		Universe.UniverseNumber = 675;
		Controller->DeviceProtocol = FDMXProtocolName(FDMXProtocolSACNModule::NAME_SACN);
		Controller->Universes.Add(Universe);
		Controller->PostEditChange();
	}

	// Add same Universe for editor 1
	{
		FName NewLibraryName = MakeUniqueObjectName(GetTransientPackage(), UDMXLibrary::StaticClass());
		UDMXEntityController* Controller = Cast<UDMXEntityController>(Helpers[1].DMXLibrary->GetOrCreateEntityObject(NewLibraryName.ToString(), UDMXEntityController::StaticClass()));
		FDMXUniverse Universe;
		Universe.UniverseNumber = 675;
		Controller->DeviceProtocol = FDMXProtocolName(FDMXProtocolSACNModule::NAME_SACN);
		Controller->Universes.Add(Universe);
		Controller->PostEditChange();
	}

	Helpers[0].DMXEditor->InitEditor(EToolkitMode::Standalone, nullptr, Helpers[0].DMXLibrary);
	Helpers[1].DMXEditor->InitEditor(EToolkitMode::Standalone, nullptr, Helpers[1].DMXLibrary);

	uint32 NumUniverses = 0;
	const TMap<FName, TSharedPtr<IDMXProtocol>>& Protocols = IDMXProtocol::GetProtocols();
	for (TMap<FName, TSharedPtr<IDMXProtocol>>::TConstIterator ProtocolIt = Protocols.CreateConstIterator(); ProtocolIt; ++ProtocolIt)
	{
		if (TSharedPtr<IDMXProtocol> ProtocolPtr = ProtocolIt->Value)
		{
			NumUniverses += ProtocolPtr->GetUniversesNum();
		}
	}

	TestEqual(TEXT("Verify number of universes Is 2"), 2, NumUniverses);

	Helpers[0].DMXEditor->CloseWindow();
	Helpers[1].DMXEditor->CloseWindow();

	FDMXEditorHelper::ResetUniverses();

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDMXEditorInputConsoleArtNetExistingTest, "VirtualProduction.DMX.Editor.InputConsole.ArtNet.Existing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter);

bool FDMXEditorInputConsoleArtNetExistingTest::RunTest(const FString& Parameters)
{
	// Reset Universes
	FDMXEditorHelper::ResetUniverses();

	// Create Helper
	TSharedPtr<FDMXEditorHelper> Helper = MakeShared<FDMXEditorHelper>();

	// 2. Create Universe
	FName NewLibraryName = MakeUniqueObjectName(GetTransientPackage(), UDMXLibrary::StaticClass());
	UDMXEntityController* Controller = Cast<UDMXEntityController>(Helper->DMXLibrary->GetOrCreateEntityObject(NewLibraryName.ToString(), UDMXEntityController::StaticClass()));
	FDMXUniverse Universe;
	Universe.UniverseNumber = ExistingUniverse;
	Controller->DeviceProtocol = FDMXProtocolName(FDMXProtocolArtNetModule::NAME_Artnet);
	Controller->Universes.Add(Universe);
	Controller->PostEditChange();

	// 3. Create editor
	Helper->DMXEditor->InitEditor(EToolkitMode::Standalone, nullptr, Helper->DMXLibrary);

	// 4. Set Input Tab
	TSharedRef<SDMXInputConsole> InputConsole = Helper->DMXEditor->GetInputConsoleTab();
	const TSharedRef<SDMXInputInfo>& InputInfo = InputConsole->GetInputInfo();
	const TSharedRef<SDMXInputInfoSelecter>& InfoSelecter = InputConsole->GetInputInfoSelecter();
	const TSharedRef<SSpinBox<uint16>>& UniverseField = InfoSelecter->GetUniverseField();

	InfoSelecter->SetProtocol(FDMXProtocolArtNetModule::NAME_Artnet);
	UniverseField->SetValue(ExistingUniverse);

	// 5. Send DMX
	TSharedPtr<IDMXProtocol> DMXProtocol = IDMXProtocol::Get(FDMXProtocolArtNetModule::NAME_Artnet);
	IDMXFragmentMap FragmentMap;
	FragmentMap.Add(0, TestChannelValue);
	DMXProtocol->SendDMXFragment(ExistingUniverse, FragmentMap);

	// Check the input console values
	AddCommand(new FDelayedFunctionLatentCommand([=] {
		TSharedRef<SDMXInputConsole> InputConsole = Helper->DMXEditor->GetInputConsoleTab();
		const TSharedPtr<SDMXInputInfo>& InputInfo = InputConsole->GetInputInfo();

		// 6. Check input console
		const TArray<uint8>& ChannelsValues = InputInfo->GetChannelsValues();
		TestEqual(TEXT("Verify ChannelsValue"), TestChannelValue, ChannelsValues[0]);

		FDMXEditorHelper::ResetUniverses();

		// 7. Close the Editor
		Helper->DMXEditor->CloseWindow();

	}, 0.2f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDMXEditorInputConsoleArtNetNonExistingTest, "VirtualProduction.DMX.Editor.InputConsole.ArtNet.NonExisting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter);

bool FDMXEditorInputConsoleArtNetNonExistingTest::RunTest(const FString& Parameters)
{
	// Reset Universes
	FDMXEditorHelper::ResetUniverses();

	// Create Helper
	TSharedPtr<FDMXEditorHelper> Helper = MakeShared<FDMXEditorHelper>();

	// 2. Create Universe
	FName NewLibraryName = MakeUniqueObjectName(GetTransientPackage(), UDMXLibrary::StaticClass());
	UDMXEntityController* Controller = Cast<UDMXEntityController>(Helper->DMXLibrary->GetOrCreateEntityObject(NewLibraryName.ToString(), UDMXEntityController::StaticClass()));
	FDMXUniverse Universe;
	Universe.UniverseNumber = ExistingUniverse;
	Controller->DeviceProtocol = FDMXProtocolName(FDMXProtocolArtNetModule::NAME_Artnet);
	Controller->Universes.Add(Universe);
	Controller->PostEditChange();

	// 3. Create editor
	Helper->DMXEditor->InitEditor(EToolkitMode::Standalone, nullptr, Helper->DMXLibrary);

	// 4. Set Input Tab
	TSharedRef<SDMXInputConsole> InputConsole = Helper->DMXEditor->GetInputConsoleTab();
	const TSharedRef<SDMXInputInfo>& InputInfo = InputConsole->GetInputInfo();
	const TSharedRef<SDMXInputInfoSelecter>& InfoSelecter = InputConsole->GetInputInfoSelecter();
	const TSharedRef<SSpinBox<uint16>>& UniverseField = InfoSelecter->GetUniverseField();

	InfoSelecter->SetProtocol(FDMXProtocolArtNetModule::NAME_Artnet);
	UniverseField->SetValue(NonExistingUniverse);

	// 5. Send DMX
	TSharedPtr<IDMXProtocol> DMXProtocol = IDMXProtocol::Get(FDMXProtocolArtNetModule::NAME_Artnet);
	IDMXFragmentMap FragmentMap;
	FragmentMap.Add(0, TestChannelValue);
	DMXProtocol->SendDMXFragment(ExistingUniverse, FragmentMap);

	// Check the input console values
	AddCommand(new FDelayedFunctionLatentCommand([=] {
		TSharedRef<SDMXInputConsole> InputConsole = Helper->DMXEditor->GetInputConsoleTab();
		const TSharedPtr<SDMXInputInfo>& InputInfo = InputConsole->GetInputInfo();

		// 6. Check input console
		const TArray<uint8>& ChannelsValues = InputInfo->GetChannelsValues();
		TestNotEqual(TEXT("Verify ChannelsValue"), TestChannelValue, ChannelsValues[0]);

		FDMXEditorHelper::ResetUniverses();

		// 7. Close the Editor
		Helper->DMXEditor->CloseWindow();

	}, 0.2f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDMXEditorInputConsoleSACNExistingTest, "VirtualProduction.DMX.Editor.InputConsole.SACN.Existing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter);

bool FDMXEditorInputConsoleSACNExistingTest::RunTest(const FString& Parameters)
{
	// Reset Universes
	FDMXEditorHelper::ResetUniverses();

	// Create Helper
	TSharedPtr<FDMXEditorHelper> Helper = MakeShared<FDMXEditorHelper>();

	// 2. Create Universe
	FName NewLibraryName = MakeUniqueObjectName(GetTransientPackage(), UDMXLibrary::StaticClass());
	UDMXEntityController* Controller = Cast<UDMXEntityController>(Helper->DMXLibrary->GetOrCreateEntityObject(NewLibraryName.ToString(), UDMXEntityController::StaticClass()));
	FDMXUniverse Universe;
	Universe.UniverseNumber = ExistingUniverse;
	Controller->DeviceProtocol = FDMXProtocolName(FDMXProtocolSACNModule::NAME_SACN);
	Controller->Universes.Add(Universe);
	Controller->PostEditChange();

	// 3. Create editor
	Helper->DMXEditor->InitEditor(EToolkitMode::Standalone, nullptr, Helper->DMXLibrary);

	// 4. Set Input Tab
	TSharedRef<SDMXInputConsole> InputConsole = Helper->DMXEditor->GetInputConsoleTab();
	const TSharedRef<SDMXInputInfo>& InputInfo = InputConsole->GetInputInfo();
	const TSharedRef<SDMXInputInfoSelecter>& InfoSelecter = InputConsole->GetInputInfoSelecter();
	const TSharedRef<SSpinBox<uint16>>& UniverseField = InfoSelecter->GetUniverseField();

	InfoSelecter->SetProtocol(FDMXProtocolSACNModule::NAME_SACN);
	UniverseField->SetValue(ExistingUniverse);

	// 5. Send DMX
	TSharedPtr<IDMXProtocol> DMXProtocol = IDMXProtocol::Get(FDMXProtocolSACNModule::NAME_SACN);
	IDMXFragmentMap FragmentMap;
	FragmentMap.Add(0, TestChannelValue);
	DMXProtocol->SendDMXFragment(ExistingUniverse, FragmentMap);

	// Check the input console values
	AddCommand(new FDelayedFunctionLatentCommand([=] {
		TSharedRef<SDMXInputConsole> InputConsole = Helper->DMXEditor->GetInputConsoleTab();
		const TSharedPtr<SDMXInputInfo>& InputInfo = InputConsole->GetInputInfo();

		// 6. Check input console
		const TArray<uint8>& ChannelsValues = InputInfo->GetChannelsValues();
		TestEqual(TEXT("Verify ChannelsValue"), TestChannelValue, ChannelsValues[0]);

		FDMXEditorHelper::ResetUniverses();

		// 7. Close the Editor
		Helper->DMXEditor->CloseWindow();

	}, 0.2f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDMXEditorInputConsoleSACNNonExistingTest, "VirtualProduction.DMX.Editor.InputConsole.SACN.NonExisting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter);

bool FDMXEditorInputConsoleSACNNonExistingTest::RunTest(const FString& Parameters)
{
	// Reset Universes
	FDMXEditorHelper::ResetUniverses();

	// Create Helper
	TSharedPtr<FDMXEditorHelper> Helper = MakeShared<FDMXEditorHelper>();

	// 2. Create Universe
	FName NewLibraryName = MakeUniqueObjectName(GetTransientPackage(), UDMXLibrary::StaticClass());
	UDMXEntityController* Controller = Cast<UDMXEntityController>(Helper->DMXLibrary->GetOrCreateEntityObject(NewLibraryName.ToString(), UDMXEntityController::StaticClass()));
	FDMXUniverse Universe;
	Universe.UniverseNumber = ExistingUniverse;
	Controller->DeviceProtocol = FDMXProtocolName(FDMXProtocolSACNModule::NAME_SACN);
	Controller->Universes.Add(Universe);
	Controller->PostEditChange();

	// 3. Create editor
	Helper->DMXEditor->InitEditor(EToolkitMode::Standalone, nullptr, Helper->DMXLibrary);

	// 4. Set Input Tab
	TSharedRef<SDMXInputConsole> InputConsole = Helper->DMXEditor->GetInputConsoleTab();
	const TSharedRef<SDMXInputInfo>& InputInfo = InputConsole->GetInputInfo();
	const TSharedRef<SDMXInputInfoSelecter>& InfoSelecter = InputConsole->GetInputInfoSelecter();
	const TSharedRef<SSpinBox<uint16>>& UniverseField = InfoSelecter->GetUniverseField();

	InfoSelecter->SetProtocol(FDMXProtocolSACNModule::NAME_SACN);
	UniverseField->SetValue(NonExistingUniverse);

	// 5. Send DMX
	TSharedPtr<IDMXProtocol> DMXProtocol = IDMXProtocol::Get(FDMXProtocolSACNModule::NAME_SACN);
	IDMXFragmentMap FragmentMap;
	FragmentMap.Add(0, TestChannelValue);
	DMXProtocol->SendDMXFragment(ExistingUniverse, FragmentMap);

	// Check the input console values
	AddCommand(new FDelayedFunctionLatentCommand([=] {
		TSharedRef<SDMXInputConsole> InputConsole = Helper->DMXEditor->GetInputConsoleTab();
		const TSharedPtr<SDMXInputInfo>& InputInfo = InputConsole->GetInputInfo();

		// 6. Check input console
		const TArray<uint8>& ChannelsValues = InputInfo->GetChannelsValues();
		TestNotEqual(TEXT("Verify ChannelsValue"), TestChannelValue, ChannelsValues[0]);

		FDMXEditorHelper::ResetUniverses();

		// 7. Close the Editor
		Helper->DMXEditor->CloseWindow();

	}, 0.2f));

	return true;
}

#endif