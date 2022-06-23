// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDPrimViewModel.h"

#include "USDConversionUtils.h"
#include "USDIntegrationUtils.h"
#include "USDLog.h"
#include "USDMemory.h"
#include "USDTypesConversion.h"

#include "UsdWrappers/SdfLayer.h"
#include "UsdWrappers/SdfPath.h"

#include "Misc/Paths.h"
#include "ScopedTransaction.h"

#include <iterator>

#if USE_USD_SDK

#include "USDIncludesStart.h"
	#include "pxr/pxr.h"
	#include "pxr/usd/sdf/namespaceEdit.h"
	#include "pxr/usd/sdf/path.h"
	#include "pxr/usd/usd/modelAPI.h"
	#include "pxr/usd/usd/prim.h"
	#include "pxr/usd/usd/references.h"
	#include "pxr/usd/usd/tokens.h"
	#include "pxr/usd/usdGeom/xform.h"
#include "USDIncludesEnd.h"

#endif // #if USE_USD_SDK

namespace UE::USDPrimViewModel::Private
{
#if USE_USD_SDK
	bool CanAddOrRemoveCustomSchema( pxr::UsdPrim Prim, pxr::TfToken SchemaName, bool bAddSchema )
	{
		if ( !Prim )
		{
			return false;
		}

		FScopedUsdAllocs UsdAllocs;

		pxr::TfType Schema = pxr::UsdSchemaRegistry::GetTypeFromSchemaTypeName( SchemaName );
		if ( !ensure( static_cast< bool >( Schema ) ) )
		{
			return false;
		}

		// Check if the schema is compatible with this prim
		if ( bAddSchema && !Prim.CanApplyAPI( Schema ) )
		{
			return false;
		}

		pxr::UsdStageRefPtr Stage = Prim.GetStage();
		pxr::SdfLayerRefPtr EditTarget = Stage->GetEditTarget().GetLayer();
		if ( !Stage || !EditTarget )
		{
			return false;
		}

		bool bAlreadyHasSchema = false;
		if ( EditTarget == Stage->GetRootLayer() )
		{
			bAlreadyHasSchema = Prim.HasAPI( Schema );
		}
		else
		{
			// In the future we'll have better layer editing facilities. For now, lets make sure that
			// we only show that we can setup/remove a schema for a prim if it can be done *at that
			// particular edit target*, which is what will happen when clicking the button anyway.
			// For example, a user could:
			//  - Add the schema on a sublayer;
			//  - Remove the schema on the root layer;
			//  - Select the sublayer as the edit target;
			// If we always just queried the existing stage, the user would see that they can add the schema
			// (because the composed prim does not have the schema) but clicking the button would do nothing,
			// as the sublayer prim spec already has the schema...

			pxr::SdfPrimSpecHandle PrimSpecOnLayer = EditTarget->GetPrimAtPath( Prim.GetPath() );
			if( !PrimSpecOnLayer )
			{
				// We can always just make a new 'over' on this layer
				return true;
			}

			// We can only add the schema if its not present on this layer, but we should let USD sort the
			// Ops out because we could have Ops to add/delete/prepend/append the same schema on the same prim...
			pxr::SdfListOp<pxr::TfToken> Ops = PrimSpecOnLayer->GetInfo( pxr::UsdTokens->apiSchemas ).Get<pxr::SdfTokenListOp>();
			std::vector<pxr::TfToken> AppliedOps;
			Ops.ApplyOperations(&AppliedOps);
			bAlreadyHasSchema = std::find( AppliedOps.begin(), AppliedOps.end(), SchemaName ) != AppliedOps.end();
		}

		return bAddSchema != bAlreadyHasSchema;
	}

	void RemoveCustomSchema( pxr::UsdPrim Prim, pxr::TfToken SchemaName )
	{
		if ( !Prim )
		{
			return;
		}

		FScopedUsdAllocs UsdAllocs;

		pxr::TfType Schema = pxr::UsdSchemaRegistry::GetTypeFromSchemaTypeName( SchemaName );
		ensure( static_cast< bool >( Schema ) );

		ensure( Prim.RemoveAPI( Schema ) );
	}

#endif // #if USE_USD_SDK
}

