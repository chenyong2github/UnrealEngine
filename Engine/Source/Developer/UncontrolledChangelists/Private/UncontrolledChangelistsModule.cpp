// Copyright Epic Games, Inc. All Rights Reserved.

#include "UncontrolledChangelistsModule.h"

#include "Algo/Find.h"
#include "Algo/Transform.h"
#include "AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "FileHelpers.h"
#include "HAL/IConsoleManager.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PackagesDialog.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "SourceControlOperations.h"
#include "Styling/SlateTypes.h"

#define LOCTEXT_NAMESPACE "UncontrolledChangelists"

static TAutoConsoleVariable<bool> CVarUncontrolledChangelistsEnable(
	TEXT("UncontrolledChangelists.Enable"),
	false,
	TEXT("Enables Uncontrolled Changelists (experimental).")
);

void FUncontrolledChangelistsModule::StartupModule()
{
	// Adds Default Uncontrolled Changelist if it is not already present.
	FUncontrolledChangelist DefaultUncontrolledChangelist(FUncontrolledChangelist::DEFAULT_UNCONTROLLED_CHANGELIST_GUID, FUncontrolledChangelist::DEFAULT_UNCONTROLLED_CHANGELIST_NAME.ToString());
	UncontrolledChangelistsStateCache.FindOrAdd(MoveTemp(DefaultUncontrolledChangelist), MakeShareable(new FUncontrolledChangelistState(DefaultUncontrolledChangelist)));

	LoadState();

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::GetModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	OnAssetAddedDelegateHandle = AssetRegistryModule.Get().OnAssetAdded().AddLambda([](const struct FAssetData& AssetData) { Get().OnAssetAdded(AssetData); });
	OnAssetLoadedDelegateHandle = FCoreUObjectDelegates::OnAssetLoaded.AddLambda([](UObject* InAsset) { Get().OnAssetLoaded(InAsset); });
	OnObjectTransactedDelegateHandle = FCoreUObjectDelegates::OnObjectTransacted.AddLambda([](UObject* InObject, const class FTransactionObjectEvent& InTransactionEvent)
	{
		Get().OnObjectTransacted(InObject, InTransactionEvent);
	});
}

void FUncontrolledChangelistsModule::ShutdownModule()
{
	FAssetRegistryModule* AssetRegistryModulePtr = static_cast<FAssetRegistryModule*>(FModuleManager::Get().GetModule(TEXT("AssetRegistry")));

	// Check in case AssetRegistry has already been shutdown.
	if (AssetRegistryModulePtr != nullptr)
	{
		AssetRegistryModulePtr->Get().OnAssetAdded().Remove(OnAssetAddedDelegateHandle);
	}

	FCoreUObjectDelegates::OnAssetLoaded.Remove(OnAssetLoadedDelegateHandle);
	FCoreUObjectDelegates::OnObjectTransacted.Remove(OnObjectTransactedDelegateHandle);
}

bool FUncontrolledChangelistsModule::IsEnabled() const
{
	return CVarUncontrolledChangelistsEnable.GetValueOnGameThread();
}

TArray<FUncontrolledChangelistStateRef> FUncontrolledChangelistsModule::GetChangelistStates() const
{
	TArray<FUncontrolledChangelistStateRef> UncontrolledChangelistStates;

	if (IsEnabled())
	{
		Algo::Transform(UncontrolledChangelistsStateCache, UncontrolledChangelistStates, [](const auto& Pair) { return Pair.Value; });
	}

	return UncontrolledChangelistStates;
}

void FUncontrolledChangelistsModule::OnMakeWritable(const FString& InFilename)
{
	if (!IsEnabled())
	{
		return;
	}

	FUncontrolledChangelist DefaultUncontrolledChangelist(FUncontrolledChangelist::DEFAULT_UNCONTROLLED_CHANGELIST_GUID, FUncontrolledChangelist::DEFAULT_UNCONTROLLED_CHANGELIST_NAME.ToString());
	FUncontrolledChangelistsStateCache::ValueType& UncontrolledChangelistState = UncontrolledChangelistsStateCache.FindOrAdd(MoveTemp(DefaultUncontrolledChangelist), MakeShareable(new FUncontrolledChangelistState(DefaultUncontrolledChangelist)));

	UncontrolledChangelistState->AddFiles({ InFilename }, FUncontrolledChangelistState::ECheckFlags::NotCheckedOut);

	SaveState();
}

void FUncontrolledChangelistsModule::UpdateStatus()
{
	if (!IsEnabled())
	{
		return;
	}

	for (FUncontrolledChangelistsStateCache::ElementType& Pair : UncontrolledChangelistsStateCache)
	{
		FUncontrolledChangelistsStateCache::ValueType& UncontrolledChangelistState = Pair.Value;
		UncontrolledChangelistState->UpdateStatus();
	}
}

void FUncontrolledChangelistsModule::OnAssetAdded(const struct FAssetData& AssetData)
{

}

