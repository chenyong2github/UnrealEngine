// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "UObject/ObjectMacros.h"
#if WITH_EDITORONLY_DATA
	#include "Templates/SubclassOf.h"
	#include "Misc/Guid.h"
#endif
#include "StateTreeTypes.h"
#include "StateTreeVariableDesc.generated.h"

/**
 * Description of a StateTree variable.
 * Contains name and type information. The editor data allows to track variable name changes,
 * and additional type information allows more fine grained UI for Object variable type.
*/
USTRUCT(BlueprintType)
struct STATETREEMODULE_API FStateTreeVariableDesc
{
	GENERATED_BODY()

	FStateTreeVariableDesc()
		: Name()
		, Offset(0)
		, Type(EStateTreeVariableType::Void)
#if WITH_EDITORONLY_DATA
		, ID(FGuid::NewGuid())
		, BaseClass(nullptr)
#endif
	{}

	FStateTreeVariableDesc(const FName& InName, const EStateTreeVariableType InType)
		: Name(InName)
		, Offset(0)
		, Type(InType)
#if WITH_EDITORONLY_DATA
		, ID(FGuid::NewGuid())
		, BaseClass(nullptr)
#endif
	{}

	FStateTreeVariableDesc(const FName& InName, const EStateTreeVariableType InType, TSubclassOf<UObject> InBaseClass)
		: Name(InName)
		, Offset(0)
		, Type(InType)
#if WITH_EDITORONLY_DATA
		, ID(FGuid::NewGuid())
		, BaseClass(InBaseClass)
#endif
	{}

	UPROPERTY(EditDefaultsOnly, Category = Value)
	FName Name;

	UPROPERTY(EditDefaultsOnly, Category = Value)
	uint16 Offset;

	UPROPERTY(EditDefaultsOnly, Category = Value)
	EStateTreeVariableType Type;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditDefaultsOnly, Category = Value, meta = (IgnoreForMemberInitializationTest))
	FGuid ID;

	UPROPERTY(EditDefaultsOnly, Category = Value)
	TSubclassOf<UObject> BaseClass;
#endif
};

