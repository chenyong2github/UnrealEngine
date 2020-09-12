// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDMXNamedType.h"

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtrTemplates.h"

#include "JsonObjectConverter.h"

class FDMXEditor;
class FDMXFixtureTypeSharedData;
class UDMXEntityFixtureType;
class IPropertyHandleArray;
class IPropertyHandle;
class FScopedTransaction;


/**
 * Item to identify and access a DMXFixtureMode.
 *
 * Note, this will no longer point to the live property when:
 * - The fixture types being edited change
 * - The details view is destroyed (be it via refresh or otherwise)
 * - The arrays that contain the property change num elements.
 */
class FDMXFixtureModeItem
	: public IDMXNamedType
{
public:
	/** Constructor */
	FDMXFixtureModeItem(const TWeakPtr<FDMXFixtureTypeSharedData>& InSharedDataPtr, const TSharedPtr<IPropertyHandle>& InModeNameHandle);
	
	/** Destructor */
	virtual ~FDMXFixtureModeItem();

public:
	/** Returns true if the mode names equal */
	bool EqualsMode(const TSharedPtr<FDMXFixtureModeItem>& Other) const;

	/** Gets the name of the mode */
	FString GetModeName() const;

	/** Returns the current mode name */
	void SetModeName(const FString& InDesiredName, FString& OutNewName);

	/** True if the mode is being edited by shared data */
	bool IsModeBeingEdited() const;

	/** True if the mode is selected by shared data */
	bool IsModeSelected() const;

	/** Selects the mode in shared data */
	void SelectMode(bool bSelected);

	/** Returns the outer fixture types of the mode name handle */
	void GetOuterFixtureTypes(TArray<TWeakObjectPtr<UDMXEntityFixtureType>>& OuterFixtureTypes) const;

protected:
	// IDMXNamedType Interface
	virtual void GetName(FString& OutUniqueName) const;
	virtual bool IsNameUnique(const FString& TestedName) const;
	virtual void SetName(const FString& InDesiredName, FString& OutUniqueName);
	// ~IDMXNamedType Interface

	/** Shared data that owns this */
	TWeakPtr<FDMXFixtureTypeSharedData> SharedDataPtr;

private:
	/** The mode name handle */
	TSharedPtr<IPropertyHandle> ModeNameHandle;
};


/**
 * Item to identify and access a DMXFixtureFunction.
 *
 * Note, this will no longer point to the live property when:
 * - The fixture types being edited change
 * - The details view is destroyed (be it via refresh or otherwise)
 * - The arrays that contain the property change num elements.
 */
class FDMXFixtureFunctionItem
	: public FDMXFixtureModeItem
{
public:
	FDMXFixtureFunctionItem(TWeakPtr<FDMXFixtureTypeSharedData> InOwnerPtr, const TSharedPtr<IPropertyHandle> InModeNameHandle, const TSharedPtr<IPropertyHandle> InFunctionHandle);

	virtual ~FDMXFixtureFunctionItem();

	/** Returns true if the function names equal */
	bool EqualsFunction(const TSharedPtr<FDMXFixtureFunctionItem>& Other) const;

	/** Returns the current function name */
	FString GetFunctionName() const;

	/** Returns the current function channel */
	int32 GetFunctionChannel() const;

	/** Sets the function name */
	void SetFunctionName(const FString& InDesiredName, FString& OutNewName);

	/** True if the function is edited in shared data */
	bool IsFunctionEdited() const;

	/** True if the function is selected in shared data */
	bool IsFunctionSelected() const;

	/** Get property handle for the function */
	const TSharedPtr<IPropertyHandle>& GetFunctionHandle() { return FunctionHandle; }

	/** Get property handle for the function name */
	const TSharedPtr<IPropertyHandle>& GetFunctionNameHandle() { return FunctionNameHandle; }
protected:
	// IDMXNamedType Interface
	virtual void GetName(FString& OutUniqueName) const override;
	virtual bool IsNameUnique(const FString& TestedName) const override;
	virtual void SetName(const FString& InDesiredName, FString& OutUniqueName) override;
	// ~IDMXNamedType Interface

private:
	/** Property handle for the function name */
	TSharedPtr<IPropertyHandle> FunctionNameHandle;

	/** Property handle for the function channel */
	TSharedPtr<IPropertyHandle> FunctionChannelHandle;

	/** Property handle for the function */
	TSharedPtr<IPropertyHandle> FunctionHandle;
};


