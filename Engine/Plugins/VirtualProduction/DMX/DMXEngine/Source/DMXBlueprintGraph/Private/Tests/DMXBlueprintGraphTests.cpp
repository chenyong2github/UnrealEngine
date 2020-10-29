// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Modules/ModuleManager.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "AssetData.h"
#include "ObjectTools.h"
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#include "ScopedTransaction.h"
#include "PackageTools.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "Factories/BlueprintFactory.h"
#include "BlueprintEditorModes.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphSchema_K2_Actions.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "K2Node_Event.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"

// Automation
#include "AssetRegistryModule.h"
#include "Tests/AutomationTestSettings.h"
#include "Tests/AutomationEditorCommon.h"
#include "Tests/AutomationEditorPromotionCommon.h"
#include "Subsystems/AssetEditorSubsystem.h"

// DMX
#include "Library/DMXLibrary.h"
#include "Library/DMXEntityController.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXEntityFixtureType.h"
#include "Factories/DMXEditorFactoryNew.h"
#include "Interfaces/IDMXProtocol.h"
#include "DMXProtocolSACNModule.h"
#include "DMXEditor.h"
#include "Commands/DMXEditorCommands.h"
#include "K2Node_GetDMXFixturePatch.h"
#include "K2Node_GetDMXAttributeValues.h"

#if WITH_DEV_AUTOMATION_TESTS

#define LOCTEXT_NAMESPACE "FDMXBlueprintGraphTest"

DEFINE_LOG_CATEGORY_STATIC(LogDMXBlueprintGraphTests, Log, All);

// TODO: Update this test? Currently references out-of-date pin names which crashes.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDMXBlueprintGraphTest, "VirtualProduction.DMX.BlueprintGraph", EAutomationTestFlags::Disabled | EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter);

struct FDMXBlueprintGraphTestHelper
{
#define ADD_TEST_STAGE(FuncName,StageName) \
	TestStages.Add(&FDMXBlueprintGraphTestHelper::FuncName); \
	StageNames.Add(StageName); 

	typedef bool(FDMXBlueprintGraphTestHelper::* TestStageFn)();


public:
	FDMXBlueprintGraphTestHelper()
		: Test(nullptr)
		, CurrentStage(0)
		, BlueprintPackage(nullptr)
		, BlueprintObject(nullptr)
		, DMXPackage(nullptr)
		, DMXLibraryObject(nullptr)
		, SACNControllerObject(nullptr)
		, FixtureTypeObject1(nullptr)
		, FixtureTypeObject2(nullptr)
		, FixturePatchObject1(nullptr)
		, FixturePatchObject2(nullptr)
		, PostBeginPlayEventNode(nullptr)
		, K2Node_GetDMXFixturePatch1(nullptr)
		, K2Node_GetDMXFixturePatch2(nullptr)
		, K2Node_GetDMXAttributeValues1(nullptr)
		, K2Node_GetDMXAttributeValues2(nullptr)
		, SetMemberVariableNode1(nullptr)
		, SetMemberVariableNode2(nullptr)
	{
		FMemory::Memzero(this, sizeof(FDMXBlueprintGraphTestHelper));

		ADD_TEST_STAGE(Cleanup, TEXT("Pre-start cleanup"));
		ADD_TEST_STAGE(DMXLibrary_CreateNew_1, TEXT("Create a new Library"));
		ADD_TEST_STAGE(DMXLibrary_AddDMXControllers_2, TEXT("Add dmx controllers"));
		ADD_TEST_STAGE(DMXLibrary_AddFixtureType_3, TEXT("Add fixture type"));
		ADD_TEST_STAGE(DMXLibrary_AddFixturePath_4, TEXT("Add fixture patch"));
		ADD_TEST_STAGE(DMXLibrary_AddFixtureTypeToFixturePatch_5, TEXT("Add fixture type to fixture patch"));
		ADD_TEST_STAGE(Blueprint_CreateNewBlueprint_6, TEXT("Create a new Blueprint"));
		ADD_TEST_STAGE(Blueprint_AddGetDMXFunctionValuesNode_7, TEXT("Add get DMX function values 1 node"));
		ADD_TEST_STAGE(Blueprint_AddGetDMXFunctionValuesNode_8, TEXT("Add get DMX function values 2 node"));
		ADD_TEST_STAGE(DMXLibrary_ChangeFixtureTypeFunctionDataValueType_9, TEXT("Change fixture type function data value type"));
		ADD_TEST_STAGE(Cleanup, TEXT("Post cleanup"));
	}


