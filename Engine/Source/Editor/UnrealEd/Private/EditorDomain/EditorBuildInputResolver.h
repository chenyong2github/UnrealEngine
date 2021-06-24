// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DerivedDataBuildInputResolver.h"

#if WITH_EDITOR

namespace UE::DerivedData
{

/** A BuildInputResolver that looks up the process-global inputs registered from package loads or a cache of the package loads. */
class FEditorBuildInputResolver : public UE::DerivedData::IBuildInputResolver
{
public:
	UNREALED_API static FEditorBuildInputResolver& Get();

	virtual FRequest ResolveKey(const FBuildKey& Key, FOnBuildKeyResolved&& OnResolved) override;
	virtual FRequest ResolveInputMeta(const FBuildDefinition& Definition, EPriority Priority,
		FOnBuildInputMetaResolved&& OnResolved) override;
	virtual FRequest ResolveInputData(const FBuildDefinition& Definition, EPriority Priority,
		FOnBuildInputDataResolved&& OnResolved, FBuildInputFilter&& Filter = FBuildInputFilter()) override;
	virtual FRequest ResolveInputData(const FBuildAction& Action, EPriority Priority,
		FOnBuildInputDataResolved&& OnResolved, FBuildInputFilter&& Filter = FBuildInputFilter()) override;
};

} // namespace UE::DerivedData

#endif // WITH_EDITOR