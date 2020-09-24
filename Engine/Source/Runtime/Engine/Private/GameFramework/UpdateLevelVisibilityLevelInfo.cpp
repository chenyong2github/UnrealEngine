// Copyright Epic Games, Inc. All Rights Reserved.
#include "GameFramework/UpdateLevelVisibilityLevelInfo.h"
#include "Engine/Level.h"
#include "HAL/IConsoleManager.h"
#include "Serialization/Archive.h"
#include "UObject/CoreNet.h"
#include "UObject/Package.h"

// CVars
namespace PlayerControllerCVars
{
	static bool LevelVisibilityDontSerializeFileName = false;
	FAutoConsoleVariableRef CVarLevelVisibilityDontSerializeFileName(
		TEXT("PlayerController.LevelVisibilityDontSerializeFileName"),
		LevelVisibilityDontSerializeFileName,
		TEXT("When true, we'll always skip serializing FileName with FUpdateLevelVisibilityLevelInfo's. This will save bandwidth when games don't need both.")
	);
}

FUpdateLevelVisibilityLevelInfo::FUpdateLevelVisibilityLevelInfo(const ULevel* const Level, const bool bInIsVisible)
	: bIsVisible(bInIsVisible)
	, bSkipCloseOnError(false)
{
	const UPackage* const LevelPackage = Level->GetOutermost();
	PackageName = LevelPackage->GetFName();

	// When packages are duplicated for PIE, they may not have a FileName.
	// For now, just revert to the old behavior.
	FileName = (LevelPackage->FileName == NAME_None) ? PackageName : LevelPackage->FileName;
}

bool FUpdateLevelVisibilityLevelInfo::NetSerialize(FArchive& Ar, UPackageMap* PackageMap, bool& bOutSuccess)
{
	bool bArePackageAndFileTheSame = !!((PlayerControllerCVars::LevelVisibilityDontSerializeFileName) || (FileName == PackageName) || (FileName == NAME_None));
	bool bLocalIsVisible = !!bIsVisible;

	Ar.SerializeBits(&bArePackageAndFileTheSame, 1);
	Ar.SerializeBits(&bLocalIsVisible, 1);
	Ar << PackageName;

	if (!bArePackageAndFileTheSame)
	{
		Ar << FileName;
	}
	else if (Ar.IsLoading())
	{
		FileName = PackageName;
	}

	bIsVisible = bLocalIsVisible;

	bOutSuccess = !Ar.IsError();
	return true;
}