	bool Update()
	{
		Test->PushContext(StageNames[CurrentStage]);
		bool bStageComplete = (this->*TestStages[CurrentStage])();
		Test->PopContext();

		if (bStageComplete)
		{
			CurrentStage++;
		}

		return CurrentStage >= TestStages.Num();
	}

private:
	bool Cleanup()
	{
		//Make sure no editors are open
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseAllAssetEditors();

		// Load the asset registry module
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

		// Form a filter from the paths
		FARFilter Filter;
		Filter.bRecursivePaths = true;
		new (Filter.PackagePaths) FName(*GetGamePath());

		// Query for a list of assets in the selected paths
		TArray<FAssetData> AssetList;
		AssetRegistry.GetAssets(Filter, AssetList);

		// Clear and try to delete all assets
		TArray<UObject*> ObjectsToDelete;
		for (int32 AssetIdx = 0; AssetIdx < AssetList.Num(); ++AssetIdx)
		{
			Test->AddInfo(*FString::Printf(TEXT("Removing asset: %s"), *AssetList[AssetIdx].AssetName.ToString()));
			if (AssetList[AssetIdx].IsAssetLoaded())
			{
				UObject* LoadedAsset = AssetList[AssetIdx].GetAsset();

				ObjectsToDelete.Add(LoadedAsset);
			}
		}

		if (ObjectsToDelete.Num())
		{
			ObjectTools::DeleteObjects(ObjectsToDelete, true);

			Test->AddInfo(*FString::Printf(TEXT("ObjectsToDelete: %d"), ObjectsToDelete.Num()));
		}

		Test->AddInfo(*FString::Printf(TEXT("Clearing Path: %s"), *GetGamePath()));
		AssetRegistry.RemovePath(GetGamePath());

		// Remove the directory
		bool bEnsureExists = false;
		bool bDeleteEntireTree = true;
		FString&& PackageDirectory = GetPackageDirectory();
		IFileManager::Get().DeleteDirectory(*PackageDirectory, bEnsureExists, bDeleteEntireTree);
		Test->AddInfo(*FString::Printf(TEXT("Deleting Folder: %s"), *PackageDirectory));

		return true;
	}
	bool DMXLibrary_CreateNew_1()
	{
		DMXEditor = MakeShared<FDMXEditor>();

		UDMXEditorFactoryNew* Factory = NewObject<UDMXEditorFactoryNew>();

		const FString PackageName = GetGamePath() + TEXT("/") + DMXLibraryNameString;
		DMXPackage = CreatePackage(*PackageName);
		EObjectFlags Flags = RF_Public | RF_Standalone;

		UObject* ExistingDMXLibrary = FindObject<UBlueprint>(DMXPackage, *DMXLibraryNameString);
		Test->TestNull(TEXT("DMXLibrary asset does not already exist (delete DMXLibrary and restart editor)"), ExistingDMXLibrary);
		// Exit early since test will not be valid with pre-existing assets
		if (ExistingDMXLibrary)
		{
			return true;
		}

		DMXLibraryObject = Cast<UDMXLibrary>(Factory->FactoryCreateNew(UDMXLibrary::StaticClass(), DMXPackage, FName(*DMXLibraryNameString), Flags, NULL, GWarn));
		Test->TestNotNull(TEXT("Created DMX library"), DMXLibraryObject);
		if (DMXLibraryObject)
		{
			// Update asset registry and mark package dirty
			FAssetRegistryModule::AssetCreated(DMXLibraryObject);
			DMXPackage->MarkPackageDirty();

			Test->AddInfo(TEXT("Opening the DMXLibrary editor for the first time"));

			DMXEditor->InitEditor(EToolkitMode::Standalone, nullptr, DMXLibraryObject);
		}
		return true;
	}

	bool DMXLibrary_AddDMXControllers_2()
	{
		if (!(DMXLibraryObject && DMXEditor.IsValid()))
		{
			return false;
		}

		// Add SACN controller
		FName NewTemplateName = MakeUniqueObjectName(DMXLibraryObject, UDMXEntityController::StaticClass());
		SACNControllerObject = Cast<UDMXEntityController>(DMXLibraryObject->GetOrCreateEntityObject(TEXT(""), UDMXEntityController::StaticClass()));
		SACNControllerObject->SetName(NewTemplateName.ToString());

		FDMXProtocolName ProtocolName(IDMXProtocol::Get(FDMXProtocolSACNModule::NAME_SACN));
		SACNControllerObject->DeviceProtocol = ProtocolName;

		SACNControllerObject->PostEditChange();

		DMXEditor->InvokeEditorTabFromEntityType(UDMXEntityController::StaticClass());
		DMXEditor->GetOnSetupNewEntity().Broadcast(SACNControllerObject);

		Test->AddInfo(TEXT("Add SACN controller"));

		return true;
	}

