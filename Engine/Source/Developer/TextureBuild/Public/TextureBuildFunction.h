// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "DerivedDataBuildFunction.h"
#include "DerivedDataBuildVersion.h"

class ITextureFormat;

class FTextureBuildFunction : public UE::DerivedData::IBuildFunction
{
public:
	TEXTUREBUILD_API virtual FGuid GetVersion() const final;
	TEXTUREBUILD_API virtual void Configure(UE::DerivedData::FBuildConfigContext& Context) const override;
	TEXTUREBUILD_API virtual void Build(UE::DerivedData::FBuildContext& Context) const override;

protected:
	virtual void GetVersion(UE::DerivedData::FBuildVersionBuilder& Builder, ITextureFormat*& OutTextureFormatVersioning) const = 0;
};
