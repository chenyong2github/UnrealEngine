// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Features/IModularFeature.h"
#include "ModuleDescriptor.h"

struct FSlateDynamicImageBrush;
class IPlugin;

/**
 * Description of a plugin template
 */
struct FPluginTemplateDescription
{
	/** Name of this template in the GUI */
	FText Name;

	/** Description of this template in the GUI */
	FText Description;

	/** Path to the directory containing template files */
	FString OnDiskPath;

	/** Brush resource for the image that is dynamically loaded */
	TSharedPtr< FSlateDynamicImageBrush > PluginIconDynamicImageBrush;

	/** Sorting priority (higer values go to the top of the list) */
	int32 SortPriority = 0;

	/** Can the plugin contain content? */
	bool bCanContainContent = false;

	/** Can the plugin be in the engine folder? */
	bool bCanBePlacedInEngine = true;

	/** What is the expected ModuleDescriptor type for this plugin? */
	EHostType::Type ModuleDescriptorType;

	/** What is the expected Loading Phase for this plugin? */
	ELoadingPhase::Type LoadingPhase;

	/** Called after the plugin has been created */
	virtual void OnPluginCreated(TSharedPtr<IPlugin> NewPlugin) {}

	/** Called to perform *additional* path validation when the path is modified (the bCanBePlacedInEngine validation will have already occurred and passed by this point) */
	virtual bool ValidatePathForPlugin(const FString& ProposedAbsolutePluginPath, FText& OutErrorMessage)
	{
		OutErrorMessage = FText::GetEmpty();
		return true;
	}

	/** Called to enforce any restrictions this template has on paths when it is first selected (so it doesn't generate an error unnecessarily) */
	virtual void UpdatePathWhenTemplateSelected(FString& InOutPath) {}

	/** Called to change away from special folders if needed */
	virtual void UpdatePathWhenTemplateUnselected(FString& InOutPath) {}

	/** Constructor */
	FPluginTemplateDescription(FText InName, FText InDescription, FString InOnDiskPath, bool InCanContainContent, EHostType::Type InModuleDescriptorType, ELoadingPhase::Type InLoadingPhase = ELoadingPhase::Default)
		: Name(InName)
		, Description(InDescription)
		, OnDiskPath(InOnDiskPath)
		, bCanContainContent(InCanContainContent)
		, ModuleDescriptorType(InModuleDescriptorType)
		, LoadingPhase(InLoadingPhase)
	{
	}

	virtual ~FPluginTemplateDescription() {}
};

/**
 * Feature interface for a Plugins management UI
 */
class IPluginsEditorFeature : public IModularFeature
{

public:
	/**
	  * Registers the specified plugin template with the New Plugin wizard
	  */
	virtual void RegisterPluginTemplate(TSharedRef<FPluginTemplateDescription> Template) = 0;

	/**
	  * Unregisters the specified plugin template from the New Plugin wizard
	  */
	virtual void UnregisterPluginTemplate(TSharedRef<FPluginTemplateDescription> Template) = 0;
};