/** Shared data for fixture type editors. */
class FDMXFixtureTypeSharedData
	: public TSharedFromThis<FDMXFixtureTypeSharedData>
{
	friend class FDMXFixtureModeItem;
	friend class FDMXFixtureFunctionItem;

public:
	FDMXFixtureTypeSharedData(TWeakPtr<FDMXEditor> InDMXEditorPtr)
		: DMXEditorPtr(InDMXEditorPtr)
	{}	

public:
	/** Sets which fixture types are being edited in shared data */
	void SetFixtureTypesBeingEdited(const TArray<TWeakObjectPtr<UDMXEntityFixtureType>>& InFixtureTypesBeingEdited);

	/** Sets which modes are being edited */
	void SetModesBeingEdited(const TArray<TSharedPtr<FDMXFixtureModeItem>>& InModesBeingEdited) { ModesBeingEdited = InModesBeingEdited; }

	/** Sets which functions are being edited */
	void SetFunctionsBeingEdited(const TArray<TSharedPtr<FDMXFixtureFunctionItem>>& InFunctionsBeingEdited) { FunctionsBeingEdited = InFunctionsBeingEdited; }

	/** Selects specified modes */
	void SelectModes(const TArray<TSharedPtr<FDMXFixtureModeItem>>& InModeItems);

	/** Selects specified functions */
	void SelectFunctions(const TArray<TSharedPtr<FDMXFixtureFunctionItem>>& InFunctions);

	/** Returns the number of modes being edited */
	int32 GetNumSelectedModes() const;

	/** Returns the number of modes being edited */
	int32 GetNumSelectedFunctions() const;

	/** Broadcasts when selected modes changed */
	FSimpleMulticastDelegate OnModesSelected;

	/** Broadcasts when selected functions changed */
	FSimpleMulticastDelegate OnFunctionsSelected;

public:	
	/** True when a mode can be added (true, if one fixture type is edited) */
	bool CanAddMode() const;

	/** Adds a mode to edited fixture type */
	void AddMode(const FScopedTransaction& TransactionScope);

	/** Duplicates specified modes in the modes arrays they reside in */
	void DuplicateModes(const TArray<TSharedPtr<FDMXFixtureModeItem>>& ModesToDuplicate);

	/** Deletes specified modes from the modes arrays they reside in */
	void DeleteModes(const TArray<TSharedPtr<FDMXFixtureModeItem>>& ModesToDelete);

	/** Copies functions to clipboard */
	void CopyModesToClipboard(const TArray<TSharedPtr<FDMXFixtureModeItem>>& ModeItems);

	/** Pastes clipboard to specified functions*/
	void PasteClipboardToModes(const TArray<TSharedPtr<FDMXFixtureModeItem>>& ModeItems);

public:
	/** True when a function can be added (true, if one mode is selected) */
	bool CanAddFunction() const;

	/** Duplicates specified modes in the modes arrays they reside in */
	void DuplicateFunctions(const TArray<TSharedPtr<FDMXFixtureFunctionItem>>& FunctionsToDuplicate);

	/** Deletes specified modes from the modes arrays they reside in */
	void DeleteFunctions(const TArray<TSharedPtr<FDMXFixtureFunctionItem>>& FunctionsToDelete);

	/** Adds a function to selected mode, only if CanAddFunction */
	void AddFunctionToSelectedMode();

	/** Copies functions to clipboard */
	void CopyFunctionsToClipboard(const TArray<TSharedPtr<FDMXFixtureFunctionItem>>& FunctionItems);

	/** Pastes clipboard to specified functions*/
	void PasteClipboardToFunctions(const TArray<TSharedPtr<FDMXFixtureFunctionItem>>& FunctionItems);

protected:
	// Protected to prevent from copying name properties across detail customizations
	const TArray<TWeakObjectPtr<UDMXEntityFixtureType>>& GetFixtureTypesBeingEdited() const { return FixtureTypesBeingEdited; }
	const TArray<TSharedPtr<FDMXFixtureModeItem>>& GetModesBeingEdited() const { return ModesBeingEdited; }
	const TArray<TSharedPtr<FDMXFixtureFunctionItem>>& GetFunctionsBeingEdited() const { return FunctionsBeingEdited; }
	const TArray<TSharedPtr<FDMXFixtureModeItem>>& GetSelectedModes() const { return SelectedModes; }
	const TArray<TSharedPtr<FDMXFixtureFunctionItem>>& GetSelectedFunctions() const { return SelectedFunctions; }

private:
	/** Internal cache for multi mode copy/paste*/
	TArray<FString> ModesClipboard;

	/** Internal cache for multi function copy/paste*/
	TArray<FString> FunctionsClipboard;

private:
	/** The Fixture types being edited */
	TArray<TWeakObjectPtr<UDMXEntityFixtureType>> FixtureTypesBeingEdited;

	/** Modes being edited */
	TArray<TSharedPtr<FDMXFixtureModeItem>> ModesBeingEdited;

	/** Gunctions being edited */
	TArray<TSharedPtr<FDMXFixtureFunctionItem>> FunctionsBeingEdited;

	/** Selected modes in fixture types */
	TArray<TSharedPtr<FDMXFixtureModeItem>> SelectedModes;

	/** Selected functions in modes */
	TArray<TSharedPtr<FDMXFixtureFunctionItem>> SelectedFunctions;

	/** Weak reference to the DMX editor */
	TWeakPtr<FDMXEditor> DMXEditorPtr;	

protected:
	/** Serializes a struct */
	template <typename StructType>
	bool SerializeStruct(const StructType& Struct, FString& OutCopyStr) const
	{
		TSharedRef<FJsonObject> RootJsonObject = MakeShareable(new FJsonObject());

		FJsonObjectConverter::UStructToJsonObject(StructType::StaticStruct(), &Struct, RootJsonObject, 0, 0);

		typedef TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>> FStringWriter;
		typedef TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>> FStringWriterFactory;

		FString CopyStr;
		TSharedRef<FStringWriter> Writer = FStringWriterFactory::Create(&CopyStr);
		FJsonSerializer::Serialize(RootJsonObject, Writer);

		if (!CopyStr.IsEmpty())
		{
			OutCopyStr = CopyStr;
			return true;
		}

		return false;
	}
};
