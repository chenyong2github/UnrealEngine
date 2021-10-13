// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "IUriResolver.h"

struct FGuid;
namespace DirectLink
{
	using FSourceHandle = FGuid;
}

namespace UE::DatasmithImporter
{
	struct FDirectLinkSourceDescription
	{
		FString ComputerName;
		FString ExecutableName;
		FString EndpointName;
		FString SourceName;
	};

	class DIRECTLINKEXTENSION_API FDirectLinkUriResolver : public IUriResolver
	{
	public:
		// IUriResolver interface begin
		virtual TSharedPtr<FExternalSource> GetOrCreateExternalSource(const FSourceUri& Uri) const override;
		virtual bool CanResolveUri(const FSourceUri& Uri) const override;
		virtual FName GetScheme() const override { return FName(GetDirectLinkScheme()); }
		// IUriResolver interface end

		/**
		 * Try to parse the DirectLink source description components from an URI. 
		 * Return an unset TOptional if the parsing failed.
		 */
		static TOptional<FDirectLinkSourceDescription> TryParseDirectLinkUri(const FSourceUri& Uri);

		/**
		 * Return the scheme used for DirectLink URIs : "directlink"
		 */
		static const FString& GetDirectLinkScheme();
	};
}