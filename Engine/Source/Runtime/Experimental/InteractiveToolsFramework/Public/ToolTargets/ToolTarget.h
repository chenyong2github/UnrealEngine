// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "ToolTarget.generated.h"

class FToolTargetTypeRequirements;

/**
 * A tool target is a stand-in object that a tool can operate on. It can implement any
 * interface(s) that a tool requires without having to implement those interfaces in
 * specific actor classes, and it allows the tools to work on anything that can provide
 * a qualifying tool target.
 */
UCLASS(Transient, Abstract)
class INTERACTIVETOOLSFRAMEWORK_API UToolTarget : public UObject
{
	GENERATED_BODY()
public:

	/** @return true if target is still valid. May become invalid for various reasons (eg Component was deleted out from under us) */
	virtual bool IsValid() const PURE_VIRTUAL(UToolTarget::IsValid, return false;);
};


/**
 * A structure used to specify the requirements of a tool for its target. E.g., a tool
 * may need a target that has base type x and interfaces w,y,z.
 */
class INTERACTIVETOOLSFRAMEWORK_API FToolTargetTypeRequirements
{
public:

	const UClass* BaseType = nullptr; //TODO: Do we even need this? Seems like we shouldn't use it.
	TArray<const UClass*, TInlineAllocator<2>> Interfaces;

	FToolTargetTypeRequirements()
	{
	}

	explicit FToolTargetTypeRequirements(const UClass* BaseTypeIn)
	{
		BaseType = BaseTypeIn;
	}

	explicit FToolTargetTypeRequirements(const UClass* BaseTypeIn, const UClass* Interface0)
	{
		BaseType = BaseTypeIn;
		Interfaces.Add(Interface0);
	}

	explicit FToolTargetTypeRequirements(const UClass* BaseTypeIn, const TArray<const UClass*>& InterfacesIn)
	{
		BaseType = BaseTypeIn;
		Interfaces = InterfacesIn;
	}

	bool AreSatisfiedBy(UClass* Class) const;

	bool AreSatisfiedBy(UToolTarget* ToolTarget) const;
};


/**
 * Base class for factories of tool targets, which let a tool manager build targets
 * out of inputs without knowing anything itself.
 */
UCLASS(Transient)
class INTERACTIVETOOLSFRAMEWORK_API UToolTargetFactory : public UObject
{
	GENERATED_BODY()

public:

	virtual bool CanBuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& TargetTypeInfo) const PURE_VIRTUAL(UToolTargetFactory::CanBuildTarget, return false;);

	virtual UToolTarget* BuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& TargetTypeInfo) PURE_VIRTUAL(UToolTargetFactory::BuildTarget, return nullptr;);
};
