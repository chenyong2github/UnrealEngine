// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "MagicLeapSharedFileTypes.h"

/**
 * The public interface to this module.  In most cases, this interface is only public to sibling modules
 * within this plugin.
 */
class MAGICLEAPSHAREDFILE_API IMagicLeapSharedFilePlugin : public IModuleInterface
{
public:

	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static inline IMagicLeapSharedFilePlugin& Get()
	{
		return FModuleManager::LoadModuleChecked<IMagicLeapSharedFilePlugin>("MagicLeapSharedFile");
	}

	/**
	 * Checks to see if this module is loaded and ready.  It is only valid to call Get() if IsAvailable() returns true.
	 *
	 * @return True if the module is loaded and ready to use
	 */
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("MagicLeapSharedFile");
	}

	/**
		Return a IFileHandle pointer to read the user shared file. Caller owns the IFileHandle pointer and should delete it after use.
		@param FileName Name of the shared file to read.
		@return IFileHandle pointer to read the file. If the application does not have user permission to access the file, nullptr will be returned.
	*/
	virtual IFileHandle* SharedFileRead(const FString& FileName) = 0;

	/**
		Return a IFileHandle pointer to write the user shared file. Caller owns the IFileHandle pointer and should delete it after use.
		@param FileName Name of the shared file to write to. Needs to just be a file name, cannot be a path.
		@return IFileHandle pointer to read the file. If the application does not have user permission to access the file, nullptr will be returned.
	*/
	virtual IFileHandle* SharedFileWrite(const FString& FileName) = 0;

	/**
		Get the names of the files that the application has access to.
		The application can then use the file names and read them with the IMagicLeapSharedFilePlugin::SharedFileRead() function.
		@param OutSharedFileList Output param containing list of file names this app has acces to.
		@return true if function call succeeded and output param is valid, false otherwise
	*/
	virtual bool SharedFileListAccessibleFiles(TArray<FString>& OutSharedFileList) = 0;

	/**
		Let the app get access to the user's shared files from the common storage location.  This API will pop up a System UI
		dialogue box with a file picker through which the users can pick files they wants to let the app have access to.  The list
		of selected file names will be returned to the app via the delegate.
		@param InResultDelegate Delegate to be called when the user finishes picking files.
		@return true if call to invoke the file picker succeeded, false otherwise
	*/
	virtual bool SharedFilePickAsync(const FMagicLeapFilesPickedResultDelegate& InResultDelegate) = 0;
};