	bool DMXLibrary_AddFixtureType_3()
	{
		if (!(DMXLibraryObject && DMXEditor.IsValid()))
		{
			return false;
		}

		// Add fixture type 1
		{
			FName NewTemplateName = MakeUniqueObjectName(DMXLibraryObject, UDMXEntityFixtureType::StaticClass());
			FixtureTypeObject1 = Cast<UDMXEntityFixtureType>(DMXLibraryObject->GetOrCreateEntityObject(TEXT(""), UDMXEntityFixtureType::StaticClass()));
			FixtureTypeObject1->SetName(NewTemplateName.ToString());

			// Add functions
			ActiveModeFunction1.FunctionName = "ActiveModeFunction1";
			ActiveModeFunction1.Channel = 1;
			ActiveModeFunction1.DataType = EDMXFixtureSignalFormat::E8Bit;

			ActiveModeFunction2.FunctionName = "ActiveModeFunction2";
			ActiveModeFunction2.Channel = 2;
			ActiveModeFunction2.DataType = EDMXFixtureSignalFormat::E16Bit;

			ActiveMode1.Functions.Add(ActiveModeFunction1);
			ActiveMode1.Functions.Add(ActiveModeFunction2);
			Test->AddInfo(TEXT("Add Function 1 and 2 to ActiveMode 1"));

			// Add mode
			ActiveMode1.ModeName = "ActiveMode1";
			FixtureTypeObject1->Modes.Add(ActiveMode1);
			Test->AddInfo(TEXT("Add ActiveMode 1 to FixtureTypeObject 1"));

			FixtureTypeObject1->PostEditChange();

			DMXEditor->InvokeEditorTabFromEntityType(UDMXEntityFixtureType::StaticClass());
			DMXEditor->GetOnSetupNewEntity().Broadcast(FixtureTypeObject1);
		}

		// Add fixture type 2
		{
			FName NewTemplateName = MakeUniqueObjectName(DMXLibraryObject, UDMXEntityFixtureType::StaticClass());
			FixtureTypeObject2 = Cast<UDMXEntityFixtureType>(DMXLibraryObject->GetOrCreateEntityObject(TEXT(""), UDMXEntityFixtureType::StaticClass()));
			FixtureTypeObject2->SetName(NewTemplateName.ToString());

			// Add functions
			ActiveModeFunction3.FunctionName = "ActiveModeFunction3";
			ActiveModeFunction3.Channel = 3;
			ActiveModeFunction3.DataType = EDMXFixtureSignalFormat::E24Bit;

			ActiveModeFunction4.FunctionName = "ActiveModeFunction4";
			ActiveModeFunction4.Channel = 4;
			ActiveModeFunction4.DataType = EDMXFixtureSignalFormat::E32Bit;

			ActiveMode2.Functions.Add(ActiveModeFunction3);
			ActiveMode2.Functions.Add(ActiveModeFunction4);
			Test->AddInfo(TEXT("Add Function 3 and 4 to ActiveMode 2"));

			// Add mode
			ActiveMode2.ModeName = "ActiveMode2";
			FixtureTypeObject2->Modes.Add(ActiveMode2);
			Test->AddInfo(TEXT("Add ActiveMode 2 to FixtureTypeObject 2"));

			FixtureTypeObject2->PostEditChange();

			DMXEditor->InvokeEditorTabFromEntityType(UDMXEntityFixtureType::StaticClass());
			DMXEditor->GetOnSetupNewEntity().Broadcast(FixtureTypeObject2);
		}


		return true;
	}

	bool DMXLibrary_AddFixturePath_4()
	{
		if (!(DMXLibraryObject && DMXEditor.IsValid()))
		{
			return false;
		}

		// Add fixture patch 1
		{
			FName NewTemplateName = MakeUniqueObjectName(DMXLibraryObject, UDMXEntityFixturePatch::StaticClass());
			FixturePatchObject1 = Cast<UDMXEntityFixturePatch>(DMXLibraryObject->GetOrCreateEntityObject(TEXT(""), UDMXEntityFixturePatch::StaticClass()));
			FixturePatchObject1->SetName(NewTemplateName.ToString());

			FixturePatchObject1->PostEditChange();
			Test->AddInfo(TEXT("Add FixturePatchObject 1"));

			DMXEditor->InvokeEditorTabFromEntityType(UDMXEntityFixturePatch::StaticClass());
			DMXEditor->GetOnSetupNewEntity().Broadcast(FixturePatchObject1);
		}

		// Add fixure patch 2
		{
			FName NewTemplateName = MakeUniqueObjectName(DMXLibraryObject, UDMXEntityFixturePatch::StaticClass());
			FixturePatchObject2 = Cast<UDMXEntityFixturePatch>(DMXLibraryObject->GetOrCreateEntityObject(TEXT(""), UDMXEntityFixturePatch::StaticClass()));
			FixturePatchObject2->SetName(NewTemplateName.ToString());

			FixturePatchObject2->PostEditChange();

			DMXEditor->InvokeEditorTabFromEntityType(UDMXEntityFixturePatch::StaticClass());
			DMXEditor->GetOnSetupNewEntity().Broadcast(FixturePatchObject2);
			Test->AddInfo(TEXT("Add FixturePatchObject 2"));
		}

		return true;
	}

	bool DMXLibrary_AddFixtureTypeToFixturePatch_5()
	{
		if (!(DMXLibraryObject && DMXEditor.IsValid() && FixtureTypeObject1 && FixtureTypeObject2))
		{
			return false;
		}

		FixturePatchObject1->ParentFixtureTypeTemplate = FixtureTypeObject1;
		Test->AddInfo(TEXT("Assign FixtureTypeObject 1 to FixturePatchObject 1"));
		FixturePatchObject2->ParentFixtureTypeTemplate = FixtureTypeObject2;
		Test->AddInfo(TEXT("Assign FixtureTypeObject 2 to FixturePatchObject 2"));
		return true;
	}

