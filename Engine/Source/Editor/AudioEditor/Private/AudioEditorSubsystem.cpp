// Copyright Epic Games, Inc. All Rights Reserved.
#include "AudioEditorSubsystem.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/BlueprintSupport.h"
#include "Blueprint/UserWidget.h"
#include "Editor.h"
#include "Interfaces/IPluginManager.h"
#include "Internationalization/Text.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"
#include "Subsystems/SubsystemCollection.h"
#include "UObject/CoreRedirects.h"
#include "WidgetBlueprint.h"


#define LOCTEXT_NAMESPACE "AudioEditorSubsystem"

TArray<UUserWidget*> UAudioEditorSubsystem::CreateUserWidgets(TSubclassOf<UAudioWidgetInterface> InWidgetClass, UClass* InObjectClass) const
{
	TArray<UUserWidget*> UserWidgets;
	UClass* InterfaceClass = InWidgetClass ? InWidgetClass.Get() : UAudioWidgetInterface::StaticClass();
	if (!ensure(InterfaceClass))
	{
		return UserWidgets;
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return UserWidgets;
	}

	const TArray<FAssetData> AssetData = GetWidgetBlueprintAssetData();
	for (const FAssetData& AssetEntry : AssetData)
	{
		if (!ImplementsInterface(AssetEntry, InterfaceClass))
		{
			continue;
		}

		UObject* Asset = AssetEntry.GetAsset();
		if (!Asset)
		{
			continue;
		}

		UWidgetBlueprint* WidgetBlueprint = CastChecked<UWidgetBlueprint>(Asset);
		UClass* GeneratedClass = WidgetBlueprint->GeneratedClass;
		if (!GeneratedClass)
		{
			continue;
		}

		UUserWidget* UserWidget = CreateWidget<UUserWidget>(World, GeneratedClass);
		if (!UserWidget)
		{
			continue;
		}

		if (InObjectClass)
		{
			const UClass* ObjectClass = IAudioWidgetInterface::Execute_GetClass(UserWidget);
			while (ObjectClass)
			{
				if (ObjectClass == InObjectClass)
				{
					UserWidgets.Add(UserWidget);
					break;
				}

				ObjectClass = ObjectClass->GetSuperClass();
			}
		}
		else
		{
			UserWidgets.Add(UserWidget);
		}
	}

	return UserWidgets;
}

const TArray<FAssetData> UAudioEditorSubsystem::GetWidgetBlueprintAssetData()
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> AssetData;
	const UClass* Class = UWidgetBlueprint::StaticClass();
	AssetRegistryModule.Get().GetAssetsByClass(Class->GetFName(), AssetData);
	return AssetData;
}

bool UAudioEditorSubsystem::ImplementsInterface(const FAssetData& InAssetData, UClass* InInterfaceClass)
{
	const FString ImplementedInterfaces = InAssetData.GetTagValueRef<FString>(FBlueprintTags::ImplementedInterfaces);
	if (!ImplementedInterfaces.IsEmpty())
	{
		FString FullInterface;
		FString RemainingString;
		FString InterfacePath;
		FString CurrentString = *ImplementedInterfaces;
		while (CurrentString.Split(TEXT(","), &FullInterface, &RemainingString))
		{
			if (!CurrentString.StartsWith(TEXT("Graphs=(")))
			{
				if (FullInterface.Split(TEXT("\""), &CurrentString, &InterfacePath, ESearchCase::CaseSensitive))
				{
					InterfacePath.RemoveFromEnd(TEXT("\"'"));

					const FCoreRedirectObjectName ResolvedInterfaceName = FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Class, FCoreRedirectObjectName(InterfacePath));
					if (InInterfaceClass->GetFName() == ResolvedInterfaceName.ObjectName)
					{
						return true;
					}
				}
			}

			CurrentString = RemainingString;
		}
	}

	return false;
}
#undef LOCTEXT_NAMESPACE // AudioEditorSubsystem