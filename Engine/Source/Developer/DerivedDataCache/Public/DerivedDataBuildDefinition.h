// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/StringView.h"
#include "Templates/RefCounting.h"
#include "Templates/UniquePtr.h"

class FCbObject;
class FCbWriter;
struct FGuid;
struct FIoHash;
template <typename FuncType> class TFunctionRef;

namespace UE::DerivedData { class FBuildDefinition; }
namespace UE::DerivedData { class FBuildDefinitionBuilder; }
namespace UE::DerivedData { struct FBuildKey; }
namespace UE::DerivedData { struct FBuildPayloadKey; }

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace UE::DerivedData::Private
{

class IBuildDefinitionInternal
{
public:
	virtual ~IBuildDefinitionInternal() = default;
	virtual const FBuildKey& GetKey() const = 0;
	virtual FStringView GetName() const = 0;
	virtual FStringView GetFunction() const = 0;
	virtual void IterateConstants(TFunctionRef<void (FStringView Key, FCbObject&& Value)> Visitor) const = 0;
	virtual void IterateInputBuilds(TFunctionRef<void (FStringView Key, const FBuildPayloadKey& PayloadKey)> Visitor) const = 0;
	virtual void IterateInputBulkData(TFunctionRef<void (FStringView Key, const FGuid& BulkDataId)> Visitor) const = 0;
	virtual void IterateInputFiles(TFunctionRef<void (FStringView Key, FStringView Path)> Visitor) const = 0;
	virtual void IterateInputHashes(TFunctionRef<void (FStringView Key, const FIoHash& RawHash)> Visitor) const = 0;
	virtual void Save(FCbWriter& Writer) const = 0;
	virtual void AddRef() const = 0;
	virtual void Release() const = 0;
};

FBuildDefinition CreateBuildDefinition(IBuildDefinitionInternal* Definition);

class IBuildDefinitionBuilderInternal
{
public:
	virtual ~IBuildDefinitionBuilderInternal() = default;
	virtual void AddConstant(FStringView Key, const FCbObject& Value) = 0;
	virtual void AddInputBuild(FStringView Key, const FBuildPayloadKey& PayloadKey) = 0;
	virtual void AddInputBulkData(FStringView Key, const FGuid& BulkDataId) = 0;
	virtual void AddInputFile(FStringView Key, FStringView Path) = 0;
	virtual void AddInputHash(FStringView Key, const FIoHash& RawHash) = 0;
	virtual FBuildDefinition Build() = 0;
};

FBuildDefinitionBuilder CreateBuildDefinitionBuilder(IBuildDefinitionBuilderInternal* DefinitionBuilder);

} // UE::DerivedData::Private

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace UE::DerivedData
{

/**
 * A build definition references a build function and the inputs to that build function.
 *
 * The purpose of a definition is to capture everything required to execute a derived data build.
 * Inputs to the build are partly static (constants and input hashes) and partly variable (files,
 * bulk data, payloads from other builds); and the build function is referenced by name, but does
 * not correspond to a fixed version of the build function.
 *
 * The key for the definition uniquely identifies the definition and is derived from its function
 * and inputs, such that any definition with the same function and inputs will have the same key.
 *
 * The keys for constants and inputs are names that are unique within the build definition across
 * every type of constant and input.
 *
 * To build a definition against a specific version of the function and inputs, queue it to build
 * on FBuildSession, which uses IBuildInputProvider to fetch the inputs at the expected versions.
 *
 * Build definitions are immutable, and are created by a builder from IBuild::CreateDefinition().
 *
 * @see FBuildDefinitionBuilder
 * @see FBuildSession
 */
class FBuildDefinition
{
public:
	/** Returns the key that uniquely identifies this build definition. */
	inline const FBuildKey& GetKey() const
	{
		return Definition->GetKey();
	}

	/** Returns the name by which to identify this definition for logging and profiling. */
	inline FStringView GetName() const
	{
		return Definition->GetName();
	}

	/** Returns the name of the build function with which to build this definition. */
	inline FStringView GetFunction() const
	{
		return Definition->GetFunction();
	}

	/** Visits every constant in order by key. */
	inline void IterateConstants(TFunctionRef<void (FStringView Key, FCbObject&& Value)> Visitor) const
	{
		return Definition->IterateConstants(Visitor);
	}

	/** Visits every input build payload in order by key. */
	inline void IterateInputBuilds(TFunctionRef<void (FStringView Key, const FBuildPayloadKey& PayloadKey)> Visitor) const
	{
		return Definition->IterateInputBuilds(Visitor);
	}

	/** Visits every input bulk data in order by key. */
	inline void IterateInputBulkData(TFunctionRef<void (FStringView Key, const FGuid& BulkDataId)> Visitor) const
	{
		return Definition->IterateInputBulkData(Visitor);
	}

	/** Visits every input file in order by key. */
	inline void IterateInputFiles(TFunctionRef<void (FStringView Key, FStringView Path)> Visitor) const
	{
		return Definition->IterateInputFiles(Visitor);
	}

	/** Visits every input hash in order by key. */
	inline void IterateInputHashes(TFunctionRef<void (FStringView Key, const FIoHash& RawHash)> Visitor) const
	{
		return Definition->IterateInputHashes(Visitor);
	}

	/** Saves the build definition to a compact binary object. Calls BeginObject and EndObject. */
	inline void Save(FCbWriter& Writer) const
	{
		return Definition->Save(Writer);
	}

private:
	friend FBuildDefinition Private::CreateBuildDefinition(Private::IBuildDefinitionInternal* Definition);

	/** Construct a build definition. Use Build() on a builder from IBuild::CreateDefinition(). */
	inline explicit FBuildDefinition(Private::IBuildDefinitionInternal* InDefinition)
		: Definition(InDefinition)
	{
	}

	TRefCountPtr<Private::IBuildDefinitionInternal> Definition;
};

/**
 * A build definition builder is used to construct a build definition.
 *
 * Create using IBuild::CreateDefinition() which must be given a build function name.
 *
 * @see FBuildDefinition
 */
class FBuildDefinitionBuilder
{
public:
	/** Add a constant object with a key that is unique within this definition. */
	inline void AddConstant(FStringView Key, const FCbObject& Value)
	{
		DefinitionBuilder->AddConstant(Key, Value);
	}

	/** Add a payload from another build with a key that is unique within this definition. */
	inline void AddInputBuild(FStringView Key, const FBuildPayloadKey& PayloadKey)
	{
		DefinitionBuilder->AddInputBuild(Key, PayloadKey);
	}

	/**
	 * Add a bulk data input with a key that is unique within this definition.
	 *
	 * @param BulkDataId   Identifier that uniquely identifies this data in the IBuildInputProvider.
	 */
	inline void AddInputBulkData(FStringView Key, const FGuid& BulkDataId)
	{
		DefinitionBuilder->AddInputBulkData(Key, BulkDataId);
	}

	/**
	 * Add a file input with a key that is unique within this definition.
	 *
	 * @param Path   Path to the file relative to a mounted content root.
	 */
	inline void AddInputFile(FStringView Key, FStringView Path)
	{
		DefinitionBuilder->AddInputFile(Key, Path);
	}

	/**
	 * Add a hash input with a key that is unique within this definition.
	 *
	 * @param RawHash   Hash of the raw data that will resolve it in the IBuildInputProvider.
	 */
	inline void AddInputHash(FStringView Key, const FIoHash& RawHash)
	{
		DefinitionBuilder->AddInputHash(Key, RawHash);
	}

	/**
	 * Build a build definition, which makes this builder subsequently unusable.
	 */
	inline FBuildDefinition Build()
	{
		return DefinitionBuilder->Build();
	}

private:
	friend FBuildDefinitionBuilder Private::CreateBuildDefinitionBuilder(Private::IBuildDefinitionBuilderInternal* DefinitionBuilder);

	/** Construct a build definition builder. Use IBuild::CreateDefinition(). */
	inline explicit FBuildDefinitionBuilder(Private::IBuildDefinitionBuilderInternal* InDefinitionBuilder)
		: DefinitionBuilder(InDefinitionBuilder)
	{
	}

	TUniquePtr<Private::IBuildDefinitionBuilderInternal> DefinitionBuilder;
};

/**
 * A build definition provider finds a definition from its key.
 */
class IBuildDefinitionProvider
{
public:
	/** Returns a definition matching the key, or a definition with an empty key on error. */
	virtual FBuildDefinition GetDefinition(const FBuildKey& Key) = 0;
};

} // UE::DerivedData
