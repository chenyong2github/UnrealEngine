// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commandlets/NiagaraSystemAuditCommandlet.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Misc/PackageName.h"
#include "AssetData.h"
#include "ARFilter.h"
#include "AssetRegistryModule.h"
#include "CollectionManagerTypes.h"
#include "ICollectionManager.h"
#include "CollectionManagerModule.h"

#include "NiagaraSystem.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraLightRendererProperties.h"

DEFINE_LOG_CATEGORY_STATIC(LogNiagaraSystemAuditCommandlet, Log, All);

UNiagaraSystemAuditCommandlet::UNiagaraSystemAuditCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

int32 UNiagaraSystemAuditCommandlet::Main(const FString& Params)
{
	if (!FParse::Value(*Params, TEXT("AuditOutputFolder="), AuditOutputFolder))
	{
		// No output folder specified. Use the default folder.
		AuditOutputFolder = FPaths::ProjectSavedDir() / TEXT("Audit");
	}

	// Add a timestamp to the folder
	AuditOutputFolder /= FDateTime::Now().ToString();

	FParse::Value(*Params, TEXT("FilterCollection="), FilterCollection);

	// User Data Interfaces to Find
	{
		FString UserDataInterfacesToFindString;
		if (FParse::Value(*Params, TEXT("UserDataInterfacesToFind="), UserDataInterfacesToFindString, false))
		{
			TArray<FString> DataInterfaceNames;
			UserDataInterfacesToFindString.ParseIntoArray(DataInterfaceNames, TEXT(","));
			for (const FString& DIName : DataInterfaceNames)
			{
				if (UClass* FoundClass = FindObject<UClass>(ANY_PACKAGE, *DIName, true))
				{
					UserDataInterfacesToFind.Add(FoundClass);
				}
				else
				{
					UE_LOG(LogNiagaraSystemAuditCommandlet, Warning, TEXT("DataInterace %s was not found so will not be searched"), *DIName);
				}
			}
		}
	}

	// Package Paths
	FString PackagePathsString;
	if (FParse::Value(*Params, TEXT("PackagePaths="), PackagePathsString, false))
	{
		TArray<FString> PackagePathsStrings;
		PackagePathsString.ParseIntoArray(PackagePathsStrings, TEXT(","));
		for (const FString& v : PackagePathsStrings)
		{
			PackagePaths.Add(FName(v));
		}
	}

	if (PackagePaths.Num() == 0)
	{
		PackagePaths.Add(FName(TEXT("/Game")));
	}

	ProcessNiagaraSystems();
	DumpResults();

	return 0;
}

