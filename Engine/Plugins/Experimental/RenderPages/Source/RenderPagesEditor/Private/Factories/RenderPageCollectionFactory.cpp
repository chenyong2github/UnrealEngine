// Copyright Epic Games, Inc. All Rights Reserved.

#include "Factories/RenderPageCollectionFactory.h"
#include "Blueprints/RenderPagesBlueprint.h"
#include "IContentBrowserSingleton.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/MessageDialog.h"
#include "RenderPage/RenderPageCollection.h"
#include "RenderPage/RenderPagesBlueprintGeneratedClass.h"

#define LOCTEXT_NAMESPACE "RenderPagesBlueprintFactory"


URenderPagesBlueprintFactory::URenderPagesBlueprintFactory()
{
	ParentClass = URenderPageCollection::StaticClass();
	SupportedClass = URenderPagesBlueprint::StaticClass();
	bCreateNew = true; // This factory manufacture new objects from scratch
	bEditAfterNew = true; // This factory will open the editor for each new object
}

UObject* URenderPagesBlueprintFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext)
{
	// Make sure we are trying to factory a Render Pages Blueprint, then create and init one
	check(InClass->IsChildOf(URenderPagesBlueprint::StaticClass()));

	if ((ParentClass == nullptr) || !FKismetEditorUtilities::CanCreateBlueprintOfClass(ParentClass) || !ParentClass->IsChildOf(URenderPageCollection::StaticClass()))
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("ClassName"), (ParentClass != nullptr) ? FText::FromString(ParentClass->GetName()) : LOCTEXT("Null", "(null)"));
		FMessageDialog::Open(EAppMsgType::Ok, FText::Format(LOCTEXT("CannotCreateRenderPagesBlueprint", "Cannot create an Render Pages Blueprint based on the class '{0}'."), Args));
		return nullptr;
	}

	if (URenderPagesBlueprint* RenderPagesBlueprint = CastChecked<URenderPagesBlueprint>(FKismetEditorUtilities::CreateBlueprint(ParentClass, InParent, InName, BPTYPE_Normal, URenderPagesBlueprint::StaticClass(), URenderPagesBlueprintGeneratedClass::StaticClass(), CallingContext)))
	{
		RenderPagesBlueprint->PostLoad();
		return RenderPagesBlueprint;
	}
	return nullptr;
}

UObject* URenderPagesBlueprintFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return FactoryCreateNew(InClass, InParent, InName, Flags, Context, Warn, NAME_None);
}

bool URenderPagesBlueprintFactory::ShouldShowInNewMenu() const
{
	return true;
}

uint32 URenderPagesBlueprintFactory::GetMenuCategories() const
{
	//  if wanting to show it in its own category:
	// IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	// return AssetTools.RegisterAdvancedAssetCategory("Render Pages", LOCTEXT("AssetCategoryName", "Render Pages"));

	return EAssetTypeCategories::Misc;
}


#undef LOCTEXT_NAMESPACE