	bool Blueprint_CreateNewBlueprint_6()
	{
		UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
		Factory->ParentClass = AActor::StaticClass();

		const FString PackageName = GetGamePath() + TEXT("/") + BlueprintNameString;
		BlueprintPackage = CreatePackage(*PackageName);
		EObjectFlags Flags = RF_Public | RF_Standalone;

		UObject* ExistingBlueprint = FindObject<UBlueprint>(BlueprintPackage, *BlueprintNameString);
		Test->TestNull(TEXT("Blueprint asset does not already exist (delete blueprint and restart editor)"), ExistingBlueprint);
		// Exit early since test will not be valid with pre-existing assets
		if (ExistingBlueprint)
		{
			return true;
		}

		BlueprintObject = Cast<UBlueprint>(Factory->FactoryCreateNew(UBlueprint::StaticClass(), BlueprintPackage, FName(*BlueprintNameString), Flags, NULL, GWarn));
		Test->TestNotNull(TEXT("Created new Actor-based blueprint"), BlueprintObject);
		if (BlueprintObject)
		{
			// Update asset registry and mark package dirty
			FAssetRegistryModule::AssetCreated(BlueprintObject);
			BlueprintPackage->MarkPackageDirty();

			Test->AddInfo(TEXT("Opening the blueprint editor for the first time"));
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(BlueprintObject);
		}

		IAssetEditorInstance* AssetEditor = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(BlueprintObject, true);
		FBlueprintEditor* BlueprintEditor = (FBlueprintEditor*)AssetEditor;

		UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(BlueprintObject);
		EventGraph->Schema = UEdGraphSchema_K2::StaticClass();

		BlueprintEditor->OpenGraphAndBringToFront(EventGraph);
		Test->AddInfo(TEXT("Opened the event graph"));

		return true;
	}

	bool Blueprint_AddGetDMXFunctionValuesNode_7()
	{
		if (!(DMXLibraryObject && DMXEditor.IsValid() && FixtureTypeObject1 && FixtureTypeObject2))
		{
			return false;
		}

		UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(BlueprintObject);

		PostBeginPlayEventNode = CreatePostBeginPlayEvent(BlueprintObject, EventGraph, FVector2D(200, 0));
		Test->TestNotNull(TEXT("Created EventBeginPlay node"), PostBeginPlayEventNode);

		UEdGraphPin* PlayThenPin = PostBeginPlayEventNode->FindPin(UEdGraphSchema_K2::PN_Then);

		// Add K2Node_GetDMXFixturePatch 1
		{
			K2Node_GetDMXFixturePatch1 = Cast<UK2Node_GetDMXFixturePatch>(GetDMXFixturePatch(BlueprintObject, EventGraph, FVector2D(200, 200)));
			Test->TestNotNull(TEXT("Add K2Node_GetDMXFixturePatch 1 node"), K2Node_GetDMXFixturePatch1);

			// Set input fixture patch
			FDMXEntityFixturePatchRef FixturePatchRef(FixturePatchObject1);
			FixturePatchRef.DMXLibrary = DMXLibraryObject;
			K2Node_GetDMXFixturePatch1->SetInFixturePatchPinValue(FixturePatchRef);
		}

		// Add K2Node_GetDMXAttributeValues 1
		{
			K2Node_GetDMXAttributeValues1 = Cast<UK2Node_GetDMXAttributeValues>(AddGetDMXAttributeValues(BlueprintObject, EventGraph, FVector2D(500, 0), PlayThenPin));
			Test->TestNotNull(TEXT("Created GetDMXAttributeValues1 node"), K2Node_GetDMXAttributeValues1);
		}

		// Connect Patch1 to FunctionValues1
		{
			UEdGraphPin* OutputDMXFixturePatchPin = K2Node_GetDMXFixturePatch1->GetOutputDMXFixturePatchPin();
			UEdGraphPin* InputDMXFixturePatchPin = K2Node_GetDMXAttributeValues1->GetInputDMXFixturePatchPin();
			InputDMXFixturePatchPin->MakeLinkTo(OutputDMXFixturePatchPin);
			Test->AddInfo(TEXT("Link FixturePatch 1 to FunctionValues 1"));
		}

		// Expose funсtions for K2Node_GetDMXAttributeValues1
		K2Node_GetDMXAttributeValues1->ExposeAttributes();
		Test->AddInfo(TEXT("Expose funсtions for K2Node_GetDMXAttributeValues1"));

		// Add Integer value
		AddIntegerMemberValue(BlueprintObject, *MemberVariableString1);

		// Create SetMemberVariableNode1 for integer 1
		{
			SetMemberVariableNode1 = Cast<UK2Node_VariableSet>(AddGetSetNode(BlueprintObject, EventGraph, MemberVariableString1, false, FVector2D(1000, 0)));
			Test->TestNotNull(TEXT("Added SetMemberVariableNode1"), SetMemberVariableNode1);
		}

		// Connect K2Node_GetDMXAttributeValues1 to SetMemberVariableNode1
		{
			UEdGraphPin* ActiveModeFunction2_16BitPin = K2Node_GetDMXAttributeValues1->FindPin(TEXT("ActiveModeFunction2_16 Bit"));
			UEdGraphPin* SetMemberVariableNodePin = SetMemberVariableNode1->FindPin(MemberVariableString1);
			ActiveModeFunction2_16BitPin->MakeLinkTo(SetMemberVariableNodePin);
			Test->TestTrue(TEXT("Link K2Node_GetDMXAttributeValues 1 to SetMemberVariableNode 1"), ActiveModeFunction2_16BitPin->LinkedTo.Contains(SetMemberVariableNodePin));

			// Connect Exec
			UEdGraphPin* FunctionValuesPin = K2Node_GetDMXAttributeValues1->FindPin(UEdGraphSchema_K2::PN_Then);
			UEdGraphPin* MemberVariableNodePin = SetMemberVariableNode1->FindPin(UEdGraphSchema_K2::PN_Execute);
			FunctionValuesPin->MakeLinkTo(MemberVariableNodePin);
			Test->TestTrue(TEXT("Link FunctionValues 1 then to SetMemberVariableNode 1 exec"), FunctionValuesPin->LinkedTo.Contains(MemberVariableNodePin));
		}

		Test->TestEqual(TEXT("Amount of the exposed pins"), K2Node_GetDMXAttributeValues1->UserDefinedPins.Num(), 4);

		CompileBlueprint(BlueprintObject);

		return true;
	}

