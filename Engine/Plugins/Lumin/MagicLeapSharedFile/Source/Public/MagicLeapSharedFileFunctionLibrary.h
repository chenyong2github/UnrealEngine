// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Engine/Engine.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MagicLeapSharedFileTypes.h"
#include "MagicLeapSharedFileFunctionLibrary.generated.h"

UCLASS(ClassGroup = MagicLeap)
class MAGICLEAPSHAREDFILE_API UMagicLeapSharedFileFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
		Get the names of the files that the application has access to.
		The application can then use the file names and read them with the IMagicLeapSharedFilePlugin::SharedFileRead() function.
		@param OutSharedFileList Output param containing list of file names this app has acces to.
		@return true if function call succeeded and output param is valid, false otherwise
	*/
	UFUNCTION(BlueprintCallable, Category = "SharedFile Function Library | MagicLeap")
	static bool SharedFileListAccessibleFiles(TArray<FString>& OutSharedFileList);

	/**
		Let the app get access to the user's shared files from the common storage location.  This API will pop up a System UI
		dialogue box with a file picker through which the users can pick files they wants to let the app have access to.  The list
		of selected file names will be returned to the app via the delegate.
		@param InResultDelegate Delegate to be called when the user finishes picking files.
		@return true if call to invoke the file picker succeeded, false otherwise
	*/
	UFUNCTION(BlueprintCallable, Category = "SharedFile Function Library | MagicLeap")
	static bool SharedFilePickAsync(const FMagicLeapFilesPickedResultDelegate& InResultDelegate);
};