bool UNiagaraSystemAuditCommandlet::ProcessNiagaraSystems()
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	AssetRegistry.SearchAllAssets(true);

	FARFilter Filter;
	Filter.PackagePaths = PackagePaths;
	Filter.bRecursivePaths = true;

	Filter.ClassNames.Add(UNiagaraSystem::StaticClass()->GetFName());
	if (!FilterCollection.IsEmpty())
	{
		FCollectionManagerModule& CollectionManagerModule = FCollectionManagerModule::GetModule();
		CollectionManagerModule.Get().GetObjectsInCollection(FName(*FilterCollection), ECollectionShareType::CST_All, Filter.ObjectPaths, ECollectionRecursionFlags::SelfAndChildren);
	}

	TArray<FAssetData> AssetList;
	AssetRegistry.GetAssets(Filter, AssetList);

	const double StartProcessNiagaraSystemsTime = FPlatformTime::Seconds();

	//  Iterate over all systems
	const FString DevelopersFolder = FPackageName::FilenameToLongPackageName(FPaths::GameDevelopersDir().LeftChop(1));
	FString LastPackageName = TEXT("");
	int32 PackageSwitches = 0;
	UPackage* CurrentPackage = nullptr;
	for (const FAssetData& AssetIt : AssetList)
	{
		const FString SystemName = AssetIt.ObjectPath.ToString();
		const FString PackageName = AssetIt.PackageName.ToString();

		if (PackageName.StartsWith(DevelopersFolder))
		{
			// Skip developer folders
			continue;
		}

		if (PackageName != LastPackageName)
		{
			UPackage* Package = ::LoadPackage(nullptr, *PackageName, LOAD_None);
			if (Package != nullptr)
			{
				LastPackageName = PackageName;
				Package->FullyLoad();
				CurrentPackage = Package;
			}
			else
			{
				UE_LOG(LogNiagaraSystemAuditCommandlet, Warning, TEXT("Failed to load package %s processing %s"), *PackageName, *SystemName);
				CurrentPackage = nullptr;
			}
		}

		const FString ShorterSystemName = AssetIt.AssetName.ToString();
		UNiagaraSystem* NiagaraSystem = FindObject<UNiagaraSystem>(CurrentPackage, *ShorterSystemName);
		if (NiagaraSystem == nullptr)
		{
			UE_LOG(LogNiagaraSystemAuditCommandlet, Warning, TEXT("Failed to load Niagara system %s"), *SystemName);
			continue;
		}

		// Iterate over all data interfaces used by the system / emitters
		TSet<FName> SystemDataInterfacesWihPrereqs;
		TSet<FName> SystemUserDataInterfaces;
		for (UNiagaraDataInterface* DataInterface : GetDataInterfaces(NiagaraSystem))
		{
			if (DataInterface->HasTickGroupPrereqs())
			{
				SystemDataInterfacesWihPrereqs.Add(DataInterface->GetClass()->GetFName());
			}
			if (UserDataInterfacesToFind.Contains(DataInterface->GetClass()))
			{
				SystemUserDataInterfaces.Add(DataInterface->GetClass()->GetFName());
			}
		}

		// Iterate over all emitters
		bool bHasLights = false;
		bool bHasGPUEmitters = false;
		bool bHasEvents = false;

		for (const FNiagaraEmitterHandle& EmitterHandle : NiagaraSystem->GetEmitterHandles())
		{
			UNiagaraEmitter* NiagaraEmitter = EmitterHandle.GetInstance();
			if (NiagaraEmitter == nullptr)
			{
				continue;
			}

			bHasGPUEmitters |= NiagaraEmitter->SimTarget == ENiagaraSimTarget::GPUComputeSim;

			bHasEvents |= NiagaraEmitter->GetEventHandlers().Num() > 0;

			for (UNiagaraRendererProperties* RendererProperties : NiagaraEmitter->GetRenderers())
			{
				if (UNiagaraLightRendererProperties* LightRendererProperties = Cast< UNiagaraLightRendererProperties>(RendererProperties))
				{
					bHasLights = true;
				}
			}
		}

		// Add to different charts we will write out
		if (NiagaraSystem->GetWarmupTime() > 0.0f)
		{
			NiagaraSystemsWithWarmup.Add(FString::Printf(TEXT("%s,%f"), *NiagaraSystem->GetPathName(), NiagaraSystem->GetWarmupTime()));
		}

		if (bHasLights)
		{
			NiagaraSystemsWithLights.Add(NiagaraSystem->GetPathName());
		}

		if (bHasGPUEmitters)
		{
			NiagaraSystemsWithGPUEmitters.Add(NiagaraSystem->GetPathName());
		}
		if (bHasEvents)
		{
			NiagaraSystemsWithEvents.Add(NiagaraSystem->GetPathName());
		}
		if (SystemDataInterfacesWihPrereqs.Num() > 0)
		{
			FString DataInterfaceNames;
			for (auto it = SystemDataInterfacesWihPrereqs.CreateConstIterator(); it; ++it)
			{
				if (!DataInterfaceNames.IsEmpty())
				{
					DataInterfaceNames.AppendChar(TEXT(' '));
				}
				DataInterfaceNames.Append(*it->ToString());
			}
			NiagaraSystemsWithPrerequisites.Add(FString::Printf(TEXT("%s,%s"), *NiagaraSystem->GetPathName(), *DataInterfaceNames));
		}
		if (SystemUserDataInterfaces.Num() > 0)
		{
			FString DataInterfaceNames;
			for (auto it = SystemUserDataInterfaces.CreateConstIterator(); it; ++it)
			{
				if (!DataInterfaceNames.IsEmpty())
				{
					DataInterfaceNames.AppendChar(TEXT(' '));
				}
				DataInterfaceNames.Append(*it->ToString());
			}
			NiagaraSystemsWithUserDataInterface.Add(FString::Printf(TEXT("%s,%s"), *NiagaraSystem->GetPathName(), *DataInterfaceNames));
		}
	}

	// Probably don't need to do this, but just in case we have any 'hanging' packages 
	// and more processing steps are added later, let's clean up everything...
	::CollectGarbage(RF_NoFlags);

	double ProcessNiagaraSystemsTime = FPlatformTime::Seconds() - StartProcessNiagaraSystemsTime;
	UE_LOG(LogNiagaraSystemAuditCommandlet, Log, TEXT("Took %5.3f seconds to process referenced Niagara systems..."), ProcessNiagaraSystemsTime);

	return true;
}