	bool Blueprint_AddGetDMXFunctionValuesNode_8()
	{
		if (!(DMXLibraryObject && DMXEditor.IsValid() && FixtureTypeObject1 && FixtureTypeObject2))
		{
			return false;
		}

		UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(BlueprintObject);
		UEdGraphPin* SetMemberVariableNode1ThenPin = SetMemberVariableNode1->FindPin(UEdGraphSchema_K2::PN_Then);

		// Add K2Node_GetDMXFixturePatch 2
		{
			K2Node_GetDMXFixturePatch2 = Cast<UK2Node_GetDMXFixturePatch>(GetDMXFixturePatch(BlueprintObject, EventGraph, FVector2D(200, 700)));
			Test->TestNotNull(TEXT("Add K2Node_GetDMXFixturePatch 2 node"), K2Node_GetDMXFixturePatch1);

			// Set input fixture patch
			FDMXEntityFixturePatchRef FixturePatchRef(FixturePatchObject2);
			FixturePatchRef.DMXLibrary = DMXLibraryObject;
			K2Node_GetDMXFixturePatch2->SetInFixturePatchPinValue(FixturePatchRef);
		}

		// Add K2Node_GetDMXAttributeValues 2
		{
			K2Node_GetDMXAttributeValues2 = Cast<UK2Node_GetDMXAttributeValues>(AddGetDMXAttributeValues(BlueprintObject, EventGraph, FVector2D(500, 500), SetMemberVariableNode1ThenPin));
			Test->TestNotNull(TEXT("Created GetDMXAttributeValues 2 node"), K2Node_GetDMXAttributeValues2);
		}

		// Connect Patch2 to FunctionValues2
		{
			UEdGraphPin* OutputDMXFixturePatchPin = K2Node_GetDMXFixturePatch2->GetOutputDMXFixturePatchPin();
			UEdGraphPin* InputDMXFixturePatchPin = K2Node_GetDMXAttributeValues2->GetInputDMXFixturePatchPin();
			InputDMXFixturePatchPin->MakeLinkTo(OutputDMXFixturePatchPin);
			Test->AddInfo(TEXT("Link FixturePatch 2 to FunctionValues 2"));
		}

		// Expose funсtions for K2Node_GetDMXAttributeValues1
		K2Node_GetDMXAttributeValues2->ExposeAttributes();
		Test->AddInfo(TEXT("Expose funсtions for K2Node_GetDMXAttributeValues 2"));

		// Add Integer value
		AddIntegerMemberValue(BlueprintObject, *MemberVariableString2);

		// Create SetMemberVariableNode1 for integer 2
		{
			SetMemberVariableNode2 = Cast<UK2Node_VariableSet>(AddGetSetNode(BlueprintObject, EventGraph, MemberVariableString2, false, FVector2D(1000, 500)));
			Test->TestNotNull(TEXT("Added SetMemberVariableNode 2"), SetMemberVariableNode2);
		}

		// Connect K2Node_GetDMXAttributeValues2 to SetMemberVariableNode2
		{
			UEdGraphPin* ActiveModeFunction4_32BitPin = K2Node_GetDMXAttributeValues2->FindPin(TEXT("ActiveModeFunction4_32 Bit"));
			UEdGraphPin* SetMemberVariableNodePin = SetMemberVariableNode2->FindPin(MemberVariableString2);
			ActiveModeFunction4_32BitPin->MakeLinkTo(SetMemberVariableNodePin);
			Test->TestTrue(TEXT("Link K2Node_GetDMXAttributeValues 2 to SetMemberVariableNode 2"), ActiveModeFunction4_32BitPin->LinkedTo.Contains(SetMemberVariableNodePin));

			// Connect Exec
			UEdGraphPin* FunctionValuesPin = K2Node_GetDMXAttributeValues2->FindPin(UEdGraphSchema_K2::PN_Then);
			UEdGraphPin* MemberVariableNodePin = SetMemberVariableNode2->FindPin(UEdGraphSchema_K2::PN_Execute);
			FunctionValuesPin->MakeLinkTo(MemberVariableNodePin);
			Test->TestTrue(TEXT("Link FunctionValues 2 then to SetMemberVariableNode 2 exec"), FunctionValuesPin->LinkedTo.Contains(MemberVariableNodePin));
		}

		// Test amount output pins for K2Node_GetDMXAttributeValues2
		Test->TestEqual(TEXT("Amount of the exposed pins"), K2Node_GetDMXAttributeValues2->UserDefinedPins.Num(), 4);

		CompileBlueprint(BlueprintObject);

		return true;
	}


