// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "DerivedDataBuildFunction.h"

class FTextureBuildFunction : public UE::DerivedData::IBuildFunction
{
public:
	TEXTUREBUILD_API virtual void Build(UE::DerivedData::FBuildContext& Context) const override;
private:
};
