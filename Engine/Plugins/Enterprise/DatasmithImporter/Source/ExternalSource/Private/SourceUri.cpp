// Copyright Epic Games, Inc. All Rights Reserved.

#include "SourceUri.h"

#include "Misc/Paths.h"

namespace UE::DatasmithImporter
{
	bool FSourceUri::IsValid() const
	{
		const int32 SchemeDelimiterIndex = Uri.Find(GetSchemeDelimiter());
		const int32 UriPathIndex = SchemeDelimiterIndex + GetSchemeDelimiter().Len();

		// Make sure the URI has at least a scheme and a path.
		return SchemeDelimiterIndex > 0 && (Uri.Len() - UriPathIndex) > 0;
	}

	FStringView FSourceUri::GetScheme() const
	{
		const int32 SchemeSeparatorLocation = Uri.Find(GetSchemeDelimiter());
		FStringView Scheme;
		
		if (SchemeSeparatorLocation != INDEX_NONE)
		{
			Scheme = FStringView(Uri).LeftChop(Uri.Len() - SchemeSeparatorLocation);
		}

		return Scheme;
	}

	const FString& FSourceUri::GetSchemeDelimiter()
	{
		// Todo - The actual delimiter according to RFC 3986 standard, should simply be ":" when there is no authority defined in the URI.
		static FString SchemeDelimiter(TEXT("://"));
		return SchemeDelimiter;
	}

	bool FSourceUri::HasScheme(const FString& InScheme) const
	{
		return Uri.StartsWith(InScheme + GetSchemeDelimiter());
	}

	FStringView FSourceUri::GetPath() const
	{
		const int32 SchemeLen = GetScheme().Len() + GetSchemeDelimiter().Len();

		if (Uri.Len() > SchemeLen)
		{
			return FStringView(Uri).RightChop(SchemeLen);
		}
		
		return FStringView();
	}

	const FString& FSourceUri::GetFileScheme()
	{
		static FString Scheme(TEXT("file"));
		return Scheme;
	}

	FSourceUri FSourceUri::FromFilePath(const FString& FilePath)
	{
		// Make sure all paths are converted to absolute and normalized, otherwise URI won't be comparable.
		const FString AbsoluteFilePath = FPaths::ConvertRelativePathToFull(FilePath);

		return FSourceUri(GetFileScheme(), AbsoluteFilePath);
	}
}