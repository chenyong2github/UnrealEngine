// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Factories/SoundCueTemplateFactory.h"

#include "ClassViewerModule.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Engine.h"
#include "Kismet2/SClassPickerDialog.h"
#include "SoundCueGraph/SoundCueGraphNode.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundCueTemplate.h"
#include "SoundFactoryUtility.h"
#include "UObject/Class.h"


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
}

UObject* USoundCueTemplateFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	FClassViewerModule& ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");

	TSharedPtr<FAssetClassParentFilter> Filter = MakeShareable(new FAssetClassParentFilter);
	Filter->DisallowedClassFlags = CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_HideDropDown;
	Filter->AllowedChildrenOfClasses.Add(USoundCueTemplate::StaticClass());

	FClassViewerInitializationOptions Options;
	Options.Mode = EClassViewerMode::ClassPicker;
	Options.ClassFilter = Filter;

	const FText TitleText = FText::FromString(TEXT("Pick Sound Cue Template"));
	UClass* ChosenClass = nullptr;
	if (SClassPickerDialog::PickClass(TitleText, Options, ChosenClass, USoundCueTemplate::StaticClass()))
	{
		if (USoundCueTemplate* SoundCueTemplate = NewObject<USoundCueTemplate>(InParent, ChosenClass, Name, Flags))
		{
			SoundCueTemplate->RebuildGraph(*SoundCueTemplate);
			return SoundCueTemplate;
		}
	}

	return nullptr;
}
