// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDLayersViewModel.h"

#include "USDLayerUtils.h"
#include "USDMemory.h"
#include "USDTypesConversion.h"

#include "UsdWrappers/SdfLayer.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"
	#include "pxr/pxr.h"
	#include "pxr/usd/pcp/cache.h"
	#include "pxr/usd/pcp/layerStack.h"
	#include "pxr/usd/sdf/layer.h"
	#include "pxr/usd/sdf/layerUtils.h"
	#include "pxr/usd/sdf/layerTree.h"
	#include "pxr/usd/usd/stage.h"
	#include "pxr/usd/usdUtils/dependencies.h"
#include "USDIncludesEnd.h"

#endif // #if USE_USD_SDK

FUsdLayerViewModel::FUsdLayerViewModel( FUsdLayerViewModel* InParentItem, const UE::FUsdStage& InUsdStage, const FString& InLayerIdentifier )
	: LayerModel( MakeShared< FUsdLayerModel >() )
	, ParentItem( InParentItem )
	, UsdStage( InUsdStage )
	, LayerIdentifier( InLayerIdentifier )
{
	RefreshData();
}

bool FUsdLayerViewModel::IsValid() const
{
	return ( bool ) UsdStage && ( !ParentItem || !ParentItem->LayerIdentifier.Equals( LayerIdentifier, ESearchCase::Type::IgnoreCase ) );
}

TArray< TSharedRef< FUsdLayerViewModel > > FUsdLayerViewModel::GetChildren()
{
	if ( !IsValid() )
	{
		return {};
	}

	bool bNeedsRefresh = false;

#if USE_USD_SDK
	{
		FScopedUsdAllocs UsdAllocs;

		pxr::SdfLayerRefPtr UsdLayer( GetLayer() );

		if ( UsdLayer )
		{
			int32 SubLayerIndex = 0;
			for ( const std::string& SubLayerPath : UsdLayer->GetSubLayerPaths() )
			{
				const FString SubLayerIdentifier = UsdToUnreal::ConvertString( pxr::SdfComputeAssetPathRelativeToLayer( UsdLayer, SubLayerPath ) );

				if ( !Children.IsValidIndex( SubLayerIndex ) || !Children[ SubLayerIndex ]->LayerIdentifier.Equals( SubLayerIdentifier, ESearchCase::Type::IgnoreCase ) )
				{
					Children.Reset();
					bNeedsRefresh = true;
					break;
				}

				++SubLayerIndex;
			}

			if ( !bNeedsRefresh && SubLayerIndex < Children.Num() )
			{
				Children.Reset();
				bNeedsRefresh = true;
			}
		}
	}
#endif // #if USE_USD_SDK

	if ( bNeedsRefresh )
	{
		FillChildren();
	}

	return Children;
}

void FUsdLayerViewModel::FillChildren()
{
	Children.Reset();

	if ( !IsValid() )
	{
		return;
	}

#if USE_USD_SDK
	{
		FScopedUsdAllocs UsdAllocs;

		pxr::SdfLayerRefPtr UsdLayer( GetLayer() );

		if ( UsdLayer )
		{
			TSet< FString > AllLayerIdentifiers;

			FUsdLayerViewModel* CurrentItem = this;
			while ( CurrentItem )
			{
				AllLayerIdentifiers.Add( CurrentItem->LayerIdentifier );
				CurrentItem = CurrentItem->ParentItem;
			}

			for ( std::string SubLayerPath : UsdLayer->GetSubLayerPaths() )
			{
				FString AssetPathRelativeToLayer = UsdToUnreal::ConvertString( pxr::SdfComputeAssetPathRelativeToLayer( UsdLayer, SubLayerPath ) );

				// Prevent infinite recursions if a sublayer refers to a parent of the same hierarchy
				if ( !AllLayerIdentifiers.Contains( AssetPathRelativeToLayer ) )
				{
					Children.Add( MakeShared< FUsdLayerViewModel >( this, UsdStage, AssetPathRelativeToLayer ) );
				}
			}
		}
	}
#endif // #if USE_USD_SDK
}

