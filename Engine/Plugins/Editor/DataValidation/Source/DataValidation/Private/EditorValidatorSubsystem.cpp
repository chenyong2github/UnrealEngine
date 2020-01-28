// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorValidatorSubsystem.h"

#include "Editor.h"
#include "AssetToolsModule.h"
#include "ObjectTools.h"
#include "AssetRegistryModule.h"
#include "EditorUtilityBlueprint.h"
#include "Settings/EditorLoadingSavingSettings.h"
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#include "CookOnTheSide/CookOnTheFlyServer.h"
#include "Logging/MessageLog.h"
#include "Misc/ScopedSlowTask.h"
#include "AssetData.h"

#define LOCTEXT_NAMESPACE "EditorValidationSubsystem"

DEFINE_LOG_CATEGORY(LogContentValidation);


UDataValidationSettings::UDataValidationSettings()
	: bValidateOnSave(true)
{

}

UEditorValidatorSubsystem::UEditorValidatorSubsystem()
	: UEditorSubsystem()
{
	bAllowBlueprintValidators = true;
}

void UEditorValidatorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	if (!AssetRegistryModule.Get().IsLoadingAssets())
	{
		RegisterBlueprintValidators();
	}
	else
	{
		// We are still discovering assets, listen for the completion delegate before building the graph
		if (!AssetRegistryModule.Get().OnFilesLoaded().IsBoundToObject(this))
		{
			AssetRegistryModule.Get().OnFilesLoaded().AddUObject(this, &UEditorValidatorSubsystem::RegisterBlueprintValidators);
		}
	}

	// C++ registration
	TArray<UClass*> ValidatorClasses;
	GetDerivedClasses(UEditorValidatorBase::StaticClass(), ValidatorClasses);
	for (UClass* ValidatorClass : ValidatorClasses)
	{
		if (!ValidatorClass->HasAllClassFlags(CLASS_Abstract))
		{
			UPackage* const ClassPackage = ValidatorClass->GetOuterUPackage();
			if (ClassPackage)
			{
				const FName ModuleName = FPackageName::GetShortFName(ClassPackage->GetFName());
				if (FModuleManager::Get().IsModuleLoaded(ModuleName))
				{
					UEditorValidatorBase* Validator = NewObject<UEditorValidatorBase>(GetTransientPackage(), ValidatorClass);
					AddValidator(Validator);
				}
			}
		}
	}
}

// Rename to BP validators
void UEditorValidatorSubsystem::RegisterBlueprintValidators()
{
	if (bAllowBlueprintValidators)
	{
		// Locate all validators (include unloaded)
		const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		TArray<FAssetData> AllBPsAssetData;
		AssetRegistryModule.Get().GetAssetsByClass(UEditorUtilityBlueprint::StaticClass()->GetFName(), AllBPsAssetData, true);

		for (FAssetData& BPAssetData : AllBPsAssetData)
		{
			UClass* ParentClass = nullptr;
			FString ParentClassName;
			if (!BPAssetData.GetTagValue(FBlueprintTags::NativeParentClassPath, ParentClassName))
			{
				BPAssetData.GetTagValue(FBlueprintTags::ParentClassPath, ParentClassName);
			}

			if (!ParentClassName.IsEmpty())
			{
				UObject* Outer = nullptr;
				ResolveName(Outer, ParentClassName, false, false);
				ParentClass = FindObject<UClass>(ANY_PACKAGE, *ParentClassName);
				if (!ParentClass->IsChildOf(UEditorValidatorBase::StaticClass()))
				{
					continue;
				}
			}

			// If this object isn't currently loaded, load it
			UObject* ValidatorObject = BPAssetData.ToSoftObjectPath().ResolveObject();
			if (ValidatorObject == nullptr)
			{
				ValidatorObject = BPAssetData.ToSoftObjectPath().TryLoad();
			}
			if (ValidatorObject)
			{
				UEditorUtilityBlueprint* ValidatorBlueprint = Cast<UEditorUtilityBlueprint>(ValidatorObject);
				UEditorValidatorBase* Validator = NewObject<UEditorValidatorBase>(GetTransientPackage(), ValidatorBlueprint->GeneratedClass);
				AddValidator(Validator);
			}
		}
	}
}

void UEditorValidatorSubsystem::Deinitialize()
{
	CleanupValidators();

	Super::Deinitialize();
}

void UEditorValidatorSubsystem::AddValidator(UEditorValidatorBase* InValidator)
{
	if (InValidator)
	{
		Validators.Add(InValidator->GetClass(), InValidator);
	}
}

void UEditorValidatorSubsystem::CleanupValidators()
{
	Validators.Empty();
}