FUsdPrimViewModel::FUsdPrimViewModel( FUsdPrimViewModel* InParentItem, const UE::FUsdStageWeak& InUsdStage, const UE::FUsdPrim& InUsdPrim )
	: FUsdPrimViewModel( InParentItem, InUsdStage )
{
	UsdPrim = InUsdPrim;

	RefreshData( false );
	FillChildren();
}

FUsdPrimViewModel::FUsdPrimViewModel( FUsdPrimViewModel* InParentItem, const UE::FUsdStageWeak& InUsdStage )
	: UsdStage( InUsdStage )
	, ParentItem( InParentItem )
	, RowData( MakeShared< FUsdPrimModel >() )
{
}

TArray< FUsdPrimViewModelRef >& FUsdPrimViewModel::UpdateChildren()
{
	if ( !UsdPrim )
	{
		return Children;
	}

#if USE_USD_SDK
	FScopedUsdAllocs UsdAllocs;

	bool bNeedsRefresh = false;

	pxr::UsdPrimSiblingRange PrimChildren = pxr::UsdPrim( UsdPrim ).GetFilteredChildren( pxr::UsdTraverseInstanceProxies( pxr::UsdPrimAllPrimsPredicate ) );

	const int32 NumUsdChildren = (TArray< FUsdPrimViewModelRef >::SizeType )std::distance( PrimChildren.begin(), PrimChildren.end() );
	const int32 NumUnrealChildren = [&]()
	{
		int32 ValidPrims = 0;
		for ( const FUsdPrimViewModelRef& Child : Children )
		{
			if ( !Child->RowData->Name.IsEmpty() )
			{
				++ValidPrims;
			}
		}

		return ValidPrims;
	}();

	if ( NumUsdChildren != NumUnrealChildren )
	{
		FScopedUnrealAllocs UnrealAllocs;

		Children.Reset();
		bNeedsRefresh = true;
	}
	else
	{
		int32 ChildIndex = 0;

		for ( const pxr::UsdPrim& Child : PrimChildren )
		{
			if ( !Children.IsValidIndex( ChildIndex ) || Children[ ChildIndex ]->UsdPrim.GetPrimPath().GetString() != UsdToUnreal::ConvertPath( Child.GetPrimPath() ) )
			{
				FScopedUnrealAllocs UnrealAllocs;

				Children.Reset();
				bNeedsRefresh = true;
				break;
			}

			++ChildIndex;
		}
	}

	if ( bNeedsRefresh )
	{
		FillChildren();
	}
#endif // #if USE_USD_SDK

	return Children;
}

void FUsdPrimViewModel::FillChildren()
{
#if USE_USD_SDK
	pxr::UsdPrimSiblingRange PrimChildren = pxr::UsdPrim( UsdPrim ).GetFilteredChildren( pxr::UsdTraverseInstanceProxies( pxr::UsdPrimAllPrimsPredicate ) );
	for ( pxr::UsdPrim Child : PrimChildren )
	{
		Children.Add( MakeShared< FUsdPrimViewModel >( this, UsdStage, UE::FUsdPrim( Child ) ) );
	}
#endif // #if USE_USD_SDK
}

