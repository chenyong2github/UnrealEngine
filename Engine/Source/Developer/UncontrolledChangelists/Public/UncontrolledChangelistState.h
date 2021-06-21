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
		NotCheckedOut	= 1 << 1,

		/** All the above checks */
		All = Modified | NotCheckedOut,
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
	
	UNCONTROLLEDCHANGELISTS_API const TSet<FString>& GetOfflineFiles() const;

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
	 * @return 	True if a change has been performed in the Uncontrolled Changelist State.
	 */
	bool AddFiles(const TArray<FString>& InFilenames, const ECheckFlags InCheckFlags);

	/**
	 * Removes files from this Uncontrolled Changelist State if present.
	 * @param 	InFileStates 	The files to be removed.
	 * @return 	True if a change has been performed in the Uncontrolled Changelist State.
	 */
	bool RemoveFiles(const TArray<FSourceControlStateRef>& InFileStates);

	/**
	 * Updates the status of all files contained in this changelist.
	 * @return 	True if the state has been modified.
	 */
	bool UpdateStatus();

	/**
	 * Removes files present both in the Uncontrolled Changelist and the provided set.
	 * @param 	InOutAddedAssets 	A Set representing Added Assets to check against.
	 */
	void RemoveDuplicates(TSet<FString>& InOutAddedAssets);

public:
	FUncontrolledChangelist Changelist;
	FString Description;
	TSet<FSourceControlStateRef> Files;
	TSet<FString> OfflineFiles;

	/** The timestamp of the last update */
	FDateTime TimeStamp;
};

ENUM_CLASS_FLAGS(FUncontrolledChangelistState::ECheckFlags);
typedef TSharedPtr<FUncontrolledChangelistState, ESPMode::ThreadSafe> FUncontrolledChangelistStatePtr;
typedef TSharedRef<FUncontrolledChangelistState, ESPMode::ThreadSafe> FUncontrolledChangelistStateRef;