EDataValidationResult UEditorValidatorSubsystem::IsObjectValid(UObject* InObject, TArray<FText>& ValidationErrors, TArray<FText>& ValidationWarnings) const
{
	EDataValidationResult Result = EDataValidationResult::NotValidated;
	
	if (ensure(InObject))
	{
		// First check the class level validation
		Result = InObject->IsDataValid(ValidationErrors);
		// If the asset is still valid or there wasn't a class-level validation, keep validating with custom validators
		if (Result != EDataValidationResult::Invalid)
		{
			for (TPair<UClass*, UEditorValidatorBase*> ValidatorPair : Validators)
			{
				if (ValidatorPair.Value && ValidatorPair.Value->IsEnabled() && ValidatorPair.Value->CanValidateAsset(InObject))
				{
					ValidatorPair.Value->ResetValidationState();
					EDataValidationResult NewResult = ValidatorPair.Value->ValidateLoadedAsset(InObject, ValidationErrors);

					// Don't accidentally overwrite an invalid result with a valid or not-validated one
					if(Result != EDataValidationResult::Invalid)
					{
						Result = NewResult;
					}

					ValidationWarnings.Append(ValidatorPair.Value->GetAllWarnings());

					ensureMsgf(ValidatorPair.Value->IsValidationStateSet(), TEXT("Validator %s did not include a pass or fail state."), *ValidatorPair.Value->GetClass()->GetName());
				}
			}
		}
	}

	return Result;
}

EDataValidationResult UEditorValidatorSubsystem::IsAssetValid(FAssetData& AssetData, TArray<FText>& ValidationErrors, TArray<FText>& ValidationWarnings) const
{
	if (AssetData.IsValid())
	{
		UObject* Obj = AssetData.GetAsset();
		if (Obj)
		{
			return IsObjectValid(Obj, ValidationErrors, ValidationWarnings);
		}
		return EDataValidationResult::NotValidated;
	}

	return EDataValidationResult::Invalid;
}

int32 UEditorValidatorSubsystem::ValidateAssets(TArray<FAssetData> AssetDataList, bool bSkipExcludedDirectories, bool bShowIfNoFailures) const
{
	FScopedSlowTask SlowTask(1.0f, LOCTEXT("ValidatingDataTask", "Validating Data..."));
	SlowTask.Visibility = bShowIfNoFailures ? ESlowTaskVisibility::ForceVisible : ESlowTaskVisibility::Invisible;
	if (bShowIfNoFailures)
	{
		SlowTask.MakeDialogDelayed(.1f);
	}

	FMessageLog DataValidationLog("AssetCheck");

	int32 NumAdded = 0;

	int32 NumFilesChecked = 0;
	int32 NumValidFiles = 0;
	int32 NumInvalidFiles = 0;
	int32 NumFilesSkipped = 0;
	int32 NumFilesUnableToValidate = 0;
	bool bAtLeastOneWarning = false;

	int32 NumFilesToValidate = AssetDataList.Num();

	// Now add to map or update as needed
	for (FAssetData& Data : AssetDataList)
	{
		FText ValidatingMessage = FText::Format(LOCTEXT("ValidatingFilename", "Validating {0}"), FText::FromString(Data.GetFullName()));
		SlowTask.EnterProgressFrame(1.0f / NumFilesToValidate, ValidatingMessage);

		// Check exclusion path
		if (bSkipExcludedDirectories && IsPathExcludedFromValidation(Data.PackageName.ToString()))
		{
			++NumFilesSkipped;
			continue;
		}

		UE_LOG(LogContentValidation, Display, TEXT("%s"), *ValidatingMessage.ToString());

		TArray<FText> ValidationErrors;
		TArray<FText> ValidationWarnings;
		EDataValidationResult Result = IsAssetValid(Data, ValidationErrors, ValidationWarnings);
		++NumFilesChecked;

		for (const FText& ErrorMsg : ValidationErrors)
		{
			DataValidationLog.Error()->AddToken(FTextToken::Create(ErrorMsg));
		}

		if (ValidationWarnings.Num() > 0)
		{
			bAtLeastOneWarning = true;

			for (const FText& WarningMsg : ValidationWarnings)
			{
				DataValidationLog.Warning()->AddToken(FTextToken::Create(WarningMsg));
			}
		}

		if (Result == EDataValidationResult::Valid)
		{
			if (ValidationWarnings.Num() > 0)
			{
				DataValidationLog.Info()->AddToken(FAssetNameToken::Create(Data.PackageName.ToString()))
					->AddToken(FTextToken::Create(LOCTEXT("ContainsWarningsResult", "contains valid data, but has warnings.")));
			}
			++NumValidFiles;
		}
		else
		{
			if (Result == EDataValidationResult::Invalid)
			{
				DataValidationLog.Info()->AddToken(FAssetNameToken::Create(Data.PackageName.ToString()))
					->AddToken(FTextToken::Create(LOCTEXT("InvalidDataResult", "contains invalid data.")));
				++NumInvalidFiles;
			}
			else if (Result == EDataValidationResult::NotValidated)
			{
				if (bShowIfNoFailures)
				{
					DataValidationLog.Info()->AddToken(FAssetNameToken::Create(Data.PackageName.ToString()))
						->AddToken(FTextToken::Create(LOCTEXT("NotValidatedDataResult", "has no data validation.")));
				}
				++NumFilesUnableToValidate;
			}
		}
	}

	const bool bFailed = (NumInvalidFiles > 0);

	if (bFailed || bAtLeastOneWarning || bShowIfNoFailures)
	{
		FFormatNamedArguments Arguments;
		Arguments.Add(TEXT("Result"), bFailed ? LOCTEXT("Failed", "FAILED") : LOCTEXT("Succeeded", "SUCCEEDED"));
		Arguments.Add(TEXT("NumChecked"), NumFilesChecked);
		Arguments.Add(TEXT("NumValid"), NumValidFiles);
		Arguments.Add(TEXT("NumInvalid"), NumInvalidFiles);
		Arguments.Add(TEXT("NumSkipped"), NumFilesSkipped);
		Arguments.Add(TEXT("NumUnableToValidate"), NumFilesUnableToValidate);

		DataValidationLog.Info()->AddToken(FTextToken::Create(FText::Format(LOCTEXT("SuccessOrFailure", "Data validation {Result}."), Arguments)));
		DataValidationLog.Info()->AddToken(FTextToken::Create(FText::Format(LOCTEXT("ResultsSummary", "Files Checked: {NumChecked}, Passed: {NumValid}, Failed: {NumInvalid}, Skipped: {NumSkipped}, Unable to validate: {NumUnableToValidate}"), Arguments)));

		DataValidationLog.Open(EMessageSeverity::Info, true);
	}

	return NumInvalidFiles;
}

