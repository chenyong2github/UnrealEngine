// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DirectLink/Network/DirectLinkStream.h"

#include "CoreTypes.h"
#include "Templates/SharedPointer.h"


namespace DirectLink
{
class ISceneProvider;

class FStreamDestination : public FStreamEndpoint
{
public:
	FStreamDestination(const FString& Name, EVisibility Visibility, const TSharedPtr<ISceneProvider>& Provider)
		: FStreamEndpoint(Name, Visibility)
		, Provider(Provider)
	{}

	const TSharedPtr<ISceneProvider>& GetProvider() const { return Provider; }

private:
	TSharedPtr<ISceneProvider> Provider;
};


} // namespace DirectLink
