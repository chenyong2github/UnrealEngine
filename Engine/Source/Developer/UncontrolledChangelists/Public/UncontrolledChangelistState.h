// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ISourceControlChangelistState.h"
#include "ISourceControlOperation.h"
#include "ISourceControlProvider.h"

#include "UncontrolledChangelist.h"

class FUncontrolledChangelistState : public TSharedFromThis<FUncontrolledChangelistState, ESPMode::ThreadSafe>
{
public:
	static constexpr const TCHAR* FILES_NAME = TEXT("files");

	enum class ECheckFlags
	{
		/** No Check */
		None			= 0,

		/** File has been modified */
		Modified		= 1,

		/** File is not checked out */
		NotCheckedOut	= 1 << 1
	};

public:
	FUncontrolledChangelistState(const FUncontrolledChangelist& InUncontrolledChangelist);

	/**
	 * Get the name of the icon graphic we should use to display the state in a UI.
	 * @returns the name of the icon to display
	 */
	UNCONTROLLEDCHANGELISTS_API FName GetIconName() const;

	/**
	 * Get the name of the small icon graphic we should use to display the state in a UI.
	 * @returns the name of the icon to display
	 */
	UNCONTROLLEDCHANGELISTS_API FName GetSmallIconName() const;

	/**
	 * Get a text representation of the state
	 * @returns	the text to display for this state
	 */
	UNCONTROLLEDCHANGELISTS_API FText GetDisplayText() const;

	/**
	 * Get a text representation of the state
	 * @returns	the text to display for this state
	 */
	UNCONTROLLEDCHANGELISTS_API FText GetDescriptionText() const;

	/**
	 * Get a tooltip to describe this state
	 * @returns	the text to display for this states tooltip
	 */
	UNCONTROLLEDCHANGELISTS_API FText GetDisplayTooltip() const;

	/**
	 * Get the timestamp of the last update that was made to this state.
	 * @returns	the timestamp of the last update
	 */
	const FDateTime& GetTimeStamp() const;

	UNCONTROLLEDCHANGELISTS_API const TSet<FSourceControlStateRef>& GetFilesStates() const;

	/**
	 * Serialize the state of the Uncontrolled Changelist to a Json Object.
	 * @param 	OutJsonObject 	The Json object used to serialize.
	 */
	void Serialize(TSharedRef<class FJsonObject> OutJsonObject) const;

	/**
	 * Deserialize the state of the Uncontrolled Changelist from a Json Object.
	 * @param 	InJsonValue 	The Json Object to read from.
	 * @return 	True if Deserialization succeeded.
	 */
	bool Deserialize(const TSharedRef<FJsonObject> InJsonValue);

	/**
	 * Adds files to this Uncontrolled Changelist State.
	 * @param 	InFilenames		The files to be added.
	 * @param 	InCheckFlags 	Tells which checks have to pass to add a file.
	 */	
	void AddFiles(const TArray<FString>& InFilenames, const ECheckFlags InCheckFlags);

	/**
	 * Removes files from this Uncontrolled Changelist State if present.
	 * @param 	InFileStates 	The files to be removed.
	 */
	void RemoveFiles(const TArray<FSourceControlStateRef>& InFileStates);

	/**
	 * Updates the status of all files.
	 */
	void UpdateStatus();

private:

	/**
	 * Called upon completion of FStatus operation
	 * @param 	InOperation 	The operation just completed.
	 * @param 	InResult 		The result of the operation.
	 */
	void OnUpdateStatusCompleted(const FSourceControlOperationRef& InOperation, ECommandResult::Type InResult);

public:
	FUncontrolledChangelist Changelist;

	FString Description;

	TSet<FSourceControlStateRef> Files;

	/** The timestamp of the last update */
	FDateTime TimeStamp;
};

ENUM_CLASS_FLAGS(FUncontrolledChangelistState::ECheckFlags);
typedef TSharedRef<FUncontrolledChangelistState, ESPMode::ThreadSafe> FUncontrolledChangelistStateRef;