	bool DMXLibrary_ChangeFixtureTypeFunctionDataValueType_9()
	{
		// Change ActiveModeFunction1 datatype from E8Bit to E24Bit
		FDMXFixtureFunction& FixtureFunction1 = FixtureTypeObject1->Modes[0].Functions[0];
		Test->TestTrue(TEXT("Data type for ActiveModeFunction1 shold equal E8Bit"), FixtureFunction1.DataType == EDMXFixtureSignalFormat::E8Bit);
		FixtureFunction1.DataType = EDMXFixtureSignalFormat::E24Bit;

		FixtureTypeObject1->PostEditChange();

		// Notify about the changes
		FixtureTypeObject1->UpdateModeChannelProperties(FixtureTypeObject1->Modes[0]);

		// It should affect only first node
		Test->TestEqual(TEXT("Amount of the exposed pins"), K2Node_GetDMXAttributeValues1->UserDefinedPins.Num(), 0);
		Test->TestEqual(TEXT("Amount of the exposed pins"), K2Node_GetDMXAttributeValues2->UserDefinedPins.Num(), 4);

		// Connect K2Node_GetDMXAttributeValues1 to SetMemberVariableNode1
		{
			K2Node_GetDMXAttributeValues1->ExposeAttributes();
			UEdGraphPin* ActiveModeFunction1_24BitPin = K2Node_GetDMXAttributeValues1->FindPin(TEXT("ActiveModeFunction1_24 Bit"));
			UEdGraphPin* SetMemberVariableNodePin = SetMemberVariableNode1->FindPin(MemberVariableString1);
			ActiveModeFunction1_24BitPin->MakeLinkTo(SetMemberVariableNodePin);
			Test->TestTrue(TEXT("Link K2Node_GetDMXAttributeValues 1 to SetMemberVariableNode 1"), ActiveModeFunction1_24BitPin->LinkedTo.Contains(SetMemberVariableNodePin));
		}

		Test->TestEqual(TEXT("Amount of the exposed pins"), K2Node_GetDMXAttributeValues1->UserDefinedPins.Num(), 4);
		Test->TestEqual(TEXT("Amount of the exposed pins"), K2Node_GetDMXAttributeValues2->UserDefinedPins.Num(), 4);

		// Change ActiveModeFunction1 datatype from E32Bit to E16Bit
		FDMXFixtureFunction& FixtureFunction4 = FixtureTypeObject2->Modes[0].Functions[1];
		Test->TestTrue(TEXT("Data type for ActiveModeFunction1 shold equal E32Bit"), FixtureFunction4.DataType == EDMXFixtureSignalFormat::E32Bit);
		FixtureFunction4.DataType = EDMXFixtureSignalFormat::E16Bit;

		// Notify about the changes
		FixtureTypeObject2->UpdateModeChannelProperties(FixtureTypeObject2->Modes[0]);

		// It should affect only first node
		Test->TestEqual(TEXT("Amount of the exposed pins"), K2Node_GetDMXAttributeValues1->UserDefinedPins.Num(), 4);
		Test->TestEqual(TEXT("Amount of the exposed pins"), K2Node_GetDMXAttributeValues2->UserDefinedPins.Num(), 0);

		// Connect K2Node_GetDMXAttributeValues1 to SetMemberVariableNode1
		{
			K2Node_GetDMXAttributeValues2->ExposeAttributes();
			UEdGraphPin* ActiveModeFunction4_16BitPin = K2Node_GetDMXAttributeValues2->FindPin(TEXT("ActiveModeFunction4_16 Bit"));
			UEdGraphPin* SetMemberVariableNodePin = SetMemberVariableNode2->FindPin(MemberVariableString2);
			ActiveModeFunction4_16BitPin->MakeLinkTo(SetMemberVariableNodePin);
			Test->TestTrue(TEXT("Link K2Node_GetDMXAttributeValues 2 to SetMemberVariableNode 2"), ActiveModeFunction4_16BitPin->LinkedTo.Contains(SetMemberVariableNodePin));
		}

		Test->TestEqual(TEXT("Amount of the exposed pins"), K2Node_GetDMXAttributeValues1->UserDefinedPins.Num(), 4);
		Test->TestEqual(TEXT("Amount of the exposed pins"), K2Node_GetDMXAttributeValues2->UserDefinedPins.Num(), 4);

		return true;
	}

public:
	static UEdGraphNode* CreatePostBeginPlayEvent(UBlueprint* InBlueprint, UEdGraph* InGraph, FVector2D InPosition)
	{
		UEdGraph* TempOuter = NewObject<UEdGraph>((UObject*)InBlueprint);
		TempOuter->SetFlags(RF_Transient);

		// Make an add component node
		UK2Node_Event* NewEventNode = NewObject<UK2Node_Event>(TempOuter);
		NewEventNode->EventReference.SetExternalMember(FName(TEXT("ReceiveBeginPlay")), AActor::StaticClass());
		NewEventNode->bOverrideFunction = true;

		//Check for existing events
		UK2Node_Event* ExistingEvent = FBlueprintEditorUtils::FindOverrideForFunction(InBlueprint, NewEventNode->EventReference.GetMemberParentClass(NewEventNode->GetBlueprintClassFromNode()), NewEventNode->EventReference.GetMemberName());

		if (!ExistingEvent)
		{
			return CreateNewGraphNode(NewEventNode, InGraph, InPosition);
		}
		return ExistingEvent;
	}

