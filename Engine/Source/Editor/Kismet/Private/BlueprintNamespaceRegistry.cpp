// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintNamespaceRegistry.h"
#include "BlueprintNamespacePathTree.h"
#include "BlueprintNamespaceUtilities.h"
#include "UObject/UObjectIterator.h"
#include "HAL/IConsoleManager.h"
#include "AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/UserDefinedEnum.h"
#include "Engine/UserDefinedStruct.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EdGraphSchema_K2.h"

DEFINE_LOG_CATEGORY_STATIC(LogNamespace, Log, All);

FBlueprintNamespaceRegistry::FBlueprintNamespaceRegistry()
	: bIsInitialized(false)
{
	IConsoleManager::Get().RegisterConsoleCommand
	(
		TEXT("BP.ToggleUsePackagePathAsDefaultNamespace"),
		TEXT("Toggle the use of a type's package path as its default namespace when not explicitly assigned. Otherwise, all types default to the global namespace."),
		FConsoleCommandDelegate::CreateRaw(this, &FBlueprintNamespaceRegistry::ToggleDefaultNamespace),
		ECVF_Default
	);

	IConsoleManager::Get().RegisterConsoleCommand
	(
		TEXT("BP.DumpAllRegisteredNamespacePaths"),
		TEXT("Dumps all registered namespace paths."),
		FConsoleCommandDelegate::CreateRaw(this, &FBlueprintNamespaceRegistry::DumpAllRegisteredPaths),
		ECVF_Default
	);
}

FBlueprintNamespaceRegistry::~FBlueprintNamespaceRegistry()
{
	Shutdown();
}

void FBlueprintNamespaceRegistry::Initialize()
{
	if (bIsInitialized)
	{
		return;
	}

	PathTree = MakeUnique<FBlueprintNamespacePathTree>();

	// Skip namespace harvesting if we're not inside an interactive editor context.
	if(GIsEditor && !IsRunningCommandlet())
	{
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		OnAssetAddedDelegateHandle = AssetRegistry.OnAssetAdded().AddRaw(this, &FBlueprintNamespaceRegistry::OnAssetAdded);
		OnAssetRemovedDelegateHandle = AssetRegistry.OnAssetRemoved().AddRaw(this, &FBlueprintNamespaceRegistry::OnAssetRemoved);
		OnAssetRenamedDelegateHandle = AssetRegistry.OnAssetRenamed().AddRaw(this, &FBlueprintNamespaceRegistry::OnAssetRenamed);

		FindAndRegisterAllNamespaces();

		OnDefaultNamespaceTypeChangedDelegateHandle = FBlueprintNamespaceUtilities::OnDefaultBlueprintNamespaceTypeChanged().AddRaw(this, &FBlueprintNamespaceRegistry::OnDefaultNamespaceTypeChanged);
	}

	bIsInitialized = true;
}

void FBlueprintNamespaceRegistry::Shutdown()
{
	if (!bIsInitialized)
	{
		return;
	}

	FBlueprintNamespaceUtilities::OnDefaultBlueprintNamespaceTypeChanged().Remove(OnDefaultNamespaceTypeChangedDelegateHandle);

	if (FModuleManager::Get().IsModuleLoaded(TEXT("AssetRegistry")))
	{
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		AssetRegistry.OnAssetAdded().Remove(OnAssetAddedDelegateHandle);
		AssetRegistry.OnAssetRemoved().Remove(OnAssetRemovedDelegateHandle);
		AssetRegistry.OnAssetRenamed().Remove(OnAssetRenamedDelegateHandle);
	}

	bIsInitialized = false;
}

void FBlueprintNamespaceRegistry::OnAssetAdded(const FAssetData& AssetData)
{
	if(const UClass* AssetClass = AssetData.GetClass())
	{
		if (AssetClass->IsChildOf<UBlueprint>()
			|| AssetClass->IsChildOf<UBlueprintGeneratedClass>()
			|| AssetClass->IsChildOf<UUserDefinedEnum>()
			|| AssetClass->IsChildOf<UUserDefinedStruct>()
			|| AssetClass->IsChildOf<UBlueprintFunctionLibrary>())
		{
			RegisterNamespace(AssetData);
		}
	}
}

void FBlueprintNamespaceRegistry::OnAssetRemoved(const FAssetData& AssetData)
{
	// @todo_namespaces - Handle Blueprint asset removal.
}

void FBlueprintNamespaceRegistry::OnAssetRenamed(const FAssetData& AssetData, const FString& InOldName)
{
	// @todo_namespaces - Handle Blueprint asset rename/relocation.
}

bool FBlueprintNamespaceRegistry::IsRegisteredPath(const FString& InPath) const
{
	TSharedPtr<FBlueprintNamespacePathTree::FNode> Node = PathTree->FindPathNode(InPath);
	return Node.IsValid() && Node->bIsAddedPath;
}

void FBlueprintNamespaceRegistry::GetNamesUnderPath(const FString& InPath, TArray<FName>& OutNames) const
{
	TSharedPtr<FBlueprintNamespacePathTree::FNode> Node = PathTree->FindPathNode(InPath);
	if (Node.IsValid())
	{
		for (auto ChildIt = Node->Children.CreateConstIterator(); ChildIt; ++ChildIt)
		{
			OutNames.Add(ChildIt.Key());
		}
	}
}

