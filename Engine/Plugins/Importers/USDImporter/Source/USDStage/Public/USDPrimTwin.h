// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Containers/Map.h"
#include "Containers/UnrealString.h"
#include "Delegates/Delegate.h"
#include "Delegates/DelegateCombinations.h"
#include "Templates/UniquePtr.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "UObject/SoftObjectPtr.h"

#include "USDPrimTwin.generated.h"

/** The Unreal equivalent (twin) of a USD prim */
UCLASS( Transient )
class UUsdPrimTwin final : public UObject
{
	GENERATED_BODY()

public:
	UUsdPrimTwin& AddChild( const FString& InPrimPath );
	void RemoveChild( const TCHAR* InPrimPath );

	void Clear();

	void Iterate( TFunction< void( UUsdPrimTwin& ) > Func, bool bRecursive )
	{
		for ( TMap< FString, UUsdPrimTwin* >::TIterator It = Children.CreateIterator(); It; ++It )
		{
			UUsdPrimTwin* Child = It->Value;

			Func( *Child );

			if ( bRecursive )
			{
				Child->Iterate( Func, bRecursive );
			}
		}
	}

	UUsdPrimTwin* Find( const FString& InPrimPath );

	DECLARE_EVENT_OneParam( UUsdPrimTwin, FOnUsdPrimTwinDestroyed, const UUsdPrimTwin& );
	FOnUsdPrimTwinDestroyed OnDestroyed;

public:
	UPROPERTY()
	FString PrimPath;

	UPROPERTY()
	TSoftObjectPtr< class AActor > SpawnedActor;

	UPROPERTY()
	TWeakObjectPtr< class USceneComponent > SceneComponent;

private:
	UPROPERTY()
	TMap< FString, UUsdPrimTwin* > Children;
};