void FUsdPrimViewModel::RefreshData( bool bRefreshChildren )
{
#if USE_USD_SDK
	if ( !UsdPrim )
	{
		return;
	}

	const bool bIsPseudoRoot = UsdPrim.GetStage().GetPseudoRoot() == UsdPrim;

	RowData->Name = FText::FromName( bIsPseudoRoot ? TEXT("Stage") : UsdPrim.GetName() );
	RowData->bHasCompositionArcs = UsdUtils::HasCompositionArcs( UsdPrim );

	RowData->Type = bIsPseudoRoot ? FText::GetEmpty() : FText::FromName( UsdPrim.GetTypeName() );
	RowData->bHasPayload = UsdPrim.HasPayload();
	RowData->bIsLoaded = UsdPrim.IsLoaded();

	bool bOldVisibility = RowData->bIsVisible;
	if ( pxr::UsdGeomImageable UsdGeomImageable = pxr::UsdGeomImageable( UsdPrim ) )
	{
		RowData->bIsVisible = ( UsdGeomImageable.ComputeVisibility() != pxr::UsdGeomTokens->invisible );
	}

	// If our visibility was enabled, it may be that the visibilities of all of our parents were enabled to accomplish
	// the target change, so we need to refresh them too. This happens when we manually change visibility on
	// a USceneComponent and write that to the USD Stage, for example
	if ( bOldVisibility == false && RowData->bIsVisible )
	{
		FUsdPrimViewModel* Item = ParentItem;
		while ( Item )
		{
			Item->RefreshData(false);
			Item = Item->ParentItem;
		}
	}

	if ( bRefreshChildren )
	{
		for ( FUsdPrimViewModelRef& Child : UpdateChildren() )
		{
			Child->RefreshData( bRefreshChildren );
		}
	}
#endif // #if USE_USD_SDK
}

bool FUsdPrimViewModel::CanExecutePrimAction() const
{
#if USE_USD_SDK
	if ( UsdPrim.IsPseudoRoot() )
	{
		return false;
	}

	UE::FSdfPath SpecPath = UsdUtils::GetPrimSpecPathForLayer( UsdPrim, UsdStage.GetEditTarget() );
	if ( !SpecPath.IsEmpty() )
	{
		return true;
	}

#endif // #if USE_USD_SDK

	return false;
}

bool FUsdPrimViewModel::HasVisibilityAttribute() const
{
#if USE_USD_SDK
	if ( pxr::UsdGeomImageable UsdGeomImageable = pxr::UsdGeomImageable( UsdPrim ) )
	{
		return true;
	}
#endif // #if USE_USD_SDK
	return false;
}

void FUsdPrimViewModel::ToggleVisibility()
{
#if USE_USD_SDK
	FScopedUsdAllocs UsdAllocs;

	if ( pxr::UsdGeomImageable UsdGeomImageable = pxr::UsdGeomImageable( UsdPrim ) )
	{
		// MakeInvisible/MakeVisible internally seem to trigger multiple notices, so group them up to prevent some unnecessary updates
		pxr::SdfChangeBlock SdfChangeBlock;

		if ( RowData->IsVisible() )
		{
			UsdGeomImageable.MakeInvisible();
		}
		else
		{
			UsdGeomImageable.MakeVisible();
		}

		RefreshData( false );
	}
#endif // #if USE_USD_SDK
}

void FUsdPrimViewModel::TogglePayload()
{
	if ( UsdPrim.HasPayload() )
	{
		if ( UsdPrim.IsLoaded() )
		{
			UsdPrim.Unload();
		}
		else
		{
			UsdPrim.Load();
		}

		RefreshData( false );
	}
}

void FUsdPrimViewModel::SetUpLiveLink()
{
#if USE_USD_SDK
	UsdUtils::ApplyLiveLinkSchema( UsdPrim );
#endif // #if USE_USD_SDK
}

bool FUsdPrimViewModel::CanSetUpLiveLink() const
{
#if USE_USD_SDK
	const bool bAddSchema = true;
	return UsdPrim
		&& !UsdPrim.IsPseudoRoot()
		&& UE::USDPrimViewModel::Private::CanAddOrRemoveCustomSchema( UsdPrim, UnrealIdentifiers::LiveLinkAPI, bAddSchema );
#else
	return false;
#endif // #if USE_USD_SDK
}

void FUsdPrimViewModel::RemoveLiveLink()
{
#if USE_USD_SDK
	UE::USDPrimViewModel::Private::RemoveCustomSchema( UsdPrim, UnrealIdentifiers::LiveLinkAPI );
#endif // #if USE_USD_SDK
}

bool FUsdPrimViewModel::CanRemoveLiveLink() const
{
#if USE_USD_SDK
	const bool bAddSchema = false;
	return UsdPrim
		&& !UsdPrim.IsPseudoRoot()
		&& UE::USDPrimViewModel::Private::CanAddOrRemoveCustomSchema( UsdPrim, UnrealIdentifiers::LiveLinkAPI, bAddSchema );
#else
	return false;
#endif // #if USE_USD_SDK
}