void FUncontrolledChangelistsModule::OnAssetLoaded(UObject* InAsset)
{
	FString Fullpath = GetUObjectPackageFullpath(InAsset);

	if (Fullpath.IsEmpty())
	{
		return;
	}
}

void FUncontrolledChangelistsModule::OnObjectTransacted(UObject* InObject, const class FTransactionObjectEvent& InTransactionEvent)
{
	FString Fullpath = GetUObjectPackageFullpath(InObject);

	if (Fullpath.IsEmpty())
	{
		return;
	}
}

void FUncontrolledChangelistsModule::MoveFilesToUncontrolledChangelist(const TArray<FSourceControlStateRef>& InControlledFileStates, const TArray<FSourceControlStateRef>& InUncontrolledFileStates, const FUncontrolledChangelist& InUncontrolledChangelist)
{
	if (!IsEnabled())
	{
		return;
	}

	FUncontrolledChangelistsStateCache::ValueType* ChangelistState = UncontrolledChangelistsStateCache.Find(InUncontrolledChangelist);

	if (ChangelistState == nullptr)
	{
		return;
	}

	TArray<FString> Filenames;
	Algo::Transform(InControlledFileStates, Filenames, [](const FSourceControlStateRef& State) { return State->GetFilename(); });

	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
	auto RevertOperation = ISourceControlOperation::Create<FRevert>();

	// Revert controlled files
	RevertOperation->SetSoftRevert(true);
	SourceControlProvider.Execute(RevertOperation, Filenames);

	// Removes selected Uncontrolled Files from their Uncontrolled Changelists
	for (const auto& Pair : UncontrolledChangelistsStateCache)
	{
		const FUncontrolledChangelistStateRef& UncontrolledChangelistState = Pair.Value;
		UncontrolledChangelistState->RemoveFiles(InUncontrolledFileStates);
	}

	Algo::Transform(InUncontrolledFileStates, Filenames, [](const FSourceControlStateRef& State) { return State->GetFilename(); });

	// Add all files to their UncontrolledChangelist
	(*ChangelistState)->AddFiles(Filenames, FUncontrolledChangelistState::ECheckFlags::None);

	SaveState();
}

void FUncontrolledChangelistsModule::MoveFilesToControlledChangelist(const TArray<FSourceControlStateRef>& InUncontrolledFileStates, const FSourceControlChangelistPtr& InChangelist)
{
	if (!IsEnabled())
	{
		return;
	}

	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
	TArray<FString> UncontrolledFilenames;
	TArray<FSourceControlStateRef> UpdatedFilestates;

	// Get updated filestates to check Checkout capabilities.
	Algo::Transform(InUncontrolledFileStates, UncontrolledFilenames, [](const FSourceControlStateRef& State) { return State->GetFilename(); });
	SourceControlProvider.GetState(UncontrolledFilenames, UpdatedFilestates, EStateCacheUsage::ForceUpdate);

	TArray<UPackage*> PackageConflicts;

	// Check if we can Checkout files
	for (const FSourceControlStateRef& Filestate : UpdatedFilestates)
	{
		if (!Filestate->CanCheckout())
		{
			FString PackageName;
			FPackageName::TryConvertFilenameToLongPackageName(Filestate->GetFilename(), PackageName);
			UPackage* FoundPackage = FindOrConstructDynamicTypePackage(*PackageName);

			if (FoundPackage != nullptr)
			{
				PackageConflicts.Add(FoundPackage);
			}
		}
	}

	bool bCanProceed = true;

	// If we detected conflict, asking user if we should proceed.
	if (!PackageConflicts.IsEmpty())
	{
		bCanProceed = ShowConflictDialog(PackageConflicts);
	}

	if (bCanProceed)
	{
		SourceControlProvider.Execute(ISourceControlOperation::Create<FCheckOut>(), InChangelist, UncontrolledFilenames);
	}
}

bool FUncontrolledChangelistsModule::ShowConflictDialog(TArray<UPackage*> InPackageConflicts)
{
	FPackagesDialogModule& CheckoutPackagesDialogModule = FModuleManager::LoadModuleChecked<FPackagesDialogModule>(TEXT("PackagesDialog"));

	CheckoutPackagesDialogModule.CreatePackagesDialog(LOCTEXT("CheckoutPackagesDialogTitle", "Check Out Assets"),
													  LOCTEXT("CheckoutPackagesDialogMessage", "Conflict detected in the following assets:"),
													  true);

	CheckoutPackagesDialogModule.AddButton(DRT_CheckOut, LOCTEXT("Dlg_CheckOutButton", "Check Out"), LOCTEXT("Dlg_CheckOutTooltip", "Attempt to Check Out Assets"));
	CheckoutPackagesDialogModule.AddButton(DRT_Cancel, LOCTEXT("Dlg_Cancel", "Cancel"), LOCTEXT("Dlg_CancelTooltip", "Cancel Request"));

	CheckoutPackagesDialogModule.SetWarning(LOCTEXT("CheckoutPackagesWarnMessage", "Warning: These assets are locked or not at the head revision. You may lose your changes if you continue, as you will be unable to submit them to source control."));

	for (UPackage* Conflict : InPackageConflicts)
	{
		CheckoutPackagesDialogModule.AddPackageItem(Conflict, ECheckBoxState::Undetermined);
	}

	EDialogReturnType UserResponse = CheckoutPackagesDialogModule.ShowPackagesDialog();

	return UserResponse == DRT_CheckOut;
}

