// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDGeomXformableTranslator.h"

#include "USDConversionUtils.h"
#include "USDPrimConversion.h"
#include "USDTypesConversion.h"

#include "Components/SceneComponent.h"
#include "Engine/World.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"
	#include "pxr/usd/usd/prim.h"
	#include "pxr/usd/usdGeom/xformable.h"
#include "USDIncludesEnd.h"


FUsdGeomXformableTranslator::FUsdGeomXformableTranslator( TSubclassOf< USceneComponent > InComponentTypeOverride, TSharedRef< FUsdSchemaTranslationContext > InContext, const pxr::UsdTyped& InSchema )
	: FUsdSchemaTranslator( InContext, InSchema )
	, ComponentTypeOverride( InComponentTypeOverride )
{
}

USceneComponent* FUsdGeomXformableTranslator::CreateComponents()
{
	TSubclassOf< USceneComponent > ComponentType;

	if ( ComponentTypeOverride.IsSet() )
	{
		ComponentType = ComponentTypeOverride.GetValue();
	}
	else
	{
		ComponentType = UsdUtils::GetComponentTypeForPrim( Schema.Get().GetPrim() );
	}

	return CreateComponents( ComponentType );
}

USceneComponent* FUsdGeomXformableTranslator::CreateComponents( TSubclassOf< USceneComponent > ComponentType )
{
	TUsdStore< pxr::UsdPrim > Prim = Schema.Get().GetPrim();
	const bool bNeedsActor = ( Context->ParentComponent == nullptr || Prim.Get().IsModel() || UsdUtils::HasCompositionArcs( Prim.Get() ) || Prim.Get().IsGroup() );

	return CreateComponents( ComponentType, bNeedsActor );
}

USceneComponent* FUsdGeomXformableTranslator::CreateComponents( TSubclassOf< USceneComponent > ComponentType, const bool bNeedsActor )
{
	if ( !Context->IsValid() )
	{
		return nullptr;
	}

	TUsdStore< pxr::UsdPrim > Prim = Schema.Get().GetPrim();
	
	if ( !Prim.Get() )
	{
		return nullptr;
	}

	FScopedUnrealAllocs UnrealAllocs;

	USceneComponent* SceneComponent = nullptr;
	UObject* ComponentOuter = nullptr;

	if ( bNeedsActor )
	{
		// Spawn actor
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.ObjectFlags = Context->ObjectFlags;
		SpawnParameters.OverrideLevel =  Context->Level;

		UClass* ActorClass = UsdUtils::GetActorTypeForPrim( Prim.Get() );
		AActor* SpawnedActor = Context->Level->GetWorld()->SpawnActor( ActorClass, nullptr, SpawnParameters );

		if ( SpawnedActor )
		{
			SpawnedActor->SetActorLabel( Prim.Get().GetName().GetText() );
			SpawnedActor->Tags.AddUnique( TEXT("SequencerActor") );	// Hack to show transient actors in world outliner

			SceneComponent = SpawnedActor->GetRootComponent();

			ComponentOuter = SpawnedActor;
		}
	}
	else
	{
		ComponentOuter = Context->ParentComponent;
	}
	
	if ( !SceneComponent )
	{
		SceneComponent = NewObject< USceneComponent >( ComponentOuter, ComponentType, FName( Prim.Get().GetName().GetText() ), Context->ObjectFlags );

		if ( AActor* Owner = SceneComponent->GetOwner() )
		{
			Owner->AddInstanceComponent( SceneComponent );
		}
	}

	if ( SceneComponent )
	{
		UpdateComponents( SceneComponent );

		if ( Context->ParentComponent && Context->ParentComponent->Mobility == EComponentMobility::Movable )
		{
			SceneComponent->SetMobility( EComponentMobility::Movable );
		}
		else
		{
			SceneComponent->SetMobility( UsdUtils::IsAnimated( Prim.Get() ) ? EComponentMobility::Movable : EComponentMobility::Static ); /*PrimsToAnimate.Contains( UsdToUnreal::ConvertPath( Path.GetPrimPath() ) */
		}

		// Attach to parent
		SceneComponent->AttachToComponent( Context->ParentComponent, FAttachmentTransformRules::KeepRelativeTransform );

		if ( !SceneComponent->GetOwner()->GetRootComponent() )
		{
			SceneComponent->GetOwner()->SetRootComponent( SceneComponent );
		}
	}

	return SceneComponent;
}

void FUsdGeomXformableTranslator::UpdateComponents( USceneComponent* SceneComponent )
{
	if ( SceneComponent )
	{
		FScopedUsdAllocs UsdAllocs;
		UsdToUnreal::ConvertXformable( Schema.Get().GetPrim().GetStage(), pxr::UsdGeomXformable( Schema.Get() ), *SceneComponent, pxr::UsdTimeCode( Context->Time ) );
	}
}

#endif // #if USE_USD_SDK