void FUsdPrimViewModel::SetUpControlRig()
{
#if USE_USD_SDK
	UsdUtils::ApplyControlRigSchema( UsdPrim );
#endif // #if USE_USD_SDK
}

bool FUsdPrimViewModel::CanSetUpControlRig() const
{
#if USE_USD_SDK
	const bool bAddSchema = true;
	return UsdPrim
		&& !UsdPrim.IsPseudoRoot()
		&& UE::USDPrimViewModel::Private::CanAddOrRemoveCustomSchema( UsdPrim, UnrealIdentifiers::ControlRigAPI, bAddSchema );
#else
	return false;
#endif // #if USE_USD_SDK
}

void FUsdPrimViewModel::RemoveControlRig()
{
#if USE_USD_SDK
	UE::USDPrimViewModel::Private::RemoveCustomSchema( UsdPrim, UnrealIdentifiers::ControlRigAPI );
#endif // #if USE_USD_SDK
}

bool FUsdPrimViewModel::CanRemoveControlRig() const
{
#if USE_USD_SDK
	const bool bAddSchema = false;
	return UsdPrim
		&& !UsdPrim.IsPseudoRoot()
		&& UE::USDPrimViewModel::Private::CanAddOrRemoveCustomSchema( UsdPrim, UnrealIdentifiers::ControlRigAPI, bAddSchema );
#else
	return false;
#endif // #if USE_USD_SDK
}

void FUsdPrimViewModel::ApplyGroomSchema()
{
#if USE_USD_SDK
	UsdUtils::ApplyGroomSchema( UsdPrim );
#endif // #if USE_USD_SDK
}

bool FUsdPrimViewModel::CanApplyGroomSchema() const
{
#if USE_USD_SDK
	const bool bAddSchema = true;
	return UsdPrim
		&& !UsdPrim.IsPseudoRoot()
		&& UE::USDPrimViewModel::Private::CanAddOrRemoveCustomSchema( UsdPrim, UnrealIdentifiers::GroomAPI, bAddSchema );
#else
	return false;
#endif // #if USE_USD_SDK
}

void FUsdPrimViewModel::RemoveGroomSchema()
{
#if USE_USD_SDK
	UE::USDPrimViewModel::Private::RemoveCustomSchema( UsdPrim, UnrealIdentifiers::GroomAPI );
#endif // #if USE_USD_SDK
}

bool FUsdPrimViewModel::CanRemoveGroomSchema() const
{
#if USE_USD_SDK
	const bool bAddSchema = false;
	return UsdPrim
		&& !UsdPrim.IsPseudoRoot()
		&& UE::USDPrimViewModel::Private::CanAddOrRemoveCustomSchema( UsdPrim, UnrealIdentifiers::GroomAPI, bAddSchema );
#else
	return false;
#endif // #if USE_USD_SDK
}

void FUsdPrimViewModel::DefinePrim( const TCHAR* PrimName )
{
#if USE_USD_SDK
	FScopedUsdAllocs UsdAllocs;

	UE::FSdfPath ParentPrimPath;

	if ( ParentItem )
	{
		ParentPrimPath = ParentItem->UsdPrim.GetPrimPath();
	}
	else
	{
		ParentPrimPath = UE::FSdfPath::AbsoluteRootPath();
	}

	UE::FSdfPath NewPrimPath = ParentPrimPath.AppendChild( PrimName );

	UsdPrim = pxr::UsdGeomXform::Define( UsdStage, NewPrimPath ).GetPrim();
#endif // #if USE_USD_SDK
}

void FUsdPrimViewModel::AddReference( const TCHAR* AbsoluteFilePath )
{
	UsdUtils::AddReference( UsdPrim, AbsoluteFilePath );
}

void FUsdPrimViewModel::ClearReferences()
{
#if USE_USD_SDK
	FScopedUsdAllocs UsdAllocs;

	pxr::UsdReferences References = pxr::UsdPrim( UsdPrim ).GetReferences();
	References.ClearReferences();
#endif // #if USE_USD_SDK
}
