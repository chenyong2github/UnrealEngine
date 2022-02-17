// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/AnimRootMotionProvider.h"
#include "Features/IModularFeatures.h"


namespace UE { namespace Anim {

const FName IAnimRootMotionProvider::ModularFeatureName(TEXT("AnimationWarping"));

const FName IAnimRootMotionProvider::AttributeName(TEXT("RootMotionDelta"));

const IAnimRootMotionProvider* IAnimRootMotionProvider::Get()
{
	if (IsAvailable())
	{
		IModularFeatures::Get().LockModularFeatureList();
		const IAnimRootMotionProvider* AnimRootMotionProvider = &IModularFeatures::Get().GetModularFeature<const IAnimRootMotionProvider>(ModularFeatureName);
		IModularFeatures::Get().UnlockModularFeatureList();
		return AnimRootMotionProvider;
	}
	return nullptr;
}

bool IAnimRootMotionProvider::IsAvailable()
{
	IModularFeatures::Get().LockModularFeatureList();
	const bool bIsAvailable = IModularFeatures::Get().IsModularFeatureAvailable(ModularFeatureName);
	IModularFeatures::Get().UnlockModularFeatureList();
	return bIsAvailable;
}

}}; // namespace UE::Anim