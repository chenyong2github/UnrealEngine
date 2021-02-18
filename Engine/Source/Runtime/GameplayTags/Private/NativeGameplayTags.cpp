// Copyright Epic Games, Inc. All Rights Reserved.

#include "NativeGameplayTags.h"
#include "Interfaces/IProjectManager.h"
#include "ModuleDescriptor.h"
#include "ProjectDescriptor.h"
#include "PluginDescriptor.h"
#include "Interfaces/IPluginManager.h"

#define LOCTEXT_NAMESPACE "FNativeGameplayTagSource"

FGameplayTag FNativeGameplayTagSource::Add(FName TagName, const FString& TagDevComment)
{
	if (TagName.IsNone())
	{
		return FGameplayTag();
	}

	FGameplayTag NewTag = FGameplayTag(TagName);
	NativeTags.Add(FGameplayTagTableRow(TagName, TagDevComment));

	return NewTag;
}

TSet<const class FNativeGameplayTag*> FNativeGameplayTag::RegisteredNativeTags;

static bool VerifyModuleCanContainGameplayTag(FName ModuleName, FName TagName, const FModuleDescriptor* Module)
{
	if (Module)
	{
		if (Module->Type == EHostType::ServerOnly || Module->Type == EHostType::ClientOnly || Module->Type == EHostType::ClientOnlyNoCommandlet)
		{
			ensureAlwaysMsgf(false, TEXT("Native Gameplay Tag '%s' defined in '%s', which is Client or Server only module.  Client and Server tags must match."), *TagName.ToString(), *ModuleName.ToString());
		}

		// Not a mistake - we return true even if it fails the test, the return value is an invalidation we were able to verify that it could or could not.
		return true;
	}

	return false;
}

FNativeGameplayTag::FNativeGameplayTag(FName PluginName, FName ModuleName, FName TagName, const FString& TagDevComment, ENativeGameplayTagToken)
{
#if !UE_BUILD_SHIPPING
	const FProjectDescriptor* const CurrentProject = IProjectManager::Get().GetCurrentProject();
	check(CurrentProject);

	const FModuleDescriptor* ProjectModule =
		CurrentProject->Modules.FindByPredicate([ModuleName](const FModuleDescriptor& Module) { return Module.Name == ModuleName; });

	if (!VerifyModuleCanContainGameplayTag(ModuleName, TagName, ProjectModule))
	{
		const FModuleDescriptor* PluginModule = nullptr;

		// Ok, so we're not in a module for the project, 
		TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName.ToString());
		if (Plugin.IsValid())
		{
			const FPluginDescriptor& PluginDescriptor = Plugin->GetDescriptor();
			PluginModule = PluginDescriptor.Modules.FindByPredicate([ModuleName](const FModuleDescriptor& Module) { return Module.Name == ModuleName; });
		}

		if (!VerifyModuleCanContainGameplayTag(ModuleName, TagName, PluginModule))
		{
			ensureAlwaysMsgf(false, TEXT("Unable to find information about module '%s' in plugin '%s'"), *ModuleName.ToString(), *PluginName.ToString());
		}
	}
#endif

	//TODO We could restrict creation to during module loading, at least in editor builds.

	InternalTag = TagName.IsNone() ? FGameplayTag() : FGameplayTag(TagName);
#if WITH_EDITOR
	DeveloperComment = TagDevComment;
#endif

	RegisteredNativeTags.Add(this);

	if (UGameplayTagsManager* Manager = UGameplayTagsManager::GetIfAllocated())
	{
		Manager->AddNativeGameplayTag(this, TagName, TagDevComment);
	}
}

FNativeGameplayTag::~FNativeGameplayTag()
{
	RegisteredNativeTags.Remove(this);

	if (UGameplayTagsManager* Manager = UGameplayTagsManager::GetIfAllocated())
	{
		Manager->RemoveNativeGameplayTag(this);
	}
}

#undef LOCTEXT_NAMESPACE
