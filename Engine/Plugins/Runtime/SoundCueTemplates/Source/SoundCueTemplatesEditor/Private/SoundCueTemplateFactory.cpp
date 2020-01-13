// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundCueTemplateFactory.h"
#include "SoundCueTemplateClassFilter.h"

#include "ClassViewerModule.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Engine.h"
#include "Kismet2/SClassPickerDialog.h"
#include "SoundCueGraph/SoundCueGraphNode.h"
#include "Sound/SoundCue.h"
#include "UObject/Class.h"

#define LOCTEXT_NAMESPACE "SoundCueTemplatesEditor"

USoundCueTemplateCopyFactory::USoundCueTemplateCopyFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = USoundCue::StaticClass();

	bCreateNew = false;
	bEditorImport = false;
	bEditAfterNew = true;
}

UObject* USoundCueTemplateCopyFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	if (SoundCueTemplate.IsValid())
	{
		if (USoundCue* NewCue = NewObject<USoundCue>(InParent, Name, Flags))
		{
			SoundCueTemplate.Get()->RebuildGraph(*NewCue);
			return NewCue;
		}
	}

	return nullptr;
}

USoundCueTemplateFactory::USoundCueTemplateFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = USoundCueTemplate::StaticClass();

	bCreateNew = true;
	bEditorImport = false;
	bEditAfterNew = true;
	SoundCueTemplateClass = nullptr;
}

bool USoundCueTemplateFactory::ConfigureProperties()
{
	SoundCueTemplateClass = nullptr;

	// Load the classviewer module to display a class picker
	FClassViewerModule& ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");

	TSharedPtr<FSoundCueTemplateAssetParentFilter> Filter = MakeShareable(new FSoundCueTemplateAssetParentFilter);
	Filter->DisallowedClassFlags = CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_HideDropDown;
	Filter->AllowedChildrenOfClasses.Add(USoundCueTemplate::StaticClass());

	FClassViewerInitializationOptions Options;
	Options.Mode = EClassViewerMode::ClassPicker;
	Options.ClassFilter = Filter;

	const FText TitleText = LOCTEXT("CreateSoundCueTemplateOfType", "Pick Type of SoundCueTemplate");
	UClass* ChosenClass = nullptr;

	const bool bPressedOk = SClassPickerDialog::PickClass(TitleText, Options, ChosenClass, USoundCueTemplate::StaticClass());

	if (bPressedOk)
	{
		SoundCueTemplateClass = ChosenClass;
	}
	return bPressedOk;
}

UObject* USoundCueTemplateFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	if (SoundCueTemplateClass != nullptr)
	{
		if (USoundCueTemplate* NewSoundCueTemplate = NewObject<USoundCueTemplate>(InParent, SoundCueTemplateClass, Name, Flags))
		{
			NewSoundCueTemplate->RebuildGraph(*NewSoundCueTemplate);
			return NewSoundCueTemplate;
		}
	}

	return nullptr;
}

FString USoundCueTemplateFactory::GetDefaultNewAssetName() const
{
	if (SoundCueTemplateClass)
	{
		return SoundCueTemplateClass->GetName() + FString(TEXT("_"));
	}

	return Super::GetDefaultNewAssetName();
}

#undef LOCTEXT_NAMESPACE // "SoundCueTemplatesEditor"
