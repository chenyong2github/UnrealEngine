// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ConcertMessageData.h"

class FConcertSyncSessionDatabase;

namespace UE::ConcertSyncTests::RenameEditAndDeleteMapsFlowTest
{
	enum ETestActivity
	{
		_1_NewPackageFoo,
		_1_SavePackageFoo,
		_2_AddActor,
		_3_RenameActor,
		_4_EditActor,
		_5_SavePackageBar,
		_5_RenameFooToBar,
		_6_EditActor,
		_7_DeleteBar,
		_8_NewPackageFoo,
		_8_SavePackageFoo,
		
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