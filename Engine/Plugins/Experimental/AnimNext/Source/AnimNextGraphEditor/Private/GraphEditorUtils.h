// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UAnimNextGraph;
class UAnimNextGraph_EditorData;
struct FAnimNextParamType;

namespace UE::AnimNext::GraphEditor
{

struct FUtils
{
	static FName ValidateName(const UAnimNextGraph_EditorData* InEditorData, const FString& InName);

	static void GetAllGraphNames(const UAnimNextGraph_EditorData* InEditorData, TSet<FName>& OutNames);

	static FAnimNextParamType GetParameterTypeFromMetaData(const FStringView& InStringView);
};

}
