// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "Containers/StringView.h"
#include "DerivedDataBuildFunction.h"
#include "Misc/Guid.h"

class ITextureCompressorModule;

class FTextureBuildFunction : public UE::DerivedData::IBuildFunction
{
public:
	TEXTUREBUILD_API FTextureBuildFunction();

	TEXTUREBUILD_API void SetName(FStringView InName)
	{
		Name = InName;
	}
	
	TEXTUREBUILD_API virtual FStringView GetName() const override
	{
		return Name;
	}

	TEXTUREBUILD_API virtual FGuid GetVersion() const override
	{
		return FGuid();
	}

	TEXTUREBUILD_API virtual void Build(UE::DerivedData::FBuildContext& Context) const override;
private:
	ITextureCompressorModule& Compressor;
	FString Name;
};