/** Dump the results of the audit */
void UNiagaraSystemAuditCommandlet::DumpResults()
{
	// Dump all the simple mappings...
	DumpSimpleSet(NiagaraSystemsWithWarmup, TEXT("NiagaraSystemsWithWarmup"), TEXT("Name,WarmupTime"));
	DumpSimpleSet(NiagaraSystemsWithLights, TEXT("NiagaraSystemsWithLights"), TEXT("Name"));
	DumpSimpleSet(NiagaraSystemsWithGPUEmitters, TEXT("NiagaraSystemsWithGPUEmitters"), TEXT("Name"));
	DumpSimpleSet(NiagaraSystemsWithEvents, TEXT("NiagaraSystemsWithEvents"), TEXT("Name"));
	DumpSimpleSet(NiagaraSystemsWithPrerequisites, TEXT("NiagaraSystemsWithPrerequisites"), TEXT("Name,DataInterface"));
	if (UserDataInterfacesToFind.Num() > 0)
	{
		DumpSimpleSet(NiagaraSystemsWithUserDataInterface, TEXT("NiagaraSystemsWithUserDataInterface"), TEXT("Name,DataInterface"));
	}
}

bool UNiagaraSystemAuditCommandlet::DumpSimpleSet(TSet<FString>& InSet, const TCHAR* InShortFilename, const TCHAR* OptionalHeader)
{
	if (InSet.Num() > 0)
	{
		check(InShortFilename != NULL);

		FArchive* OutputStream = GetOutputFile(InShortFilename);
		if (OutputStream != NULL)
		{
			UE_LOG(LogNiagaraSystemAuditCommandlet, Log, TEXT("Dumping '%s' results..."), InShortFilename);
			if (OptionalHeader != nullptr)
			{
				OutputStream->Logf(TEXT("%s"), OptionalHeader);
			}

			for (TSet<FString>::TIterator DumpIt(InSet); DumpIt; ++DumpIt)
			{
				FString ObjName = *DumpIt;
				OutputStream->Logf(TEXT("%s"), *ObjName);
			}

			OutputStream->Close();
			delete OutputStream;
		}
		else
		{
			return false;
		}
	}
	return true;
}

FArchive* UNiagaraSystemAuditCommandlet::GetOutputFile(const TCHAR* InShortFilename)
{
	const FString Filename = FString::Printf(TEXT("%s/%s.csv"), *AuditOutputFolder, InShortFilename);
	FArchive* OutputStream = IFileManager::Get().CreateDebugFileWriter(*Filename);
	if (OutputStream == NULL)
	{
		UE_LOG(LogNiagaraSystemAuditCommandlet, Warning, TEXT("Failed to create output stream %s"), *Filename);
	}
	return OutputStream;
}

TArray<class UNiagaraDataInterface*> UNiagaraSystemAuditCommandlet::GetDataInterfaces(class UNiagaraSystem* NiagaraSystem)
{
	TArray<UNiagaraDataInterface*> DataInterfaces;
	for (UNiagaraDataInterface* ParamDI : NiagaraSystem->GetExposedParameters().GetDataInterfaces())
	{
		if (ParamDI != nullptr)
		{
			DataInterfaces.AddUnique(ParamDI);
		}
	}

	auto GatherScriptDIs =
		[&](UNiagaraScript* NiagaraScript)
		{
			for (const FNiagaraScriptDataInterfaceInfo& DataInterfaceInfo : NiagaraScript->GetCachedDefaultDataInterfaces())
			{
				if ( UNiagaraDataInterface* ScriptDI = DataInterfaceInfo.DataInterface )
				{
					DataInterfaces.AddUnique(ScriptDI);
				}
			}
		};

	GatherScriptDIs(NiagaraSystem->GetSystemSpawnScript());
	GatherScriptDIs(NiagaraSystem->GetSystemUpdateScript());

	for (const FNiagaraEmitterHandle& EmitterHandle : NiagaraSystem->GetEmitterHandles())
	{
		UNiagaraEmitter* NiagaraEmitter = EmitterHandle.GetInstance();
		if (NiagaraEmitter == nullptr)
		{
			continue;
		}

		TArray<UNiagaraScript*> EmitterScripts;
		NiagaraEmitter->GetScripts(EmitterScripts);
		for (UNiagaraScript* Script : EmitterScripts)
		{
			GatherScriptDIs(Script);
		}
	}
	return DataInterfaces;
}
