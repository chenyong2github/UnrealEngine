// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterConfiguratorFactory.h"

#include "DisplayClusterConfiguratorEditorData.h"
#include "DisplayClusterConfiguratorEditorSubsystem.h"
#include "DisplayClusterConfigurationStrings.h"

#include "Editor.h"

UDisplayClusterConfiguratorFactory::UDisplayClusterConfiguratorFactory()
{
	SupportedClass = UDisplayClusterConfiguratorEditorData::StaticClass();

	Formats.Add(FString(DisplayClusterConfigurationStrings::file::FileExtCfg) + TEXT(";Config"));
	Formats.Add(FString(DisplayClusterConfigurationStrings::file::FileExtJson) + TEXT(";Config"));

	bCreateNew = false;
	bEditorImport = true;
}

bool UDisplayClusterConfiguratorFactory::DoesSupportClass(UClass* Class)
{
	return Class == UDisplayClusterConfiguratorEditorData::StaticClass();
}

UClass* UDisplayClusterConfiguratorFactory::ResolveSupportedClass()
{
	return UDisplayClusterConfiguratorEditorData::StaticClass();
}

UObject* UDisplayClusterConfiguratorFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags InFlags, const FString& InFilename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
	UDisplayClusterConfiguratorEditorData* EditingObject = NewObject<UDisplayClusterConfiguratorEditorData>(InParent, InClass, InName, InFlags | RF_Public);

	// Keep the path to the origin inside the editing UObject
	EditingObject->PathToConfig = InFilename;

	return EditingObject;
}

UDisplayClusterConfiguratorReimportFactory::UDisplayClusterConfiguratorReimportFactory()
{
	SupportedClass = UDisplayClusterConfiguratorEditorData::StaticClass();

	bCreateNew = false;
}


bool UDisplayClusterConfiguratorReimportFactory::CanReimport(UObject* Obj, TArray<FString>& OutFilenames)
{
	UDisplayClusterConfiguratorEditorData* ConfiguratorEditorData = Cast<UDisplayClusterConfiguratorEditorData>(Obj);
	if (ConfiguratorEditorData != nullptr)
	{
		OutFilenames.Add(ConfiguratorEditorData->PathToConfig);
		return true;
	}
	return false;
}

void UDisplayClusterConfiguratorReimportFactory::SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths)
{
	UDisplayClusterConfiguratorEditorData* ConfiguratorEditorData = Cast<UDisplayClusterConfiguratorEditorData>(Obj);
	if (ConfiguratorEditorData != nullptr)
	{
		ConfiguratorEditorData->PathToConfig = NewReimportPaths[0];
	}
}

EReimportResult::Type UDisplayClusterConfiguratorReimportFactory::Reimport(UObject* Obj)
{
	if (!Obj || !Obj->IsA(UDisplayClusterConfiguratorEditorData::StaticClass()))
	{
		return EReimportResult::Failed;
	}

	UDisplayClusterConfiguratorEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UDisplayClusterConfiguratorEditorSubsystem>();
	if (EditorSubsystem != nullptr && EditorSubsystem->ReimportAsset(Cast<UDisplayClusterConfiguratorEditorData>(Obj)))
	{
		return EReimportResult::Succeeded;
	}

	return EReimportResult::Failed;
}

int32 UDisplayClusterConfiguratorReimportFactory::GetPriority() const
{
	return ImportPriority;
}
