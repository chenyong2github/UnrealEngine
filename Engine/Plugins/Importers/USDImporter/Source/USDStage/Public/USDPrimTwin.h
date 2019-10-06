// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Containers/Map.h"
#include "Containers/UnrealString.h"
#include "Delegates/Delegate.h"
#include "Delegates/DelegateCombinations.h"
#include "Templates/UniquePtr.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

/** The Unreal equivalent (twin) of a USD prim */
class FUsdPrimTwin final
{
public:
	FUsdPrimTwin() = default;

	FUsdPrimTwin( FUsdPrimTwin&& Other ) = default;
	FUsdPrimTwin& operator=( FUsdPrimTwin&& Other ) = default;
	FUsdPrimTwin( const FUsdPrimTwin& Other ) = delete;
	FUsdPrimTwin& operator=( const FUsdPrimTwin& Other ) = delete;
	~FUsdPrimTwin() { Clear(); }

	FUsdPrimTwin& AddChild( const FString& InPrimPath );
	void RemoveChild( const TCHAR* InPrimPath );

	void Clear();

	void Iterate( TFunction< void( FUsdPrimTwin& ) > Func, bool bRecursive )
	{
		for ( TMap< FString, TUniquePtr< FUsdPrimTwin > >::TIterator It = Children.CreateIterator(); It; ++It )
		{
			FUsdPrimTwin* Child = It->Value.Get();

			Func( *Child );

			if ( bRecursive )
			{
				Child->Iterate( Func, bRecursive );
			}
		}
	}

	FUsdPrimTwin* Find( const FString& InPrimPath );

	DECLARE_EVENT_OneParam( FUsdPrimTwin, FOnUsdPrimTwinDestroyed, const FUsdPrimTwin& );
	FOnUsdPrimTwinDestroyed OnDestroyed;

public:
	FString PrimPath;
	TWeakObjectPtr< class AActor > SpawnedActor;
	TWeakObjectPtr< class USceneComponent > SceneComponent;

	FDelegateHandle AnimationHandle;

private:
	TMap< FString, TUniquePtr< FUsdPrimTwin > > Children;

};