	static UEdGraphNode* CreateNewGraphNode(UK2Node* NodeTemplate, UEdGraph* InGraph, const FVector2D& GraphLocation, UEdGraphPin* ConnectPin = nullptr)
	{
		TSharedPtr<FEdGraphSchemaAction_K2NewNode> Action = TSharedPtr<FEdGraphSchemaAction_K2NewNode>(new FEdGraphSchemaAction_K2NewNode(FText::GetEmpty(), FText::GetEmpty(), FText::GetEmpty(), 0));
		Action->NodeTemplate = NodeTemplate;

		return Action->PerformAction(InGraph, ConnectPin, GraphLocation, false);
	}

	static UEdGraphNode* AddGetDMXAttributeValues(UBlueprint* InBlueprint, UEdGraph* InGraph, FVector2D InPosition, UEdGraphPin* ConnectPin = nullptr)
	{
		UEdGraph* TempOuter = NewObject<UEdGraph>((UObject*)InBlueprint);
		TempOuter->SetFlags(RF_Transient);

		InGraph->Modify();

		return CreateNewGraphNodeFromTemplate(NewObject<UK2Node_GetDMXAttributeValues>(TempOuter), InGraph, InPosition, ConnectPin);
	}

	static UEdGraphNode* GetDMXFixturePatch(UBlueprint* InBlueprint, UEdGraph* InGraph, FVector2D InPosition, UEdGraphPin* ConnectPin = nullptr)
	{
		UEdGraph* TempOuter = NewObject<UEdGraph>((UObject*)InBlueprint);
		TempOuter->SetFlags(RF_Transient);

		InGraph->Modify();

		return CreateNewGraphNodeFromTemplate(NewObject<UK2Node_GetDMXFixturePatch>(TempOuter), InGraph, InPosition, ConnectPin);
	}

	static UEdGraphNode* AddPrintStringNode(UBlueprint* InBlueprint, UEdGraph* InGraph, FVector2D InPosition, UEdGraphPin* ConnectPin = nullptr)
	{
		UEdGraph* TempOuter = NewObject<UEdGraph>((UObject*)InBlueprint);
		TempOuter->SetFlags(RF_Transient);

		// Make a call function template
		static FName PrintStringFunctionName(TEXT("PrintString"));
		UK2Node* CallFuncNode = CreateKismetFunctionTemplate(TempOuter, PrintStringFunctionName);

		return CreateNewGraphNodeFromTemplate(CallFuncNode, InGraph, InPosition, ConnectPin);
	}

	static UK2Node* CreateKismetFunctionTemplate(UObject* NodeOuter, const FName& FunctionName)
	{
		// Make a call function template
		UK2Node_CallFunction* CallFuncNode = NewObject<UK2Node_CallFunction>(NodeOuter);
		UFunction* Function = FindFieldChecked<UFunction>(UKismetSystemLibrary::StaticClass(), FunctionName);
		CallFuncNode->FunctionReference.SetFromField<UFunction>(Function, false);
		return CallFuncNode;
	}


	static UEdGraphNode* CreateNewGraphNodeFromTemplate(UK2Node* NodeTemplate, UEdGraph* InGraph, const FVector2D& GraphLocation, UEdGraphPin* ConnectPin = NULL)
	{
		TSharedPtr<FEdGraphSchemaAction_K2NewNode> Action = TSharedPtr<FEdGraphSchemaAction_K2NewNode>(new FEdGraphSchemaAction_K2NewNode(FText::GetEmpty(), FText::GetEmpty(), FText::GetEmpty(), 0));
		Action->NodeTemplate = NodeTemplate;

		return Action->PerformAction(InGraph, ConnectPin, GraphLocation, false);
	}


	static UEdGraphNode* AddGetSetNode(UBlueprint* InBlueprint, UEdGraph* InGraph, const FString& VarName, bool bGet, FVector2D InPosition)
	{
		const FScopedTransaction PropertyChanged(LOCTEXT("AddedGraphNode", "Added a graph node"));
		InGraph->Modify();

		FEdGraphSchemaAction_K2NewNode NodeInfo;
		// Create get or set node, depending on whether we clicked on an input or output pin
		UK2Node_Variable* TemplateNode = NULL;
		if (bGet)
		{
			TemplateNode = NewObject<UK2Node_VariableGet>();
		}
		else
		{
			TemplateNode = NewObject<UK2Node_VariableSet>();
		}

		TemplateNode->VariableReference.SetSelfMember(FName(*VarName));
		NodeInfo.NodeTemplate = TemplateNode;

		return NodeInfo.PerformAction(InGraph, NULL, InPosition, true);
	}


