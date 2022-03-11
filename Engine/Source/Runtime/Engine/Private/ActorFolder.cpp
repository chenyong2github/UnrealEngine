// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
 * UActorFolder implementation
 *=============================================================================*/

#include "ActorFolder.h"

#if WITH_EDITOR

#include "Engine/World.h"
#include "EditorActorFolders.h"
#include "ExternalPackageHelper.h"
#include "AssetRegistryModule.h"
#include "IAssetRegistry.h"
#include "ActorFolderDesc.h"

UActorFolder* UActorFolder::Create(ULevel* InLevel, const FString& InFolderLabel, UActorFolder* InParent)
{
	const FGuid NewFolderGuid = FGuid::NewGuid();

	// We generate a globally unique name to avoid any potential clash of 2 users creating the same folder
	FString FolderShortName = UActorFolder::StaticClass()->GetName() + TEXT("_UID_") + NewFolderGuid.ToString(EGuidFormats::UniqueObjectGuid);
	TStringBuilderWithBuffer<TCHAR, NAME_SIZE> GloballyUniqueObjectPath;
	GloballyUniqueObjectPath += InLevel->GetPathName();
	GloballyUniqueObjectPath += TEXT(".");
	GloballyUniqueObjectPath += FolderShortName;

	const bool bIsTransientFolder = (InLevel->IsInstancedLevel() && !InLevel->IsPersistentLevel()) || InLevel->GetPackage()->HasAnyPackageFlags(PKG_PlayInEditor);
	const bool bUseExternalObject = InLevel->IsUsingExternalObjects() && !bIsTransientFolder;
	const bool bShouldDirtyLevel = !bUseExternalObject;
	const EObjectFlags Flags = (bIsTransientFolder ? RF_Transient : RF_NoFlags) | RF_Transactional;

	UPackage* ExternalPackage = bUseExternalObject ? FExternalPackageHelper::CreateExternalPackage(InLevel, *GloballyUniqueObjectPath, GetExternalPackageFlags()) : nullptr;

	UActorFolder* ActorFolder = NewObject<UActorFolder>(InLevel, UActorFolder::StaticClass(), FName(FolderShortName), Flags, nullptr, /*bCopyTransientsFromClassDefaults*/false, /*InstanceGraph*/nullptr, ExternalPackage);
	check(ActorFolder);
	ActorFolder->FolderGuid = NewFolderGuid;
	ActorFolder->SetLabel(InFolderLabel);
	ActorFolder->SetParent(InParent);
	ActorFolder->SetIsInitiallyExpanded(true);

	FLevelActorFoldersHelper::AddActorFolder(InLevel, ActorFolder, bShouldDirtyLevel);
	return ActorFolder;
}

bool UActorFolder::IsAsset() const
{
	// Actor Folders are considered assets to allow using the asset logic for save dialogs, etc.
	// Also, they return true even if pending kill, in order to show up as deleted in these dialogs.
	return IsPackageExternal() && !GetPackage()->HasAnyFlags(RF_Transient) && !HasAnyFlags(RF_Transient | RF_ClassDefaultObject);
}

namespace ActorFolder
{
	static const FName NAME_FolderGuid(TEXT("FolderGuid"));
	static const FName NAME_ParentFolderGuid(TEXT("ParentFolderGuid"));
	static const FName NAME_FolderLabel(TEXT("FolderLabel"));
	static const FName NAME_FolderInitiallyExpanded(TEXT("FolderInitiallyExpanded"));
	static const FName NAME_FolderIsDeleted(TEXT("FolderIsDeleted"));
	static const FName NAME_OuterPackageName(TEXT("OuterPackageName"));
};

void UActorFolder::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	OutTags.Add(FAssetRegistryTag(ActorFolder::NAME_FolderGuid, *FolderGuid.ToString(), FAssetRegistryTag::TT_Hidden));
	OutTags.Add(FAssetRegistryTag(ActorFolder::NAME_ParentFolderGuid, *ParentFolderGuid.ToString(), FAssetRegistryTag::TT_Hidden));
	OutTags.Add(FAssetRegistryTag(ActorFolder::NAME_FolderLabel, *FolderLabel, FAssetRegistryTag::TT_Hidden));
	OutTags.Add(FAssetRegistryTag(ActorFolder::NAME_FolderInitiallyExpanded, bFolderInitiallyExpanded ? TEXT("1") : TEXT("0"), FAssetRegistryTag::TT_Hidden));
	OutTags.Add(FAssetRegistryTag(ActorFolder::NAME_FolderIsDeleted, bIsDeleted ? TEXT("1") : TEXT("0"), FAssetRegistryTag::TT_Hidden));
	OutTags.Add(FAssetRegistryTag(ActorFolder::NAME_OuterPackageName, *GetOuterULevel()->GetPackage()->GetName(), FAssetRegistryTag::TT_Hidden));
}

