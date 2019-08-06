// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "HairStrandsFactory.h"
#include "HairStrandsAsset.h"
#include "HairStrandsLoader.h"

#include "Developer/DesktopPlatform/Public/IDesktopPlatform.h"
#include "Developer/DesktopPlatform/Public/DesktopPlatformModule.h"
#include "UnrealEd/Public/EditorDirectories.h"

#include "Misc/Paths.h"

/**
 * true if the extension is for the .hair, .fbx, .abc format.
 */
static bool IsHairStrandsFormat(const TCHAR* Extension)
{
	return (FCString::Stricmp(Extension, TEXT("abc")) == 0) || 
		   (FCString::Stricmp(Extension, TEXT("fbx")) == 0) ||
		   (FCString::Stricmp(Extension, TEXT("hair")) == 0);
}

static void BuildHairStrands( const FString& FilePath, FHairStrandsDatas& OutStrandsDatas)
{
	if (FCString::Stricmp(*FPaths::GetExtension(FilePath), TEXT("hair")) == 0)
	{
		THairStrandsLoader<FHairFormat>::LoadHairStrands(FilePath, OutStrandsDatas);
	}
	else if (FCString::Stricmp(*FPaths::GetExtension(FilePath), TEXT("fbx")) == 0)
	{
		THairStrandsLoader<FFbxFormat>::LoadHairStrands(FilePath, OutStrandsDatas);
	}
	else if (FCString::Stricmp(*FPaths::GetExtension(FilePath), TEXT("abc")) == 0)
	{
		THairStrandsLoader<FAbcFormat>::LoadHairStrands(FilePath, OutStrandsDatas);
	}
}

UHairStrandsFactory::UHairStrandsFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UHairStrandsAsset::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
	bEditorImport = false;

	Formats.Add(TEXT("hair;Hair hair strands file"));
	Formats.Add(TEXT("fbx;Fbx hair strands file"));
	Formats.Add(TEXT("abc;Alembic hair strands file"));
}

UObject* UHairStrandsFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UHairStrandsAsset* ExistingAsset = FindObject<UHairStrandsAsset>(InParent, *InName.ToString());
	if (ExistingAsset)
	{
		ExistingAsset->ReleaseResource();
	}
	UHairStrandsAsset* CurrentAsset = NewObject<UHairStrandsAsset>(InParent, InClass, InName, Flags);
	check(CurrentAsset);

	const FString Filter(TEXT("Hair Strands Files (*.hair,*.fbx,*.abc)|*.hair;*.fbx;*.abc"));

	TArray<FString> OpenFilenames;
	int32 FilterIndex = -1;
	if (FDesktopPlatformModule::Get()->OpenFileDialog(
		nullptr,
		FString(TEXT("Choose a hair strands file")),
		FEditorDirectories::Get().GetLastDirectory(ELastDirectory::GENERIC_IMPORT),
		TEXT(""),
		Filter,
		EFileDialogFlags::None,
		OpenFilenames,
		FilterIndex))
	{
		CurrentAsset->FilePath = OpenFilenames[0];
		BuildHairStrands(CurrentAsset->FilePath, CurrentAsset->StrandsDatas);
	}

	if (CurrentAsset)
	{
		CurrentAsset->InitResource();
	}
	return CurrentAsset;
}

UObject* UHairStrandsFactory::FactoryCreateFile(UClass * InClass, UObject * InParent, FName InName, EObjectFlags Flags, 
	const FString & Filename, const TCHAR* Parms, FFeedbackContext * Warn, bool& bOutOperationCanceled) 
{
	UHairStrandsAsset* ExistingAsset = FindObject<UHairStrandsAsset>(InParent, *InName.ToString());
	if (ExistingAsset)
	{
		ExistingAsset->ReleaseResource();
	}

	UHairStrandsAsset* CurrentAsset = NewObject<UHairStrandsAsset>(InParent, InClass, InName, Flags);
	check(CurrentAsset);

	CurrentAsset->FilePath = Filename;
	BuildHairStrands(CurrentAsset->FilePath, CurrentAsset->StrandsDatas);

	if (CurrentAsset)
	{
		CurrentAsset->InitResource();
	}

	return CurrentAsset;
}

bool UHairStrandsFactory::FactoryCanImport(const FString& Filename)
{
	return  IsHairStrandsFormat(*FPaths::GetExtension(Filename));
}

bool UHairStrandsFactory::CanReimport(UObject* Obj, TArray<FString>& OutFilenames)
{
	UHairStrandsAsset* Asset = Cast<UHairStrandsAsset>(Obj);
	if (Asset)
	{
		const FString& FileName = Asset->FilePath;
		if (!FileName.IsEmpty())
		{
			OutFilenames.Add(FileName);
		}

		return true;
	}
	return false;
}

void UHairStrandsFactory::SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths)
{
	UHairStrandsAsset* Asset = Cast<UHairStrandsAsset>(Obj);
	if (Asset && ensure(NewReimportPaths.Num() == 1))
	{
		Asset->FilePath = NewReimportPaths[0];
	}
}

EReimportResult::Type UHairStrandsFactory::Reimport(UObject* Obj)
{
	UHairStrandsAsset* CurrentAsset = Cast<UHairStrandsAsset>(Obj);
	BuildHairStrands(CurrentAsset->FilePath, CurrentAsset->StrandsDatas);
	// Try to find the outer package so we can dirty it up
	if (Obj->GetOuter())
	{
		Obj->GetOuter()->MarkPackageDirty();
	}
	else
	{
		Obj->MarkPackageDirty();
	}
	return EReimportResult::Succeeded;
}

int32 UHairStrandsFactory::GetPriority() const
{
	return ImportPriority;
}
