// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/StringFwd.h"
#include "IO/IoHash.h"
#include "Misc/ScopeExit.h"
#include "Templates/Function.h"
#include "Templates/RefCounting.h"
#include "Templates/UniquePtr.h"

class FCbObject;
class FCbWriter;
struct FGuid;

namespace UE::DerivedData { class FBuildAction; }
namespace UE::DerivedData { class FBuildActionBuilder; }

namespace UE::DerivedData
{

/** A key that uniquely identifies a build action. */
struct FBuildActionKey
{
	FIoHash Hash;
};

} // UE::DerivedData

namespace UE::DerivedData::Private
{

class IBuildActionInternal
{
public:
	virtual ~IBuildActionInternal() = default;
	virtual const FBuildActionKey& GetKey() const = 0;
	virtual FStringView GetName() const = 0;
	virtual FStringView GetFunction() const = 0;
	virtual const FGuid& GetFunctionVersion() const = 0;
	virtual const FGuid& GetBuildSystemVersion() const = 0;
	virtual void IterateConstants(TFunctionRef<void (FStringView Key, FCbObject&& Value)> Visitor) const = 0;
	virtual void IterateInputs(TFunctionRef<void (FStringView Key, const FIoHash& RawHash, uint64 RawSize)> Visitor) const = 0;
	virtual void Save(FCbWriter& Writer) const = 0;
	virtual void AddRef() const = 0;
	virtual void Release() const = 0;
};

FBuildAction CreateBuildAction(IBuildActionInternal* Action);

class IBuildActionBuilderInternal
{
public:
	virtual ~IBuildActionBuilderInternal() = default;
	virtual void AddConstant(FStringView Key, const FCbObject& Value) = 0;
	virtual void AddInput(FStringView Key, const FIoHash& RawHash, uint64 RawSize) = 0;
	virtual FBuildAction Build() = 0;
};

FBuildActionBuilder CreateBuildActionBuilder(IBuildActionBuilderInternal* ActionBuilder);

} // UE::DerivedData::Private

namespace UE::DerivedData
{

/**
 * A build action references a build function and the inputs to that build function.
 *
 * The purpose of an action is to capture everything required to execute a derived data build for
 * a fixed version of the build function and its constants and inputs.
 *
 * The key for the action uniquely identifies the action and is derived by hashing the serialized
 * compact binary representation of the action.
 *
 * The keys for constants and inputs are names that are unique within the build action.
 *
 * The build action is immutable, and is created by a build session from a build definition.
 *
 * @see FBuildDefinition
 * @see FBuildSession
 */
class FBuildAction
{
public:
	/** Returns the key that uniquely identifies this build action. */
	inline const FBuildActionKey& GetKey() const { return Action->GetKey(); }

	/** Returns the name by which to identify this action for logging and profiling. */
	inline FStringView GetName() const
	{
		return Action->GetName();
	}

	/** Returns the name of the build function with which to build this action. */
	inline FStringView GetFunction() const
	{
		return Action->GetFunction();
	}

	/** Returns the version of the build function with which to build this action. */
	inline const FGuid& GetFunctionVersion() const
	{
		return Action->GetFunctionVersion();
	}

	/** Returns the version of the build system required to build this action. */
	inline const FGuid& GetBuildSystemVersion() const
	{
		return Action->GetBuildSystemVersion();
	}

	/** Visits every constant in order by key. */
	inline void IterateConstants(TFunctionRef<void (FStringView Key, FCbObject&& Value)> Visitor) const
	{
		Action->IterateConstants(MoveTemp(Visitor));
	}

	/** Visits every input in order by key. */
	inline void IterateInputs(TFunctionRef<void (FStringView Key, const FIoHash& RawHash, uint64 RawSize)> Visitor) const
	{
		Action->IterateInputs(MoveTemp(Visitor));
	}

	/** Saves the build action to a compact binary object. Calls BeginObject and EndObject. */
	inline void Save(FCbWriter& Writer) const
	{
		return Action->Save(Writer);
	}

private:
	friend FBuildAction Private::CreateBuildAction(Private::IBuildActionInternal* Action);

	/** Construct a build action. Use Build() on a builder from IBuild::CreateAction(). */
	inline explicit FBuildAction(Private::IBuildActionInternal* InAction)
		: Action(InAction)
	{
	}

	TRefCountPtr<Private::IBuildActionInternal> Action;
};

/**
 * A build action builder is used to construct a build action.
 *
 * Create using IBuild::CreateAction() which must be given a build function name.
 *
 * @see FBuildAction
 */
class FBuildActionBuilder
{
public:
	/** Add a constant object with a key that is unique within this action. */
	inline void AddConstant(FStringView Key, const FCbObject& Value)
	{
		ActionBuilder->AddConstant(Key, Value);
	}

	/** Add an input with a key that is unique within this action. */
	inline void AddInput(FStringView Key, const FIoHash& RawHash, uint64 RawSize)
	{
		ActionBuilder->AddInput(Key, RawHash, RawSize);
	}

	/** Build a build action, which makes this builder subsequently unusable. */
	inline FBuildAction Build()
	{
		ON_SCOPE_EXIT { ActionBuilder = nullptr; };
		return ActionBuilder->Build();
	}

private:
	friend FBuildActionBuilder Private::CreateBuildActionBuilder(Private::IBuildActionBuilderInternal* ActionBuilder);

	/** Construct a build action builder. Use IBuild::CreateAction(). */
	inline explicit FBuildActionBuilder(Private::IBuildActionBuilderInternal* InActionBuilder)
		: ActionBuilder(InActionBuilder)
	{
	}

	TUniquePtr<Private::IBuildActionBuilderInternal> ActionBuilder;
};

} // UE::DerivedData