FActorFolderDesc UActorFolder::GetAssetRegistryInfoFromPackage(FName ActorFolderPackageName)
{
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	FActorFolderDesc ActorFolderDesc;
	TArray<FAssetData> Assets;
	AssetRegistry.GetAssetsByPackageName(ActorFolderPackageName, Assets, true);
	check(Assets.Num() <= 1);
	if (Assets.Num() == 1)
	{
		const FAssetData& Asset = Assets[0];
		static const FName NAME_ActorFolder(TEXT("ActorFolder"));
		check(Asset.AssetClass == NAME_ActorFolder);
		{
			FString Value;
			if (Asset.GetTagValue(ActorFolder::NAME_FolderGuid, Value))
			{
				FGuid::Parse(Value, ActorFolderDesc.FolderGuid);
			}
			if (Asset.GetTagValue(ActorFolder::NAME_ParentFolderGuid, Value))
			{
				FGuid::Parse(Value, ActorFolderDesc.ParentFolderGuid);
			}
			if (Asset.GetTagValue(ActorFolder::NAME_FolderLabel, Value))
			{
				ActorFolderDesc.FolderLabel = Value;
			}
			if (Asset.GetTagValue(ActorFolder::NAME_FolderInitiallyExpanded, Value))
			{
				ActorFolderDesc.bFolderInitiallyExpanded = (Value == TEXT("1"));
			}
			if (Asset.GetTagValue(ActorFolder::NAME_FolderIsDeleted, Value))
			{
				ActorFolderDesc.bFolderIsDeleted = (Value == TEXT("1"));
			}
			if (Asset.GetTagValue(ActorFolder::NAME_OuterPackageName, Value))
			{
				ActorFolderDesc.OuterPackageName = Value;
			}
		}
	}
	return ActorFolderDesc;
}

void UActorFolder::SetLabel(const FString& InFolderLabel)
{
	check(IsValid());
	if (!FolderLabel.Equals(InFolderLabel, ESearchCase::CaseSensitive))
	{
		Modify();
		FolderLabel = InFolderLabel;
	}
}

void UActorFolder::SetIsInitiallyExpanded(bool bInFolderInitiallyExpanded)
{
	check(IsValid());
	if (bFolderInitiallyExpanded != bInFolderInitiallyExpanded)
	{
		Modify();
		bFolderInitiallyExpanded = bInFolderInitiallyExpanded;
	}
}

void UActorFolder::SetParent(UActorFolder* InParent)
{
	if ((this != InParent) && (GetParent() != InParent))
	{
		Modify();
		ParentFolderGuid = InParent ? InParent->GetGuid() : FGuid();
	}
}

FString UActorFolder::GetDisplayName() const
{
	if (IsMarkedAsDeleted())
	{
		return FString::Printf(TEXT("<Deleted> %s"), *GetLabel());
	}
	return GetPath().ToString();
}

void UActorFolder::MarkAsDeleted()
{
	Modify();

	// Deleting a folder must not modify actors part of it nor sub folders.
	// Here, we simply mark the folder as deleted. 
	// When marked as deleted, the folder will act as a redirector to its parent.
	check(!bIsDeleted);
	bIsDeleted = true;
}

UActorFolder* UActorFolder::GetParent() const
{
	UActorFolder* Parent = ParentFolderGuid.IsValid() ? GetOuterULevel()->GetActorFolder(ParentFolderGuid) : nullptr;
	if (Parent)
	{
		if (!Parent->IsValid())
		{
			return Parent->GetParent();
		}
	}
	return Parent;
}

FName UActorFolder::GetPath() const
{
	if (!FFolder::GetOptionalFolderRootObject(GetOuterULevel()))
	{
		return NAME_None;
	}

	TStringBuilder<1024> StringBuilder;
	if (IsValid())
	{
		StringBuilder += FolderLabel;
	}
	UActorFolder* Parent = GetParent();
	while (Parent)
	{
		StringBuilder.Prepend(TEXT("/"));
		StringBuilder.Prepend(Parent->FolderLabel);
		Parent = Parent->GetParent();
	}
	return FName(*StringBuilder);
}

void UActorFolder::FixupParentFolder()
{
	if (ParentFolderGuid.IsValid() && !GetOuterULevel()->GetActorFolder(ParentFolderGuid, /*bSkipDeleted*/ false))
	{
		UE_LOG(LogLevel, Warning, TEXT("Missing parent actor folder for actor folder %s (%s)"), *FolderLabel, *GetName());
		Modify();
		ParentFolderGuid.Invalidate();
	}
}

void UActorFolder::Fixup()
{
	if (!IsMarkedAsDeleted() && ParentFolderGuid.IsValid())
	{
		UActorFolder* Parent = GetParent();
		FGuid ValidParentFolderGuid = Parent ? Parent->GetGuid() : FGuid();
		if (ParentFolderGuid != ValidParentFolderGuid)
		{
			Modify();
			ParentFolderGuid = ValidParentFolderGuid;
		}
	}
}

FFolder UActorFolder::GetFolder() const
{
	return FFolder(GetPath(), FFolder::GetOptionalFolderRootObject(GetOuterULevel()).Get(FFolder::GetDefaultRootObject()));
}

void UActorFolder::SetPackageExternal(bool bInExternal, bool bShouldDirty)
{
	FExternalPackageHelper::SetPackagingMode(this, GetOuterULevel(), bInExternal, bShouldDirty, GetExternalPackageFlags());
}

#endif