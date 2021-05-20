// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/StringView.h"
#include "Misc/ScopeExit.h"
#include "Templates/RefCounting.h"
#include "Templates/UniquePtr.h"

class FCompressedBuffer;

namespace UE::DerivedData { class FBuildInput; }
namespace UE::DerivedData { class FBuildInputBuilder; }

namespace UE::DerivedData::Private
{

class IBuildInputInternal
{
public:
	virtual ~IBuildInputInternal() = default;
	virtual FStringView GetName() const = 0;
	virtual const FCompressedBuffer& GetInput(FStringView Key) const = 0;
	virtual void AddRef() const = 0;
	virtual void Release() const = 0;
};

FBuildInput CreateBuildInput(IBuildInputInternal* Input);

class IBuildInputBuilderInternal
{
public:
	virtual ~IBuildInputBuilderInternal() = default;
	virtual void AddInput(FStringView Key, const FCompressedBuffer& Buffer) = 0;
	virtual FBuildInput Build() = 0;
};

FBuildInputBuilder CreateBuildInputBuilder(IBuildInputBuilderInternal* InputBuilder);

} // UE::DerivedData::Private

namespace UE::DerivedData
{

/**
 * A build input is an immutable container of key/value pairs for the inputs to a build function.
 *
 * The keys for inputs are names that are unique within the build input.
 *
 * @see FBuildAction
 */
class FBuildInput
{
public:
	/** Returns the name by which to identify this input for logging and profiling. */
	inline FStringView GetName() const { return Input->GetName(); }

	/** Visits every input in order by key. */
	inline const FCompressedBuffer& GetInput(FStringView Key) const { return Input->GetInput(Key); }

private:
	friend class FOptionalBuildInput;
	friend FBuildInput Private::CreateBuildInput(Private::IBuildInputInternal* Input);

	/** Construct a build input. Use Build() on a builder from IBuild::CreateInput(). */
	inline explicit FBuildInput(Private::IBuildInputInternal* InInput)
		: Input(InInput)
	{
	}

	TRefCountPtr<Private::IBuildInputInternal> Input;
};

/**
 * A build input builder is used to construct a build input.
 *
 * Create using IBuild::CreateInput().
 *
 * @see FBuildInput
 */
class FBuildInputBuilder
{
public:
	/** Add an input with a key that is unique within this input. */
	inline void AddInput(FStringView Key, const FCompressedBuffer& Buffer)
	{
		InputBuilder->AddInput(Key, Buffer);
	}

	/** Build a build input, which makes this builder subsequently unusable. */
	inline FBuildInput Build()
	{
		ON_SCOPE_EXIT { InputBuilder = nullptr; };
		return InputBuilder->Build();
	}

private:
	friend FBuildInputBuilder Private::CreateBuildInputBuilder(Private::IBuildInputBuilderInternal* InputBuilder);

	/** Construct a build input builder. Use IBuild::CreateInput(). */
	inline explicit FBuildInputBuilder(Private::IBuildInputBuilderInternal* InInputBuilder)
		: InputBuilder(InInputBuilder)
	{
	}

	TUniquePtr<Private::IBuildInputBuilderInternal> InputBuilder;
};

/**
 * A build input that can be null.
 *
 * @see FBuildInput
 */
class FOptionalBuildInput : private FBuildInput
{
public:
	inline FOptionalBuildInput() : FBuildInput(nullptr) {}

	inline FOptionalBuildInput(FBuildInput&& InInput) : FBuildInput(MoveTemp(InInput)) {}
	inline FOptionalBuildInput(const FBuildInput& InInput) : FBuildInput(InInput) {}
	inline FOptionalBuildInput& operator=(FBuildInput&& InInput) { FBuildInput::operator=(MoveTemp(InInput)); return *this; }
	inline FOptionalBuildInput& operator=(const FBuildInput& InInput) { FBuildInput::operator=(InInput); return *this; }

	/** Returns the build input. The caller must check for null before using this accessor. */
	inline const FBuildInput& Get() const & { return *this; }
	inline FBuildInput&& Get() && { return MoveTemp(*this); }

	inline bool IsNull() const { return !IsValid(); }
	inline bool IsValid() const { return Input.IsValid(); }
	inline explicit operator bool() const { return IsValid(); }

	inline void Reset() { *this = FOptionalBuildInput(); }
};

} // UE::DerivedData