	static void AddIntegerMemberValue(UBlueprint* InBlueprint, const FName& VariableName)
	{
		FEdGraphPinType IntegerPinType(UEdGraphSchema_K2::PC_Int, NAME_None, nullptr, EPinContainerType::None, false, FEdGraphTerminalType());
		FBlueprintEditorUtils::AddMemberVariable(InBlueprint, VariableName, IntegerPinType);
	}


	static void CompileBlueprint(UBlueprint* InBlueprint)
	{
		FBlueprintEditorUtils::RefreshAllNodes(InBlueprint);

		FKismetEditorUtilities::CompileBlueprint(InBlueprint, EBlueprintCompileOptions::SkipGarbageCollection);
		if (InBlueprint->Status == EBlueprintStatus::BS_UpToDate)
		{
			UE_LOG(LogDMXBlueprintGraphTests, Display, TEXT("Blueprint compiled successfully (%s)"), *InBlueprint->GetName());
		}
		else if (InBlueprint->Status == EBlueprintStatus::BS_UpToDateWithWarnings)
		{
			UE_LOG(LogDMXBlueprintGraphTests, Display, TEXT("Blueprint compiled successfully with warnings(%s)"), *InBlueprint->GetName());
		}
		else if (InBlueprint->Status == EBlueprintStatus::BS_Error)
		{
			UE_LOG(LogDMXBlueprintGraphTests, Display, TEXT("Blueprint failed to compile (%s)"), *InBlueprint->GetName());
		}
		else
		{
			UE_LOG(LogDMXBlueprintGraphTests, Error, TEXT("Blueprint is in an unexpected state after compiling (%s)"), *InBlueprint->GetName());
		}
	}


	static FString GetGamePath()
	{
		return TEXT("/Game/") + TestDMXAssetFolder;
	}

	static FString GetPackageDirectory()
	{
		return FPaths::ProjectContentDir() / TestDMXAssetFolder;
	}

public:

	/** Pointer to running automation test instance */
	FDMXBlueprintGraphTest* Test;

	/** The index of the test stage we are on */
	int32 CurrentStage;

	TArray<FString> StageNames;

	TArray<TestStageFn> TestStages;

private:
	TSharedPtr<FDMXEditor> DMXEditor;

	UPackage* BlueprintPackage;
	UBlueprint* BlueprintObject;

	UPackage* DMXPackage;
	UDMXLibrary* DMXLibraryObject;

	UDMXEntityController* SACNControllerObject;
	UDMXEntityFixtureType* FixtureTypeObject1;
	UDMXEntityFixtureType* FixtureTypeObject2;
	UDMXEntityFixturePatch* FixturePatchObject1;
	UDMXEntityFixturePatch* FixturePatchObject2;

	FDMXFixtureMode ActiveMode1;
	FDMXFixtureFunction ActiveModeFunction1;
	FDMXFixtureFunction ActiveModeFunction2;

	FDMXFixtureMode ActiveMode2;
	FDMXFixtureFunction ActiveModeFunction3;
	FDMXFixtureFunction ActiveModeFunction4;

	UEdGraphNode* PostBeginPlayEventNode;

	UK2Node_GetDMXFixturePatch* K2Node_GetDMXFixturePatch1;
	UK2Node_GetDMXFixturePatch* K2Node_GetDMXFixturePatch2;
	UK2Node_GetDMXAttributeValues* K2Node_GetDMXAttributeValues1;
	UK2Node_GetDMXAttributeValues* K2Node_GetDMXAttributeValues2;

	UK2Node_VariableSet* SetMemberVariableNode1;
	UK2Node_VariableSet* SetMemberVariableNode2;

private:
	static const FString BlueprintNameString;
	static const FString DMXLibraryNameString;
	static const FString MemberVariableString1;
	static const FString MemberVariableString2;
	static const FString TestDMXAssetFolder;

#undef ADD_TEST_STAGE
};

const FString FDMXBlueprintGraphTestHelper::BlueprintNameString = TEXT("DMXTestBlueprint");
const FString FDMXBlueprintGraphTestHelper::DMXLibraryNameString = TEXT("DMXTestLibrary");
const FString FDMXBlueprintGraphTestHelper::MemberVariableString1 = TEXT("MemberVariable1");
const FString FDMXBlueprintGraphTestHelper::MemberVariableString2 = TEXT("MemberVariable2");
const FString FDMXBlueprintGraphTestHelper::TestDMXAssetFolder = TEXT("BuildPromotionTest");


/**
* Latent command to run the main build promotion test
*/
DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FDMXRunBlueprintGraphTestCommand, TSharedPtr<FDMXBlueprintGraphTestHelper>, Helper);
bool FDMXRunBlueprintGraphTestCommand::Update()
{
	return Helper->Update();
}

/**
* Automation test that handles the blueprint editor promotion process
*/
bool FDMXBlueprintGraphTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDMXBlueprintGraphTestHelper> Helper = MakeShared<FDMXBlueprintGraphTestHelper>();
	Helper->Test = this;
	ADD_LATENT_AUTOMATION_COMMAND(FDMXRunBlueprintGraphTestCommand(Helper));
	return true;
}

#undef LOCTEXT_NAMESPACE

#endif //WITH_DEV_AUTOMATION_TESTS