void FUncontrolledChangelistsModule::SaveState() const
{
	TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);
	TArray<TSharedPtr<FJsonValue>> UncontrolledChangelistsArray;

	RootObject->SetNumberField(VERSION_NAME, VERSION_NUMBER);

	for (const auto& Pair : UncontrolledChangelistsStateCache)
	{
		const FUncontrolledChangelist& UncontrolledChangelist = Pair.Key;
		FUncontrolledChangelistStateRef UncontrolledChangelistState = Pair.Value;
		TSharedPtr<FJsonObject> UncontrolledChangelistObject = MakeShareable(new FJsonObject);

		UncontrolledChangelist.Serialize(UncontrolledChangelistObject.ToSharedRef());
		UncontrolledChangelistState->Serialize(UncontrolledChangelistObject.ToSharedRef());

		UncontrolledChangelistsArray.Add(MakeShareable(new FJsonValueObject(UncontrolledChangelistObject)));
	}

	RootObject->SetArrayField(CHANGELISTS_NAME, UncontrolledChangelistsArray);

	using FStringWriter = TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>;
	using FStringWriterFactory = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>;

	FString RootObjectStr;
	TSharedRef<FStringWriter> Writer = FStringWriterFactory::Create(&RootObjectStr);
	FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);

	FFileHelper::SaveStringToFile(RootObjectStr, *GetPersistentFilePath());
}

void FUncontrolledChangelistsModule::LoadState()
{
	FString ImportJsonString;
	TSharedPtr<FJsonObject> RootObject;
	uint32 VersionNumber = 0;
	const TArray<TSharedPtr<FJsonValue>>* UncontrolledChangelistsArray = nullptr;

	if (!FFileHelper::LoadFileToString(ImportJsonString, *GetPersistentFilePath()))
	{
		UE_LOG(LogSourceControl, Warning, TEXT("Cannot load file %s."), *GetPersistentFilePath());
		return;
	}

	TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(ImportJsonString);

	if (!FJsonSerializer::Deserialize(JsonReader, RootObject))
	{
		UE_LOG(LogSourceControl, Error, TEXT("Cannot deserialize RootObject."));
		return;
	}

	if (!RootObject->TryGetNumberField(VERSION_NAME, VersionNumber))
	{
		UE_LOG(LogSourceControl, Error, TEXT("Cannot get field %s."), VERSION_NAME);
		return;
	}

	if (VersionNumber != VERSION_NUMBER)
	{
		UE_LOG(LogSourceControl, Error, TEXT("Version number is invalid (file: %u, current: %u)."), VersionNumber, VERSION_NUMBER);
		return;
	}

	if (!RootObject->TryGetArrayField(CHANGELISTS_NAME, UncontrolledChangelistsArray))
	{
		UE_LOG(LogSourceControl, Error, TEXT("Cannot get field %s."), CHANGELISTS_NAME);
		return;
	}

	for (const TSharedPtr<FJsonValue>& JsonValue : *UncontrolledChangelistsArray)
	{
		FUncontrolledChangelist TempKey;
		TSharedRef<FJsonObject> JsonObject = JsonValue->AsObject().ToSharedRef();

		if (!TempKey.Deserialize(JsonObject))
		{
			UE_LOG(LogSourceControl, Error, TEXT("Cannot deserialize FUncontrolledChangelist."));
			continue;
		}

		FUncontrolledChangelistsStateCache::ValueType& UncontrolledChangelistState = UncontrolledChangelistsStateCache.FindOrAdd(MoveTemp(TempKey), MakeShareable(new FUncontrolledChangelistState(TempKey)));

		UncontrolledChangelistState->Deserialize(JsonObject);
	}
}

FString FUncontrolledChangelistsModule::GetPersistentFilePath() const
{
	return FPaths::ProjectSavedDir() + TEXT("SourceControl/UncontrolledChangelists.json");
}

FString FUncontrolledChangelistsModule::GetUObjectPackageFullpath(const UObject* InObject) const
{
	FString Fullpath = TEXT("");

	if (InObject == nullptr)
	{
		return Fullpath;
	}

	const UPackage* Package = InObject->GetPackage();

	if (Package == nullptr)
	{
		return Fullpath;
	}

	const FString LocalFullPath(Package->GetLoadedPath().GetLocalFullPath());

	if (LocalFullPath.IsEmpty())
	{
		return Fullpath;
	}

	Fullpath = FPaths::ConvertRelativePathToFull(LocalFullPath);

	return Fullpath;
}

IMPLEMENT_MODULE(FUncontrolledChangelistsModule, UncontrolledChangelist);

#undef LOCTEXT_NAMESPACE
