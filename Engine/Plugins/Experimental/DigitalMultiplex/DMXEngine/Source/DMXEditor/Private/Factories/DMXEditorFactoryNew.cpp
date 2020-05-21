// Copyright Epic Games, Inc. All Rights Reserved.

#include "Factories/DMXEditorFactoryNew.h"
#include "AssetTypeCategories.h"
#include "Library/DMXLibrary.h"
#include "DMXEditorLog.h"
#include "DMXEditorModule.h"

#include "Widgets/SWindow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "EditorStyleSet.h"
#include "Input/Reply.h"

#include "Components/ListView.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Input/SComboBox.h"

#include "Editor.h"

#define LOCTEXT_NAMESPACE "UDMXEditorFactoryNew"

UDMXEditorFactoryNew::UDMXEditorFactoryNew()
{
	SupportedClass = UDMXLibrary::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject * UDMXEditorFactoryNew::FactoryCreateNew(UClass * Class, UObject * InParent, FName Name, EObjectFlags Flags, UObject * Context, FFeedbackContext * Warn)
{
	UDMXLibrary* DMXLibrary = nullptr;
	if (ensure(SupportedClass == Class))
	{
		ensure(0 != (RF_Public & Flags));
		DMXLibrary = MakeNewEditor(InParent, Name, Flags);
	}
	else
	{
		UE_LOG_DMXEDITOR(Warning, TEXT("SupportedClass should be the same as New Object class"));
	}

	return DMXLibrary;
}

uint32 UDMXEditorFactoryNew::GetMenuCategories() const
{
	return (uint32)FDMXEditorModule::GetAssetCategory();
}

UDMXLibrary * UDMXEditorFactoryNew::MakeNewEditor(UObject * InParent, FName Name, EObjectFlags Flags)
{
	return NewObject<UDMXLibrary>(InParent, Name, Flags);
}

#undef LOCTEXT_NAMESPACE