// Copyright Epic Games, Inc. All Rights Reserved.

#include "DirectLinkUriResolver.h"

#include "DirectLinkExtensionModule.h"
#include "DirectLinkExternalSource.h"
#include "IDirectLinkManager.h"

namespace UE::DatasmithImporter
{
	TSharedPtr<FExternalSource> FDirectLinkUriResolver::GetOrCreateExternalSource(const FSourceUri& Uri) const
	{
		IDirectLinkManager& DirectLinkManager = IDirectLinkExtensionModule::Get().GetManager();
		return DirectLinkManager.GetOrCreateExternalSource(Uri);
	}

	bool FDirectLinkUriResolver::CanResolveUri(const FSourceUri& Uri) const
	{
		return Uri.HasScheme(GetDirectLinkScheme());
	}

	TOptional<FDirectLinkSourceDescription> FDirectLinkUriResolver::TryParseDirectLinkUri(const FSourceUri& Uri)
	{
		if (Uri.HasScheme(GetDirectLinkScheme()))
		{
			const FString UriPath(Uri.GetPath());
			TArray<FString> PathStrings;

			// Try to split the URI path into 4 parts, those parts should correspond to the DirectLink source info.
			if (UriPath.ParseIntoArray(PathStrings, TEXT("/")) == 4)
			{
				FDirectLinkSourceDescription SourceDescription;
				SourceDescription.ComputerName = MoveTemp(PathStrings[0]);
				SourceDescription.ExecutableName = MoveTemp(PathStrings[1]);
				SourceDescription.EndpointName = MoveTemp(PathStrings[2]);
				SourceDescription.SourceName = MoveTemp(PathStrings[3]);

				return TOptional<FDirectLinkSourceDescription>(MoveTemp(SourceDescription));
			}
		}

		return TOptional<FDirectLinkSourceDescription>();
	}

	const FString& FDirectLinkUriResolver::GetDirectLinkScheme()
	{
		static FString Scheme(TEXT("directlink"));
		return Scheme;
	}
}