void FBlueprintNamespaceRegistry::GetAllRegisteredPaths(TArray<FString>& OutPaths) const
{
	PathTree->ForeachNode([&OutPaths](const TArray<FName>& CurrentPath, TSharedRef<FBlueprintNamespacePathTree::FNode> Node)
	{
		if (Node->bIsAddedPath)
		{
			// Note: This is not a hard limit on namespace path identifier string length, it's an optimization to try and avoid reallocation during path construction.
			TStringBuilder<128> PathBuilder;
			for (FName PathSegment : CurrentPath)
			{
				if (PathBuilder.Len() > 0)
				{
					PathBuilder += TEXT(".");
				}
				PathBuilder += PathSegment.ToString();
			}

			FString FullPath = PathBuilder.ToString();
			OutPaths.Add(MoveTemp(FullPath));
		}
	});
}

void FBlueprintNamespaceRegistry::FindAndRegisterAllNamespaces()
{
	// Register loaded class type namespace identifiers.
	for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
	{
		const UClass* ClassObject = *ClassIt;
		if (UEdGraphSchema_K2::IsAllowableBlueprintVariableType(ClassObject))
		{
			RegisterNamespace(ClassObject);
		}
	}

	// Register loaded struct type namespace identifiers.
	for (TObjectIterator<UScriptStruct> StructIt; StructIt; ++StructIt)
	{
		const UScriptStruct* StructObject = *StructIt;
		if (UEdGraphSchema_K2::IsAllowableBlueprintVariableType(StructObject))
		{
			RegisterNamespace(StructObject);
		}
	}

	// Register loaded enum type namespace identifiers.
	for (TObjectIterator<UEnum> EnumIt; EnumIt; ++EnumIt)
	{
		const UEnum* EnumObject = *EnumIt;
		if (UEdGraphSchema_K2::IsAllowableBlueprintVariableType(EnumObject))
		{
			RegisterNamespace(EnumObject);
		}
	}

	// Register loaded function library namespace identifiers.
	for (TObjectIterator<UBlueprintFunctionLibrary> LibraryIt; LibraryIt; ++LibraryIt)
	{
		const UBlueprintFunctionLibrary* LibraryObject = *LibraryIt;
		if (LibraryObject)
		{
			RegisterNamespace(LibraryObject);
		}
	}

	FARFilter ClassFilter;
	ClassFilter.ClassNames.Add(UBlueprint::StaticClass()->GetFName());
	ClassFilter.ClassNames.Add(UBlueprintGeneratedClass::StaticClass()->GetFName());
	ClassFilter.ClassNames.Add(UUserDefinedStruct::StaticClass()->GetFName());
	ClassFilter.ClassNames.Add(UUserDefinedEnum::StaticClass()->GetFName());
	ClassFilter.ClassNames.Add(UBlueprintFunctionLibrary::StaticClass()->GetFName());
	ClassFilter.bRecursiveClasses = true;

	// Register unloaded type namespace identifiers.
	TArray<FAssetData> BlueprintAssets;
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	AssetRegistry.GetAssets(ClassFilter, BlueprintAssets);
	for (const FAssetData& BlueprintAsset : BlueprintAssets)
	{
		if (!BlueprintAsset.IsAssetLoaded())
		{
			RegisterNamespace(BlueprintAsset);
		}
	}
}

void FBlueprintNamespaceRegistry::RegisterNamespace(const FString& InPath)
{
	if (!InPath.IsEmpty())
	{
		PathTree->AddPath(InPath);
	}
}

void FBlueprintNamespaceRegistry::RegisterNamespace(const UObject* InObject)
{
	FString ObjectNamespace = FBlueprintNamespaceUtilities::GetObjectNamespace(InObject);
	RegisterNamespace(ObjectNamespace);
}

void FBlueprintNamespaceRegistry::RegisterNamespace(const FAssetData& AssetData)
{
	FString AssetNamespace = FBlueprintNamespaceUtilities::GetAssetNamespace(AssetData);
	RegisterNamespace(AssetNamespace);
}

void FBlueprintNamespaceRegistry::ToggleDefaultNamespace()
{
	const EDefaultBlueprintNamespaceType OldType = FBlueprintNamespaceUtilities::GetDefaultBlueprintNamespaceType();
	if (OldType == EDefaultBlueprintNamespaceType::DefaultToGlobalNamespace)
	{
		FBlueprintNamespaceUtilities::SetDefaultBlueprintNamespaceType(EDefaultBlueprintNamespaceType::UsePackagePathAsDefaultNamespace);
	}
	else if(OldType == EDefaultBlueprintNamespaceType::UsePackagePathAsDefaultNamespace)
	{
		FBlueprintNamespaceUtilities::SetDefaultBlueprintNamespaceType(EDefaultBlueprintNamespaceType::DefaultToGlobalNamespace);
	}
}

void FBlueprintNamespaceRegistry::DumpAllRegisteredPaths()
{
	if (!bIsInitialized)
	{
		Initialize();
	}

	TArray<FString> AllPaths;
	GetAllRegisteredPaths(AllPaths);

	UE_LOG(LogNamespace, Log, TEXT("=== Registered Blueprint namespace paths:"));

	for (const FString& Path : AllPaths)
	{
		UE_LOG(LogNamespace, Log, TEXT("%s"), *Path);
	}

	UE_LOG(LogNamespace, Log, TEXT("=== (end) %d total paths ==="), AllPaths.Num());
}

void FBlueprintNamespaceRegistry::OnDefaultNamespaceTypeChanged()
{
	// Rebuild the registry to reflect the appropriate default namespace identifiers for all known types.
	PathTree = MakeUnique<FBlueprintNamespacePathTree>();
	FindAndRegisterAllNamespaces();
}

FBlueprintNamespaceRegistry& FBlueprintNamespaceRegistry::Get()
{
	static FBlueprintNamespaceRegistry* Singleton = new FBlueprintNamespaceRegistry();
	return *Singleton;
}