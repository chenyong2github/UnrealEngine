// Copyright Epic Games, Inc. All Rights Reserved.

#include "UncontrolledChangelistsModule.h"

#include "Algo/AnyOf.h"
#include "Algo/Copy.h"
#include "Algo/Find.h"
#include "Algo/ForEach.h"
#include "Algo/Transform.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "PackagesDialog.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "SourceControlOperations.h"
#include "Styling/SlateTypes.h"
#include "UObject/ObjectSaveContext.h"

#define LOCTEXT_NAMESPACE "UncontrolledChangelists"

static TAutoConsoleVariable<bool> CVarUncontrolledChangelistsEnable(
	TEXT("UncontrolledChangelists.Enable"),
	false,
	TEXT("Enables Uncontrolled Changelists (experimental).")
);

void FUncontrolledChangelistsModule::FStartupTask::DoWork()
{
	double StartTime = FPlatformTime::Seconds();
	UE_LOG(LogSourceControl, Log, TEXT("Uncontrolled asset enumeration started..."));

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::GetModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> Assets;
	const bool bIncludeOnlyOnDiskAssets = true;
	AssetRegistry.GetAllAssets(Assets, bIncludeOnlyOnDiskAssets);
	Algo::ForEach(Assets, [this](const struct FAssetData& AssetData) 
	{ 
		Owner->OnAssetAddedInternal(AssetData, AddedAssetsCache, true);
	});
	
	UE_LOG(LogSourceControl, Log, TEXT("Uncontrolled asset enumeration finished in %s seconds (Found %d uncontrolled assets)"), *FString::SanitizeFloat(FPlatformTime::Seconds() - StartTime), AddedAssetsCache.Num());
}

void FUncontrolledChangelistsModule::StartupModule()
{
	bIsEnabled = CVarUncontrolledChangelistsEnable.GetValueOnGameThread();
	if (!IsEnabled())
	{
		return;
	}

	// Adds Default Uncontrolled Changelist if it is not already present.
	FUncontrolledChangelist DefaultUncontrolledChangelist(FUncontrolledChangelist::DEFAULT_UNCONTROLLED_CHANGELIST_GUID, FUncontrolledChangelist::DEFAULT_UNCONTROLLED_CHANGELIST_NAME.ToString());
	UncontrolledChangelistsStateCache.FindOrAdd(MoveTemp(DefaultUncontrolledChangelist), MakeShareable(new FUncontrolledChangelistState(DefaultUncontrolledChangelist)));

	LoadState();

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::GetModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	OnAssetAddedDelegateHandle = AssetRegistry.OnAssetAdded().AddLambda([](const struct FAssetData& AssetData) { Get().OnAssetAdded(AssetData); });
	OnObjectPreSavedDelegateHandle = FCoreUObjectDelegates::OnObjectPreSave.AddLambda([](UObject* InAsset, const FObjectPreSaveContext& InPreSaveContext) { Get().OnObjectPreSaved(InAsset, InPreSaveContext); });
		
	StartupTask = MakeUnique<FAsyncTask<FStartupTask>>(this);		
	StartupTask->StartBackgroundTask();
}

void FUncontrolledChangelistsModule::ShutdownModule()
{
	FAssetRegistryModule* AssetRegistryModulePtr = static_cast<FAssetRegistryModule*>(FModuleManager::Get().GetModule(TEXT("AssetRegistry")));

	// Check in case AssetRegistry has already been shutdown.
	if (AssetRegistryModulePtr != nullptr)
	{
		AssetRegistryModulePtr->Get().OnAssetAdded().Remove(OnAssetAddedDelegateHandle);
	}

	FCoreUObjectDelegates::OnObjectPreSave.Remove(OnObjectPreSavedDelegateHandle);
}

