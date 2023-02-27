// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "AnimAssetFindReplace.generated.h"

class SAnimAssetFindReplace;

/** Context for toolbar */
UCLASS()
class UAnimAssetFindReplaceContext : public UObject
{
	GENERATED_BODY()

public:
	/** Weak ptr to the find/replace widget */
	TWeakPtr<SAnimAssetFindReplace> Widget;
};

enum class EAnimAssetFindReplaceMode : int32
{
	Find,
	Replace,
};

enum class EAnimAssetFindReplaceType : int32
{
	Curves,
	Notifies,
};

/** Configuration for the find/replace tab */
struct FAnimAssetFindReplaceConfig
{
	FAnimAssetFindReplaceConfig() = default;

	/** The initial find string **/
	FString FindString;

	/** The initial replace string **/
	FString ReplaceString;

	/** The initial type */
	EAnimAssetFindReplaceType Type = EAnimAssetFindReplaceType::Curves;

	/** The initial mode */
	EAnimAssetFindReplaceMode Mode = EAnimAssetFindReplaceMode::Find;

	/** The initial skeleton to filter by */
	FAssetData SkeletonFilter;
};