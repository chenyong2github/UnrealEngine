// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Shader/PreshaderTypes.h"

class FUniformExpressionSet;
struct FMaterialRenderContext;

namespace UE
{
namespace Shader
{

class FPreshaderData;

using FPreshaderStack = TArray<FValue, TInlineAllocator<64u>>;

struct FPreshaderDataContext
{
	explicit FPreshaderDataContext(const FPreshaderData& InData);
	FPreshaderDataContext(const FPreshaderDataContext& InContext, uint32 InOffset, uint32 InSize);

	const uint8* RESTRICT Ptr;
	const uint8* RESTRICT EndPtr;
	const FScriptName* RESTRICT Names;
	int32 NumNames;
};

ENGINE_API void EvaluatePreshader(const FUniformExpressionSet* UniformExpressionSet, const FMaterialRenderContext& Context, FPreshaderStack& Stack, FPreshaderDataContext& RESTRICT Data, FValue& OutValue);

} // namespace Shader
} // namespace UE
