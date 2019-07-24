// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

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
}
