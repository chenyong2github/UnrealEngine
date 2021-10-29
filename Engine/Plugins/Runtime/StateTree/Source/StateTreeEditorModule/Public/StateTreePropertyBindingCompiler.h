// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include "InstancedStruct.h"
#include "UObject/Interface.h"
#include "IPropertyAccessEditor.h"
#include "StateTreeEditorPropertyBindings.h"
#include "StateTreePropertyBindings.h"
#include "StateTreeCompilerLog.h"
#include "StateTreePropertyBindingCompiler.generated.h"

/**
 * Helper class to compile editor representation of property bindings into runtime representation.
 * TODO: Better error reporting, something that can be shown in the UI.
 */
USTRUCT()
struct STATETREEEDITORMODULE_API FStateTreePropertyBindingCompiler
{
	GENERATED_BODY()

	/**
	  * Initializes the compiler to compile copies to specified Property Bindings.
	  * @param PropertyBindings - Reference to the Property Bindings where all the batches will be stored.
	  * @return true on success.
	  */
	bool Init(FStateTreePropertyBindings& InPropertyBindings, FStateTreeCompilerLog& InLog);

	/**
	  * Compiles a batch of property copies.
	  * @param TargetStruct - Description of the structs which contains the target properties.
	  * @param PropertyBindings - Array of bindings to compile, all bindings that point to TargetStructs will be added to the batch.
	  * @param OutBatchIndex - Resulting batch index, if index is INDEX_NONE, no bindings were found and no batch was generated.
	  * @return True on success, false on failure.
	 */
	bool CompileBatch(const FStateTreeBindableStructDesc& TargetStruct, TConstArrayView<FStateTreeEditorPropertyBinding> EditorPropertyBindings, int32& OutBatchIndex);

	/** Finalizes compilation, should be called once all batches are compiled. */
	void Finalize();

	/**
	  * Adds source struct. When compiling a batch, the bindings can be between any of the defined source structs, and the target struct.
	  * Source structs can be added between calls to Compilebatch().
	  * @param SourceStruct - Description of the source struct to add.
	  * @return Source struct index.
	  */
	int32 AddSourceStruct(const FStateTreeBindableStructDesc& SourceStruct);

	/** @return Index of a source struct by specified ID, or INDEX_NONE if not found. */
	int32 GetSourceStructIndexByID(const FGuid& ID) const;

	/** @return Reference to a source struct based on ID. */
	const FStateTreeBindableStructDesc& GetSourceStructDesc(const int32 Index) const
	{
		return SourceStructs[Index];
	}

	/**
	 * Resolves a string based property path in specified struct into segments of property names and access types.
	 * If logging is required both, Log and LogContextStruct needs to be non-null.
	 * @param InStructDesc Description of the struct in which the property path is valid.
	 * @param InPath The property path in string format.
	 * @param OutSegments The resolved property access path as segments.
	 * @param OutLeafProperty The leaf property of the resolved path.
	 * @param OutLeafArrayIndex The left array index (or INDEX_NONE if not applicable) of the resolved path.
	 * @param Log Pointer to compiler log, or null if no logging needed.
	 * @param LogContextStruct Pointer to bindable struct desc where the property path belongs to.
	 * @return True of the property was solved successfully.
	 */
	static bool ResolvePropertyPath(const FStateTreeBindableStructDesc& InStructDesc, const FStateTreeEditorPropertyPath& InPath,
									TArray<FStateTreePropertySegment>& OutSegments, const FProperty*& OutLeafProperty, int32& OutLeafArrayIndex,
									FStateTreeCompilerLog* Log = nullptr, const FStateTreeBindableStructDesc* LogContextStruct = nullptr);

	/**
	 * Checks if two property types can are compatible for copying.
	 * @param InPropertyA First property used in the check.
	 * @param InPropertyB Second property used in the check.
	 * @return Incompatible if the properties cannot be copied, Compatible if they are trivially copyable, or Promotable if numeric values can be promoted to another numeric type.
	 */
	static EPropertyAccessCompatibility GetPropertyCompatibility(const FProperty* InPropertyA, const FProperty* InPropertyB);
	
protected:

	void StoreSourceStructs();

	UPROPERTY()
	TArray<FStateTreeBindableStructDesc> SourceStructs;

	FStateTreePropertyBindings* PropertyBindings = nullptr;

	FStateTreeCompilerLog* Log = nullptr;
	
	struct FResolvedPathResult
	{
		int32 PathIndex = INDEX_NONE;
		const FProperty* LeafProperty = nullptr;
		int32 LeafArrayIndex = INDEX_NONE;
	};

	EStateTreePropertyCopyType GetCopyType(const FProperty* SourceProperty, const int32 SourceArrayIndex, const FProperty* TargetProperty, const int32 TargetArrayIndex);
	bool ResolvePropertyPath(const FStateTreeBindableStructDesc& InOwnerStructDesc, const FStateTreeBindableStructDesc& InStructDesc, const FStateTreeEditorPropertyPath& InPath, FResolvedPathResult& OutResult);
};