void UEditorValidatorSubsystem::ValidateOnSave(TArray<FAssetData> AssetDataList) const
{
	// Only validate if enabled and not auto saving
	if (!GetDefault<UDataValidationSettings>()->bValidateOnSave || GEditor->IsAutosaving())
	{
		return;
	}

	bool bIsCooking = GIsCookerLoadingPackage;
	if ((!bValidateAssetsWhileSavingForCook && bIsCooking))
	{
		return;
	}

	FMessageLog DataValidationLog("AssetCheck");
	FText SavedAsset = AssetDataList.Num() == 1 ? FText::FromName(AssetDataList[0].AssetName) : LOCTEXT("MultipleErrors", "multiple assets");
	DataValidationLog.NewPage(FText::Format(LOCTEXT("DataValidationLogPage", "Asset Save: {0}"), SavedAsset));
	if (ValidateAssets(AssetDataList, true, false) > 0)
	{
		const FText ErrorMessageNotification = FText::Format(
			LOCTEXT("ValidationFailureNotification", "Validation failed when saving {0}, check Data Validation log"), SavedAsset);
		DataValidationLog.Notify(ErrorMessageNotification, EMessageSeverity::Warning, /*bForce=*/ true);
	}
}

void UEditorValidatorSubsystem::ValidateSavedPackage(FName PackageName)
{
	// Only validate if enabled and not auto saving
	if (!GetDefault<UDataValidationSettings>()->bValidateOnSave || GEditor->IsAutosaving())
	{
		return;
	}

	// For performance reasons, don't validate when cooking by default. Assumption is we validated when saving previously. 
	bool bIsCooking = GIsCookerLoadingPackage;
	if ((!bValidateAssetsWhileSavingForCook && bIsCooking))
	{
		return;
	}

	if (SavedPackagesToValidate.Num() == 0)
	{
		GEditor->GetTimerManager()->SetTimerForNextTick(this, &UEditorValidatorSubsystem::ValidateAllSavedPackages);
	}

	SavedPackagesToValidate.AddUnique(PackageName);
}

bool UEditorValidatorSubsystem::IsPathExcludedFromValidation(const FString& Path) const
{
	for (const FDirectoryPath& ExcludedPath : ExcludedDirectories)
	{
		if (Path.Contains(ExcludedPath.Path))
		{
			return true;
		}
	}

	return false;
}

void UEditorValidatorSubsystem::ValidateAllSavedPackages()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UEditorValidatorSubsystem::ValidateAllSavedPackages);

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	// Prior to validation, make sure Asset Registry is updated.
	// DirectoryWatcher is responsible of scanning modified asset files, but validation can be called before.
	if (SavedPackagesToValidate.Num())
	{
		TArray<FString> FilesToScan;
		FilesToScan.Reserve(SavedPackagesToValidate.Num());
		for (FName PackageName : SavedPackagesToValidate)
		{
			FString PackageFilename;
			if (FPackageName::FindPackageFileWithoutExtension(FPackageName::LongPackageNameToFilename(PackageName.ToString()), PackageFilename))
			{
				FilesToScan.Add(PackageFilename);
			}
		}
		if (FilesToScan.Num())
		{
			AssetRegistry.ScanModifiedAssetFiles(FilesToScan);
		}
	}

	TArray<FAssetData> Assets;
	for (FName PackageName : SavedPackagesToValidate)
	{
		// We need to query the in-memory data as the disk cache may not be accurate
		AssetRegistry.GetAssetsByPackageName(PackageName, Assets);
	}

	ValidateOnSave(Assets);

	SavedPackagesToValidate.Empty();
}

#undef LOCTEXT_NAMESPACE
