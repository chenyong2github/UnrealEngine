// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/AnimPoseSearchProvider.h"
#include "Features/IModularFeatures.h"


namespace UE { namespace Anim {

const FName IPoseSearchProvider::ModularFeatureName(TEXT("AnimPoseSearch"));

IPoseSearchProvider* IPoseSearchProvider::Get()
{
	if (IsAvailable())
	{
		return &IModularFeatures::Get().GetModularFeature<IPoseSearchProvider>(ModularFeatureName);
	}
	return nullptr;
}

bool IPoseSearchProvider::IsAvailable()
{
	return IModularFeatures::Get().IsModularFeatureAvailable(ModularFeatureName);
}

}} // namespace UE::Anim