void FUsdLayerViewModel::RefreshData()
{
	if ( !IsValid() )
	{
		return;
	}

	Children = GetChildren();

#if USE_USD_SDK
	FScopedUsdAllocs UsdAllocs;

	pxr::UsdStageRefPtr UsdStageRef( UsdStage );

	const TUsdStore< std::string > UsdLayerIdentifier = UnrealToUsd::ConvertString( *LayerIdentifier );

	LayerModel->DisplayName = FText::FromString( UsdToUnreal::ConvertString( pxr::SdfLayer::GetDisplayNameFromIdentifier( UsdLayerIdentifier.Get() ) ) );
	LayerModel->bIsMuted = UsdStageRef->IsLayerMuted( UsdLayerIdentifier.Get() );

	const pxr::SdfLayerHandle& EditTargetLayer = UsdStageRef->GetEditTarget().GetLayer();
	LayerModel->bIsEditTarget = ( EditTargetLayer
		? FCStringAnsi::Stricmp( EditTargetLayer->GetIdentifier().c_str(), UsdLayerIdentifier.Get().c_str() ) == 0
		: false );

	for ( const TSharedRef< FUsdLayerViewModel >& Child : Children )
	{
		Child->RefreshData();
	}
#endif // #if USE_USD_SDK
}

UE::FSdfLayer FUsdLayerViewModel::GetLayer() const
{
	return UE::FSdfLayer::FindOrOpen( *LayerIdentifier );
}

bool FUsdLayerViewModel::CanMuteLayer() const
{
	if ( !IsValid() )
	{
		return false;
	}

	return !UsdStage.GetRootLayer().GetIdentifier().Equals( LayerIdentifier, ESearchCase::Type::IgnoreCase ) && !LayerModel->bIsEditTarget;
}

void FUsdLayerViewModel::ToggleMuteLayer()
{
	if ( !IsValid() || !CanMuteLayer() )
	{
		return;
	}

#if USE_USD_SDK
	FScopedUsdAllocs UsdAllocs;

	const TUsdStore< std::string > UsdLayerIdentifier = UnrealToUsd::ConvertString( *LayerIdentifier );

	pxr::UsdStageRefPtr UsdStageRef( UsdStage );

	if ( UsdStageRef->IsLayerMuted( UsdLayerIdentifier.Get() ) )
	{
		UsdStageRef->UnmuteLayer( UsdLayerIdentifier.Get() );
	}
	else
	{
		UsdStageRef->MuteLayer( UsdLayerIdentifier.Get() );
	}

	RefreshData();
#endif // #if USE_USD_SDK
}

bool FUsdLayerViewModel::CanEditLayer() const
{
	return !LayerModel->bIsMuted;
}

bool FUsdLayerViewModel::EditLayer()
{
	UE::FSdfLayer Layer( GetLayer() );
	if ( !UsdStage || !Layer || !CanEditLayer() )
	{
		return false;
	}

	UsdStage.SetEditTarget( Layer );
	RefreshData();

	return true;
}

void FUsdLayerViewModel::AddSubLayer( const TCHAR* SubLayerIdentifier )
{
#if USE_USD_SDK
	FScopedUsdAllocs UsdAllocs;
	UsdUtils::InsertSubLayer( pxr::SdfLayerRefPtr( GetLayer() ), SubLayerIdentifier );
#endif // #if USE_USD_SDK
}

void FUsdLayerViewModel::NewSubLayer( const TCHAR* SubLayerIdentifier )
{
#if USE_USD_SDK
	FScopedUsdAllocs UsdAllocs;
	UsdUtils::CreateNewLayer( pxr::UsdStageRefPtr( UsdStage ), pxr::SdfLayerRefPtr( GetLayer() ), SubLayerIdentifier );
#endif // #if USE_USD_SDK
}

bool FUsdLayerViewModel::RemoveSubLayer( int32 SubLayerIndex )
{
	bool bLayerRemoved = false;

#if USE_USD_SDK
	FScopedUsdAllocs UsdAllocs;

	pxr::SdfLayerRefPtr UsdLayer( GetLayer() );

	if ( UsdLayer )
	{
		UsdLayer->RemoveSubLayerPath( SubLayerIndex );
		bLayerRemoved = true;
	}
#endif // #if USE_USD_SDK

	return bLayerRemoved;
}
