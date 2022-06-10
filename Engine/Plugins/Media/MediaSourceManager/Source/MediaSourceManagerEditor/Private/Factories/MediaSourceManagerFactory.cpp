// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaSourceManagerFactory.h"

#include "AssetTypeCategories.h"
#include "MediaSourceManager.h"

/* UMediaSourceManagerFactory structors
 *****************************************************************************/

UMediaSourceManagerFactory::UMediaSourceManagerFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UMediaSourceManager::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

/* UFactory overrides
 *****************************************************************************/

UObject* UMediaSourceManagerFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UMediaSourceManager>(InParent, InClass, InName, Flags);
}

uint32 UMediaSourceManagerFactory::GetMenuCategories() const
{
	return EAssetTypeCategories::Media;
}

bool UMediaSourceManagerFactory::ShouldShowInNewMenu() const
{
	return true;
}