bool FUncontrolledChangelistsModule::IsEnabled() const
{
	return bIsEnabled;
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

bool FUncontrolledChangelistsModule::OnMakeWritable(const TArray<FString>& InFilenames)
{
	bool bHasStateChanged = false;

	if (!IsEnabled())
	{
		return false;
	}

	FUncontrolledChangelist DefaultUncontrolledChangelist(FUncontrolledChangelist::DEFAULT_UNCONTROLLED_CHANGELIST_GUID, FUncontrolledChangelist::DEFAULT_UNCONTROLLED_CHANGELIST_NAME.ToString());
	FUncontrolledChangelistsStateCache::ValueType& UncontrolledChangelistState = UncontrolledChangelistsStateCache.FindOrAdd(MoveTemp(DefaultUncontrolledChangelist), MakeShareable(new FUncontrolledChangelistState(DefaultUncontrolledChangelist)));

	bHasStateChanged = UncontrolledChangelistState->AddFiles(InFilenames, FUncontrolledChangelistState::ECheckFlags::NotCheckedOut);

	if (bHasStateChanged)
	{
		OnStateChanged();
	}

	return bHasStateChanged;
}

void FUncontrolledChangelistsModule::UpdateStatus()
{
	bool bHasStateChanged = false;

	if (!IsEnabled())
	{
		return;
	}

	for (FUncontrolledChangelistsStateCache::ElementType& Pair : UncontrolledChangelistsStateCache)
	{
		FUncontrolledChangelistsStateCache::ValueType& UncontrolledChangelistState = Pair.Value;
		bHasStateChanged |= UncontrolledChangelistState->UpdateStatus();
	}

	if (bHasStateChanged)
	{
		OnStateChanged();
	}
}

FText FUncontrolledChangelistsModule::GetReconcileStatus() const
{
	if (StartupTask && !StartupTask->IsDone())
	{
		return LOCTEXT("ProcessingAssetsStatus", "Processing assets...");
	}

	return FText::Format(LOCTEXT("ReconcileStatus", "Assets to check for reconcile: {0}"), FText::AsNumber(AddedAssetsCache.Num()));
}

bool FUncontrolledChangelistsModule::OnReconcileAssets()
{
	FScopedSlowTask Scope(0, LOCTEXT("ProcessingAssetsProgress", "Processing assets"));
	const bool bShowCancelButton = false;
	const bool bAllowInPIE = false;
	Scope.MakeDialogDelayed(1.0f, bShowCancelButton, bAllowInPIE);

	if (StartupTask)
	{
		while (!StartupTask->WaitCompletionWithTimeout(0.016))
		{
			Scope.EnterProgressFrame(0.f);
		}
				
		AddedAssetsCache.Append(StartupTask->GetTask().GetAddedAssetsCache());
		StartupTask = nullptr;
	}

	if ((!IsEnabled()) || AddedAssetsCache.IsEmpty())
	{
		return false;
	}

	Scope.EnterProgressFrame(0.f, LOCTEXT("ReconcileAssetsProgress", "Reconciling assets"));

	CleanAssetsCaches();
	bool bHasStateChanged = AddFilesToDefaultUncontrolledChangelist(AddedAssetsCache.Array(), FUncontrolledChangelistState::ECheckFlags::All);

	AddedAssetsCache.Empty();

	return bHasStateChanged;
}

void FUncontrolledChangelistsModule::OnAssetAdded(const FAssetData& AssetData)
{
	if (!IsEnabled())
	{
		return;
	}

	OnAssetAddedInternal(AssetData, AddedAssetsCache, false);
}

void FUncontrolledChangelistsModule::OnAssetAddedInternal(const FAssetData& AssetData, TSet<FString>& InAddedAssetsCache, bool bInStartupTask)
{
	FPackagePath PackagePath;
	if (!FPackagePath::TryFromPackageName(AssetData.PackageName, PackagePath))
	{
		return;
	}

	// No need to check for existence when running startup task
	if (!bInStartupTask && !FPackageName::DoesPackageExist(PackagePath, &PackagePath))
	{
		return; // If the package does not exist on disk there is nothing more to do
	}

	const FString LocalFullPath(PackagePath.GetLocalFullPath());

	if (LocalFullPath.IsEmpty())
	{
		return;
	}

	FString Fullpath = FPaths::ConvertRelativePathToFull(LocalFullPath);

	if (Fullpath.IsEmpty())
	{
		return;
	}

	if (!IFileManager::Get().IsReadOnly(*Fullpath))
	{
		InAddedAssetsCache.Add(MoveTemp(Fullpath));
	}
}

void FUncontrolledChangelistsModule::OnObjectPreSaved(UObject* InAsset, const FObjectPreSaveContext& InPreSaveContext)
{
	if (!IsEnabled())
	{
		return;
	}

	FString Fullpath = FPaths::ConvertRelativePathToFull(InPreSaveContext.GetTargetFilename());

	if (Fullpath.IsEmpty())
	{
		return;
	}

	AddedAssetsCache.Add(MoveTemp(Fullpath));
}

void FUncontrolledChangelistsModule::MoveFilesToUncontrolledChangelist(const TArray<FSourceControlStateRef>& InControlledFileStates, const TArray<FSourceControlStateRef>& InUncontrolledFileStates, const FUncontrolledChangelist& InUncontrolledChangelist)
{
	bool bHasStateChanged = false;

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
	bHasStateChanged = (*ChangelistState)->AddFiles(Filenames, FUncontrolledChangelistState::ECheckFlags::None);

	if (bHasStateChanged)
	{
		OnStateChanged();
	}
}

void FUncontrolledChangelistsModule::MoveFilesToControlledChangelist(const TArray<FSourceControlStateRef>& InUncontrolledFileStates, const FSourceControlChangelistPtr& InChangelist)
{
	if (!IsEnabled())
	{
		return;
	}

	TArray<FString> UncontrolledFilenames;
	
	Algo::Transform(InUncontrolledFileStates, UncontrolledFilenames, [](const FSourceControlStateRef& State) { return State->GetFilename(); });
	MoveFilesToControlledChangelist(UncontrolledFilenames, InChangelist);
}

void FUncontrolledChangelistsModule::MoveFilesToControlledChangelist(const TArray<FString>& InUncontrolledFiles, const FSourceControlChangelistPtr& InChangelist)
{
	if (!IsEnabled())
	{
		return;
	}

	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
	TArray<FSourceControlStateRef> UpdatedFilestates;

	// Get updated filestates to check Checkout capabilities.
	SourceControlProvider.GetState(InUncontrolledFiles, UpdatedFilestates, EStateCacheUsage::ForceUpdate);

	TArray<UPackage*> PackageConflicts;
	TArray<FString> FilesToAdd;
	TArray<FString> FilesToCheckout;
	TArray<FString> FilesToDelete;

	// Check if we can Checkout files or mark for add
	for (const FSourceControlStateRef& Filestate : UpdatedFilestates)
	{
		if (!Filestate->IsSourceControlled())
		{
			FilesToAdd.Add(Filestate->GetFilename());
		}
		else if (!IFileManager::Get().FileExists(*Filestate->GetFilename()))
		{
			FilesToDelete.Add(Filestate->GetFilename());
		}
		else if (Filestate->CanCheckout())
		{
			FilesToCheckout.Add(Filestate->GetFilename());
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
		if (!FilesToCheckout.IsEmpty())
		{
			SourceControlProvider.Execute(ISourceControlOperation::Create<FCheckOut>(), InChangelist, FilesToCheckout);
		}

		if (!FilesToAdd.IsEmpty())
		{
			SourceControlProvider.Execute(ISourceControlOperation::Create<FMarkForAdd>(), InChangelist, FilesToAdd);
		}

		if (!FilesToDelete.IsEmpty())
		{
			SourceControlProvider.Execute(ISourceControlOperation::Create<FDelete>(), InChangelist, FilesToDelete);
		}

		// UpdateStatus so UncontrolledChangelists can remove files from their cache if they were present before checkout.
		UpdateStatus();
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

void FUncontrolledChangelistsModule::OnStateChanged()
{
	OnUncontrolledChangelistModuleChanged.Broadcast();
	SaveState();
}

void FUncontrolledChangelistsModule::CleanAssetsCaches()
{
	// Remove files we are already tracking in Uncontrolled Changelists
	for (FUncontrolledChangelistsStateCache::ElementType& Pair : UncontrolledChangelistsStateCache)
	{
		FUncontrolledChangelistsStateCache::ValueType& UncontrolledChangelistState = Pair.Value;
		UncontrolledChangelistState->RemoveDuplicates(AddedAssetsCache);
	}
}

bool FUncontrolledChangelistsModule::AddFilesToDefaultUncontrolledChangelist(const TArray<FString>& InFilenames, const FUncontrolledChangelistState::ECheckFlags InCheckFlags)
{
	bool bHasStateChanged = false;

	FUncontrolledChangelist DefaultUncontrolledChangelist(FUncontrolledChangelist::DEFAULT_UNCONTROLLED_CHANGELIST_GUID, FUncontrolledChangelist::DEFAULT_UNCONTROLLED_CHANGELIST_NAME.ToString());
	FUncontrolledChangelistsStateCache::ValueType& UncontrolledChangelistState = UncontrolledChangelistsStateCache.FindOrAdd(MoveTemp(DefaultUncontrolledChangelist), MakeShareable(new FUncontrolledChangelistState(DefaultUncontrolledChangelist)));

	// Try to add files, they will be added only if they pass the required checks
	bHasStateChanged = UncontrolledChangelistState->AddFiles(InFilenames, InCheckFlags);

	if (bHasStateChanged)
	{
		OnStateChanged();
	}

	return bHasStateChanged;
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

	UE_LOG(LogSourceControl, Display, TEXT("Uncontrolled Changelist persistency file loaded %s"), *GetPersistentFilePath());
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

IMPLEMENT_MODULE(FUncontrolledChangelistsModule, UncontrolledChangelists);

#undef LOCTEXT_NAMESPACE
