// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/ArrayView.h"
#include "Containers/StringView.h"
#include "Templates/RefCounting.h"
#include "Templates/UniquePtr.h"

class FCbObject;
class FCbWriter;

template <typename FuncType> class TFunctionRef;

namespace UE::DerivedData { class FBuildOutput; }
namespace UE::DerivedData { class FBuildOutputBuilder; }
namespace UE::DerivedData { class FCacheRecordBuilder; }
namespace UE::DerivedData { class FPayload; }
namespace UE::DerivedData { struct FBuildDiagnostic; }
namespace UE::DerivedData { struct FPayloadId; }

namespace UE::DerivedData::Private
{

class IBuildOutputInternal
{
public:
	virtual ~IBuildOutputInternal() = default;
	virtual FStringView GetName() const = 0;
	virtual FStringView GetFunction() const = 0;
	virtual const FCbObject& GetMeta() const = 0;
	virtual bool HasError() const = 0;
	virtual void IterateDiagnostics(TFunctionRef<void (const FBuildDiagnostic& Diagnostic)> Visitor) const = 0;
	virtual const FPayload& GetPayload(const FPayloadId& Id) const = 0;
	virtual TConstArrayView<FPayload> GetPayloads() const = 0;
	virtual void Save(FCbWriter& Writer) const = 0;
	virtual void Save(FCacheRecordBuilder& RecordBuilder) const = 0;
	virtual void AddRef() const = 0;
	virtual void Release() const = 0;
};

FBuildOutput CreateBuildOutput(IBuildOutputInternal* Output);
const IBuildOutputInternal* GetBuildOutput(const FBuildOutput& Output);

class IBuildOutputBuilderInternal
{
public:
	virtual ~IBuildOutputBuilderInternal() = default;
	virtual void SetMeta(FCbObject&& Meta) = 0;
	virtual bool HasError() const = 0;
	virtual void AddDiagnostic(const FBuildDiagnostic& Diagnostic) = 0;
	virtual void AddPayload(const FPayload& Payload) = 0;
	virtual FBuildOutput Build() = 0;
};

FBuildOutputBuilder CreateBuildOutputBuilder(IBuildOutputBuilderInternal* OutputBuilder);

} // UE::DerivedData::Private

namespace UE::DerivedData
{

/** Level of severity for build diagnostics. */
enum class EBuildDiagnosticLevel : uint8
{
	/** Errors always indicates a failure of the corresponding build. */
	Error,
	/** Warnings are expected to be actionable issues found while executing a build. */
	Warning,
};

/** A build diagnostic is a message logged by a build. */
struct FBuildDiagnostic
{
	/** The name of the category of the diagnostic. */
	FStringView Category;
	/** The message of the diagnostic. */
	FStringView Message;
	/** The level of severity of the diagnostic. */
	EBuildDiagnosticLevel Level;
};

/**
 * A build output contains the payloads and diagnostics produced by a build.
 *
 * The output will not contain any payloads if it has any errors.
 *
 * The output can be requested without data, which means the payloads will have null data.
 */
class FBuildOutput
{
public:
	/** Returns the name of the build definition that produced this output. */
	inline FStringView GetName() const { return Output->GetName(); }

	/** Returns the name of the build function that produced this output. */
	inline FStringView GetFunction() const { return Output->GetFunction(); }

	/** Returns the optional metadata. */
	inline const FCbObject& GetMeta() const { return Output->GetMeta(); }

	/** Returns whether the output has any error diagnostics. */
	inline bool HasError() const { return Output->HasError(); }

	/** Returns the payloads in the output in order by ID. */
	inline TConstArrayView<FPayload> GetPayloads() const { return Output->GetPayloads(); }

	/** Returns the payload matching the ID. Null if no match. Buffer is null if skipped. */
	inline const FPayload& GetPayload(const FPayloadId& Id) const { return Output->GetPayload(Id); }

	/** Visits every diagnostic in the order it was recorded. */
	inline void IterateDiagnostics(TFunctionRef<void (const FBuildDiagnostic& Diagnostic)> Visitor) const
	{
		Output->IterateDiagnostics(Visitor);
	}

	/** Saves the build output to a compact binary object with payloads as attachments. */
	void Save(FCbWriter& Writer) const
	{
		Output->Save(Writer);
	}

	/** Saves the build output to a cache record. */
	void Save(FCacheRecordBuilder& RecordBuilder) const
	{
		Output->Save(RecordBuilder);
	}

private:
	friend FBuildOutput Private::CreateBuildOutput(Private::IBuildOutputInternal* Output);
	friend const Private::IBuildOutputInternal* Private::GetBuildOutput(const FBuildOutput& Output);

	inline explicit FBuildOutput(Private::IBuildOutputInternal* InOutput)
		: Output(InOutput)
	{
	}

	TRefCountPtr<Private::IBuildOutputInternal> Output;
};

/**
 * A build output builder is used to construct a build output.
 *
 * Create using IBuild::CreateOutput() which must be given a build definition.
 *
 * @see FBuildOutput
 */
class FBuildOutputBuilder
{
public:
	/** Returns whether the output has any error diagnostics. */
	inline bool HasError() const
	{
		return OutputBuilder->HasError();
	}

	/** Set the metadata for the build output. Holds a reference and is cloned if not owned. */
	inline void SetMeta(FCbObject&& Meta)
	{
		return OutputBuilder->SetMeta(MoveTemp(Meta));
	}

	/** Add a payload to the output. The ID must be unique in this output. */
	inline void AddPayload(const FPayload& Payload)
	{
		OutputBuilder->AddPayload(Payload);
	}

	/** Add an error diagnostic to the output. */
	inline void AddError(FStringView Category, FStringView Message)
	{
		OutputBuilder->AddDiagnostic({Category, Message, EBuildDiagnosticLevel::Error});
	}

	/** Add a warning diagnostic to the output. */
	inline void AddWarning(FStringView Category, FStringView Message)
	{
		OutputBuilder->AddDiagnostic({Category, Message, EBuildDiagnosticLevel::Warning});
	}

	/** Build a build output, which makes this builder subsequently unusable. */
	inline FBuildOutput Build()
	{
		return OutputBuilder->Build();
	}

private:
	friend FBuildOutputBuilder Private::CreateBuildOutputBuilder(Private::IBuildOutputBuilderInternal* OutputBuilder);

	/** Construct a build output builder. Use IBuild::CreateOutput(). */
	inline explicit FBuildOutputBuilder(Private::IBuildOutputBuilderInternal* InOutputBuilder)
		: OutputBuilder(InOutputBuilder)
	{
	}

	TUniquePtr<Private::IBuildOutputBuilderInternal> OutputBuilder;
};

} // UE::DerivedData
