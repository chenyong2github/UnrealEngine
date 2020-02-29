// Copyright Epic Games, Inc. All Rights Reserved.

#include "K2Node_DMXBase.h"
#include "EdGraphSchema_K2.h"
#include "Library/DMXLibrary.h"
#include "DMXProtocolConstants.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "FindInBlueprintManager.h"
#include "BlueprintNodeSpawner.h"
#include "Kismet2/BlueprintEditorUtils.h"

FName FK2Node_DMXBaseHelper::ClassPinName("DMXLibrary");

UK2Node_DMXBase::~UK2Node_DMXBase()
{
	if (CachedLibrary != nullptr)
	{
		CachedLibrary->GetOnEntitiesUpdated().Remove(LibraryEntitiesUpdatedHandle);
		CachedLibrary = nullptr;
	}
}

void UK2Node_DMXBase::AllocateDefaultPins()
{
	UEdGraphPin* ClassPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Object, GetClassPinBaseClass(), FK2Node_DMXBaseHelper::ClassPinName);

	Super::AllocateDefaultPins();
}

bool UK2Node_DMXBase::IsCompatibleWithGraph(const UEdGraph* TargetGraph) const
{
	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(TargetGraph);
	return Super::IsCompatibleWithGraph(TargetGraph) && (!Blueprint || (FBlueprintEditorUtils::FindUserConstructionScript(Blueprint) != TargetGraph && Blueprint->GeneratedClass->GetDefaultObject()->ImplementsGetWorld()));
}

void UK2Node_DMXBase::AddSearchMetaDataInfo(TArray<struct FSearchTagDataPair>& OutTaggedMetaData) const
{
	Super::AddSearchMetaDataInfo(OutTaggedMetaData);
	OutTaggedMetaData.Add(FSearchTagDataPair(FFindInBlueprintSearchTags::FiB_NativeName, CachedNodeTitle.GetCachedText()));
}

void UK2Node_DMXBase::PinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::PinConnectionListChanged(Pin);

	if (Pin != nullptr)
	{
		if (Pin->PinName == FK2Node_DMXBaseHelper::ClassPinName)
		{
			if (Pin->DefaultObject != nullptr)
			{
				OnLibraryAssetChanged(Cast<UDMXLibrary>(Pin->DefaultObject));
			}
		}
	}
}

void UK2Node_DMXBase::PinDefaultValueChanged(UEdGraphPin* Pin)
{
	if (Pin != nullptr)
	{
		if (Pin->PinName == FK2Node_DMXBaseHelper::ClassPinName)
		{
			if (Pin->DefaultObject != nullptr)
			{
				OnLibraryAssetChanged(Cast<UDMXLibrary>(Pin->DefaultObject));
			}
		}
	}
}

void UK2Node_DMXBase::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();

	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
		check(NodeSpawner != nullptr);

		ActionRegistrar.AddBlueprintAction(ActionKey, NodeSpawner);
	}
}

FText UK2Node_DMXBase::GetMenuCategory() const
{
	return FText::FromString(DMX_K2_CATEGORY_NAME);
}

UEdGraphPin* UK2Node_DMXBase::GetClassPin(const TArray<UEdGraphPin*>* InPinsToSearch /*= NULL*/) const
{
	const TArray<UEdGraphPin*>* PinsToSearch = InPinsToSearch ? InPinsToSearch : &Pins;

	UEdGraphPin* Pin = nullptr;
	for (UEdGraphPin* TestPin : *PinsToSearch)
	{
		if (TestPin && TestPin->PinName == FK2Node_DMXBaseHelper::ClassPinName)
		{
			Pin = TestPin;
			break;
		}
	}
	check(Pin == nullptr || Pin->Direction == EGPD_Input);
	return Pin;
}

UClass* UK2Node_DMXBase::GetClassPinBaseClass() const
{
	return UDMXLibrary::StaticClass();
}

void UK2Node_DMXBase::OnLibraryAssetChanged(UDMXLibrary* Library)
{
	// Unbind from previous library
	if (CachedLibrary != nullptr)
	{
		CachedLibrary->GetOnEntitiesUpdated().Remove(LibraryEntitiesUpdatedHandle);
		CachedLibrary = nullptr;
	}

	if (Library != nullptr)
	{
		LibraryEntitiesUpdatedHandle = Library->GetOnEntitiesUpdated().AddUObject(this, &UK2Node_DMXBase::OnLibraryEntitiesUpdated);
		CachedLibrary = Library;
	}

	OnLibraryEntitiesUpdated(Library);
}

void UK2Node_DMXBase::OnLibraryEntitiesUpdated(UDMXLibrary* Library)
{
	UEdGraphPin* ClassPin = FindPin(FK2Node_DMXBaseHelper::ClassPinName);
	if (ClassPin != nullptr && Library != nullptr && ClassPin->DefaultObject == Library)
	{
		if (UBlueprint* BP = GetBlueprint())
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		}
	}
}
