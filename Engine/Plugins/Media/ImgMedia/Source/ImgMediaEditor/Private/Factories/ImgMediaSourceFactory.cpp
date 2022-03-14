// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImgMediaSourceFactory.h"
#include "Misc/Paths.h"
#include "ImgMediaSource.h"


/* UExrFileMediaSourceFactory structors
 *****************************************************************************/

UImgMediaSourceFactory::UImgMediaSourceFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Formats.Add(TEXT("exr;EXR ImgMedia Image Sequence"));

	SupportedClass = UImgMediaSource::StaticClass();
	bEditorImport = true;
	
	// Required to allow texture factory to take priority when importing new image files
	ImportPriority = DefaultImportPriority - 1;
}


/* UFactory overrides
 *****************************************************************************/

UObject* UImgMediaSourceFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
	UImgMediaSource* MediaSource = NewObject<UImgMediaSource>(InParent, InClass, InName, Flags);
	MediaSource->SetSequencePath(FPaths::GetPath(CurrentFilename));

	return MediaSource;
}
