// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ConcertMessageData.h"

class FConcertSyncSessionDatabase;

namespace UE::ConcertSyncTests::RenameEditAndDeleteMapsFlowTest
{
	enum ETestActivity
	{
		// Redundant = are visual finding aid during debugging
		_1_NewPackageFoo	= 0,
		_1_SavePackageFoo	= 1,
		_2_AddActor			= 2,
		_3_RenameActor		= 3,
		_4_EditActor		= 4,
		_5_SavePackageBar	= 5,
		_5_RenameFooToBar	= 6,
		_6_EditActor		= 7,
		_7_DeleteBar		= 8,
		_8_NewPackageFoo	= 9,
		_8_SavePackageFoo	= 10,
		
		ActivityCount
	};
	TSet<ETestActivity> AllActivities();
	FString LexToString(ETestActivity Activity);
	
	/** An array where every entry of ETestActivity is a valid index. */
	template<typename T>
	using TTestActivityArray = TArray<T, TInlineAllocator<ActivityCount>>;

	/**
	 * Creates a session history which resembles the following sequence of user actions:
	 *	1 Create map Foo
	 *	2 Add actor A
	 *	3 Edit actor A
	 *	4 Edit actor A
	 *	5 Rename map to Bar
	 *	6 Edit actor A
	 *	7 Delete map Bar
	 *	8 Create map Bar
	 *
	 *	@return An array where every ETestActivity entry is the index to the activity ID added to SessionDatabase
	 */
	TTestActivityArray<FActivityID> CreateActivityHistory(FConcertSyncSessionDatabase& SessionDatabase, const FGuid& EndpointID);
}