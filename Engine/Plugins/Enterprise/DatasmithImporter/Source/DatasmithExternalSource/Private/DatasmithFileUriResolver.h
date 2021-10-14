// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IUriResolver.h"

namespace UE::DatasmithImporter
{
	class FDatasmithFileUriResolver : public IUriResolver
	{
	public:
		// IUriResolver interface begin
		virtual TSharedPtr<FExternalSource> GetOrCreateExternalSource(const FSourceUri& URI) const override;
		virtual bool CanResolveUri(const FSourceUri& URI) const override;
		virtual FName GetScheme() const override;
		// IUriResolver interface end
	};
}