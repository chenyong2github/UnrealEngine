// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IImageWrapper.h"


class UTexture2D;


namespace UE::RenderPages::Private
{
	/**
	 * A class containing static utility functions for the RenderPages module.
	 */
	class FRenderPagesUtils
	{
	public:
		/**
		 * Returns true if the given file is likely a valid image.
		 */
		static bool IsImage(const FString& ImagePath);

		/**
		 * Loads an image from the disk, tries to automatically figure out the correct image format.
		 *
		 * Returns NULL if it fails.
		 *
		 * Will re-use the given Texture2D if possible, bOutReusedGivenTexture2D will be true if it was possible.
		 */
		static UTexture2D* GetImage(const FString& ImagePath, UTexture2D* Texture2D, bool& bOutReusedGivenTexture2D);


		/**
		 * Converts bytes into a texture.
		 *
		 * Returns NULL if it fails.
		 */
		static UTexture2D* BytesToImage(const TArray<uint8>& ByteArray, const EImageFormat ImageFormat);

		/**
		 * Converts bytes into an texture.
		 *
		 * Returns NULL if it fails.
		 *
		 * Will re-use the given Texture2D if possible, bOutReusedGivenTexture2D will be true if it was possible.
		 */
		static UTexture2D* BytesToExistingImage(const TArray<uint8>& ByteArray, const EImageFormat ImageFormat, UTexture2D* Texture2D, bool& bOutReusedGivenTexture2D);

		/**
		 * Converts texture data into a texture.
		 *
		 * Returns NULL if it fails.
		 */
		static UTexture2D* DataToTexture2D(int32 Width, int32 Height, const void* Src, SIZE_T Count);

		/**
		 * Converts texture data into an texture.
		 *
		 * Returns NULL if it fails.
		 *
		 * Will re-use the given Texture2D if possible, bOutReusedGivenTexture2D will be true if it was possible.
		 */
		static UTexture2D* DataToExistingTexture2D(int32 Width, int32 Height, const void* Src, SIZE_T Count, UTexture2D* Texture2D, bool& bOutReusedGivenTexture2D);


		/**
		 * Returns the paths of the files that exist in the given directory path.
		 */
		static TArray<FString> GetFiles(const FString& Directory, const bool bRecursive = false);

		/**
		 * Returns the data of the file, returns an empty byte array if the file doesn't exist.
		 *
		 * Note: Can only open files of 2GB and smaller, will return an empty byte array if it is bigger than 2GB.
		 */
		static TArray<uint8> GetFileData(const FString& File);


		/**
		 * Deletes all files and directories in the given directory, including the given directory.
		 */
		static void DeleteDirectory(const FString& Directory);

		/**
		 * Deletes all files and directories in the given directory, won't delete the given directory.
		 */
		static void EmptyDirectory(const FString& Directory);


		/**
		 * Returns a normalized directory path.
		 */
		static FString NormalizeOutputDirectory(const FString& OutputDirectory);
	};
}
