// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Logging/TokenizedMessage.h"

class AActor;
class IDataprepProgressReporter;
class UStaticMesh;
class UWorld;

namespace DataprepCorePrivateUtils
{
	/**
	 * Move an array element to another spot
	 * This operation take O(n) time. Where n is the absolute value of SourceIndex - DestinationIndex
	 * @param SourceIndex The Index of the element to move
	 * @param DestinationIndex The index of where the element will be move to
	 * @return True if the element was move
	 */
	template<class TArrayClass>
	bool MoveArrayElement(TArrayClass& Array, int32 SourceIndex, int32 DestinationIndex)
	{
		if ( Array.IsValidIndex( SourceIndex ) && Array.IsValidIndex( DestinationIndex ) && SourceIndex != DestinationIndex )
		{
			// Swap the operation until all operation are at right position. O(n) where n is Upper - Lower 

			while ( SourceIndex < DestinationIndex )
			{
				Array.Swap( SourceIndex, DestinationIndex );
				DestinationIndex--;
			}
			
			while ( DestinationIndex < SourceIndex )
			{
				Array.Swap( DestinationIndex, SourceIndex );
				DestinationIndex++;
			}

			return true;
		}

		return false;
	}

	/**
	 * Mark the input object for kill and unregister it
	 * @param Asset		The object to be deleted
	 */
	void DeleteRegisteredAsset(UObject* Asset);

	/**
	 * Collect on the valid actors in the input World
	 * @param World			World to parse
	 * @param OutActors		Actors present in the world
	 */
	void GetActorsFromWorld( const UWorld* World, TArray<AActor*>& OutActors );

	/** Returns directory where to store temporary files when running Dataprep asset */
	const FString& GetRootTemporaryDir();

	/** Returns content folder where to create temporary assets when running Dataprep asset */
	const FString& GetRootPackagePath();

	/**
	 * Logs messages in output log and message panel using "Dataprep Core" label
	 * @param Severity				Severity of the message to log
	 * @param Message				Message to log
	 * @param NotificationText		Text of the notification to display to alert the user 
	 * @remark Notification is only done ifNotificationText is not empty
	 */
	void LogMessage(EMessageSeverity::Type Severity, const FText& Message, const FText& NotificationText = FText());

	/**
	 * Clear 
	 */
	void ClearAssets(const TArray< TWeakObjectPtr< UObject > >& Assets);

	/** Build the render data based on the current geometry available in the static mesh */
	void BuildStaticMeshes(TSet<UStaticMesh*>& StaticMeshes, TFunction<bool(UStaticMesh*)> ProgressFunction, bool bForceBuild = false);
}
