// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/StringView.h"
#include "Containers/UnrealString.h"
#include "HAL/Platform.h"

namespace UE::DatasmithImporter
{
	/**
	 * URI container class used for referencing external sources.
	 * TODO - A proper standardized version should be implemented in a Runtime/Core module to unify all URI implementations across the engine.
	 *		  For example, a URI implementation is provided in Plugins\Media\ElectraPlayer\Source\ElectraPlayerRuntime\Private\Runtime\Utilities\URI.h
	 */
	class EXTERNALSOURCE_API FSourceUri
	{
	public:
		FSourceUri() = default;

		explicit FSourceUri(const FString& InUri)
			: Uri(InUri)
		{}

		FSourceUri(const FString& InScheme, const FString& InPath)
			: Uri(InScheme + GetSchemeDelimiter() + InPath)
		{}

		/**
		 * Generate a FSourceUri from the given filepath.
		 */
		static FSourceUri FromFilePath(const FString& FilePath);

		/**
		 * Return the scheme used for file URIs : "file"
		 */
		static const FString& GetFileScheme();

		/**
		 * Return if the FSourceUri is a properly structure valid uri.
		 */
		bool IsValid() const;

		FStringView GetScheme() const;

		/**
		 * Check if the FSourceUri has the provided scheme.
		 * @param InScheme	The scheme to look for.
		 * @return	True if the Uri has the provided scheme.
		 */
		bool HasScheme(const FString& InScheme) const;

		/**
		 * Return the path of the FSourceUri
		 * @return	The path, for now no distinction is made between the authority and the path.
		 */
		FStringView GetPath() const;

		/**
		 * Return the FSourceUri as a string.
		 */
		const FString& ToString() const { return Uri; }

		bool operator==(const FSourceUri& Other) const
		{
			return Uri == Other.Uri;
		}

		bool operator!=(const FSourceUri& Other) const
		{
			return !(*this == Other);
		}

	private:

		static const FString& GetSchemeDelimiter();
		
		FString Uri;
	};

	FORCEINLINE uint32 GetTypeHash(const FSourceUri& SourceUri)
	{
		return GetTypeHash(SourceUri.ToString());
	}
}