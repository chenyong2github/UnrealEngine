// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDLayerUtils.h"

#include "UnrealUSDWrapper.h"
#include "USDErrorUtils.h"
#include "USDLog.h"
#include "USDTypesConversion.h"

#include "UsdWrappers/SdfLayer.h"

#include "Framework/Application/SlateApplication.h"
#include "Misc/Paths.h"
#include "Widgets/SWidget.h"

#if WITH_EDITOR
	#include "DesktopPlatformModule.h"
#endif // WITH_EDITOR

#if USE_USD_SDK

#include "USDIncludesStart.h"
	#include "pxr/usd/pcp/layerStack.h"
	#include "pxr/usd/sdf/layer.h"
	#include "pxr/usd/sdf/layerUtils.h"
	#include "pxr/usd/usd/editContext.h"
	#include "pxr/usd/usd/modelAPI.h"
	#include "pxr/usd/usd/primCompositionQuery.h"
	#include "pxr/usd/usd/stage.h"
	#include "pxr/usd/usdGeom/xform.h"
#include "USDIncludesEnd.h"

#define LOCTEXT_NAMESPACE "USDLayerUtils"

bool UsdUtils::InsertSubLayer( const TUsdStore< pxr::SdfLayerRefPtr >& ParentLayer, const TCHAR* SubLayerFile )
{
	if ( !ParentLayer.Get() )
	{
		return false;
	}

	FScopedUsdAllocs UsdAllocs;

	std::string UsdLayerFilePath = ParentLayer.Get()->GetRealPath();
	FString LayerFilePath = UsdToUnreal::ConvertString( UsdLayerFilePath );

	FString SubLayerFilePath = FPaths::ConvertRelativePathToFull( SubLayerFile );
	FPaths::MakePathRelativeTo( SubLayerFilePath, *LayerFilePath );

	ParentLayer.Get()->InsertSubLayerPath( UnrealToUsd::ConvertString( *SubLayerFilePath ).Get() );

	return true;
}

#if WITH_EDITOR
TOptional< FString > UsdUtils::BrowseUsdFile( EBrowseFileMode Mode, TSharedRef< const SWidget > OriginatingWidget )
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();

	if ( DesktopPlatform == nullptr )
	{
		return {};
	}

	// show the file browse dialog
	TSharedPtr< SWindow > ParentWindow = FSlateApplication::Get().FindWidgetWindow( OriginatingWidget );
	void* ParentWindowHandle = ( ParentWindow.IsValid() && ParentWindow->GetNativeWindow().IsValid() )
		? ParentWindow->GetNativeWindow()->GetOSWindowHandle()
		: nullptr;

	TArray< FString > OutFiles;

	TArray< FString > SupportedExtensions = UnrealUSDWrapper::GetAllSupportedFileFormats();
	if ( SupportedExtensions.Num() == 0 )
	{
		UE_LOG(LogUsd, Error, TEXT("No file extensions supported by the USD SDK!"));
		return {};
	}
	FString JoinedExtensions = FString::Join(SupportedExtensions, TEXT(";*.")); // Combine "usd" and "usda" into "usd; *.usda"
	FString FileTypes = FString::Printf(TEXT("usd files (*.%s)|*.%s"), *JoinedExtensions, *JoinedExtensions);

	switch ( Mode )
	{
		case EBrowseFileMode::Open :

			if ( DesktopPlatform->OpenFileDialog( ParentWindowHandle, LOCTEXT( "ChooseFile", "Choose file").ToString(), TEXT(""), TEXT(""), *FileTypes, EFileDialogFlags::None, OutFiles ) )
			{
				return FPaths::ConvertRelativePathToFull(OutFiles[0]);
			}
			break;

		case EBrowseFileMode::Save :
			if ( DesktopPlatform->SaveFileDialog( ParentWindowHandle, LOCTEXT( "ChooseFile", "Choose file").ToString(), TEXT(""), TEXT(""), *FileTypes, EFileDialogFlags::None, OutFiles ) )
			{
				return FPaths::ConvertRelativePathToFull(OutFiles[0]);
			}
			break;

		default:
			break;
	}

	return {};
}
#endif // WITH_EDITOR

TUsdStore< pxr::SdfLayerRefPtr > UsdUtils::CreateNewLayer( TUsdStore< pxr::UsdStageRefPtr > UsdStage, const TUsdStore<pxr::SdfLayerRefPtr>& ParentLayer, const TCHAR* LayerFilePath )
{
	FScopedUsdAllocs UsdAllocs;

	std::string UsdLayerFilePath = UnrealToUsd::ConvertString( *FPaths::ConvertRelativePathToFull( LayerFilePath ) ).Get();

	pxr::SdfLayerRefPtr LayerRef = pxr::SdfLayer::CreateNew( UsdLayerFilePath );

	if ( !LayerRef )
	{
		return {};
	}

	// New layer needs to be created and in the stage layer stack before we can edit it
	UsdUtils::InsertSubLayer( ParentLayer, LayerFilePath );

	UsdUtils::StartMonitoringErrors();
	pxr::UsdEditContext UsdEditContext( UsdStage.Get(), LayerRef );

	// Create default prim
	FString PrimPath = TEXT("/") + FPaths::GetBaseFilename( UsdToUnreal::ConvertString( LayerRef->GetDisplayName() ) );

	pxr::SdfPath UsdPrimPath = UnrealToUsd::ConvertPath( *PrimPath ).Get();
	pxr::UsdGeomXform DefaultPrim = pxr::UsdGeomXform::Define( UsdStage.Get(), UsdPrimPath );

	if ( DefaultPrim)
	{
		// Set default prim
		LayerRef->SetDefaultPrim( DefaultPrim.GetPrim().GetName() );
	}

	bool bHadErrors = UsdUtils::ShowErrorsAndStopMonitoring();

	if (bHadErrors)
	{
		return {};
	}

	return TUsdStore< pxr::SdfLayerRefPtr >( LayerRef );
}

UE::FSdfLayer UsdUtils::FindLayerForPrim( const pxr::UsdPrim& Prim )
{
	if ( !Prim )
	{
		return {};
	}

	FScopedUsdAllocs UsdAllocs;

	pxr::UsdPrimCompositionQuery PrimCompositionQuery( Prim );
	std::vector< pxr::UsdPrimCompositionQueryArc > CompositionArcs = PrimCompositionQuery.GetCompositionArcs();

	for ( const pxr::UsdPrimCompositionQueryArc& CompositionArc : CompositionArcs )
	{
		pxr::SdfLayerHandle IntroducingLayer = CompositionArc.GetIntroducingLayer();

		if ( IntroducingLayer )
		{
			return UE::FSdfLayer( IntroducingLayer );
		}
	}

	return UE::FSdfLayer( Prim.GetStage()->GetRootLayer() );
}

UE::FSdfLayer UsdUtils::FindLayerForAttribute( const pxr::UsdAttribute& Attribute, double TimeCode )
{
	if ( !Attribute )
	{
		return {};
	}

	FScopedUsdAllocs UsdAllocs;

	for ( const pxr::SdfPropertySpecHandle& PropertySpec : Attribute.GetPropertyStack( TimeCode ) )
	{
		if ( PropertySpec->HasDefaultValue() || PropertySpec->GetLayer()->GetNumTimeSamplesForPath( PropertySpec->GetPath() ) > 0 )
		{
			return UE::FSdfLayer( PropertySpec->GetLayer() );
		}
	}

	return {};
}

UE::FSdfLayer UsdUtils::FindLayerForSubLayerPath( const UE::FSdfLayer& RootLayer, const FStringView& SubLayerPath )
{
	const FString RelativeLayerPath = UE::FSdfLayerUtils::SdfComputeAssetPathRelativeToLayer( RootLayer, SubLayerPath.GetData() );

	return UE::FSdfLayer::FindOrOpen( *RelativeLayerPath );
}

bool UsdUtils::SetRefOrPayloadLayerOffset( pxr::UsdPrim& Prim, const UE::FSdfLayerOffset& LayerOffset )
{
	FScopedUsdAllocs UsdAllocs;

	pxr::UsdPrimCompositionQuery PrimCompositionQuery( Prim );
	std::vector< pxr::UsdPrimCompositionQueryArc > CompositionArcs = PrimCompositionQuery.GetCompositionArcs();

	for ( const pxr::UsdPrimCompositionQueryArc& CompositionArc : CompositionArcs )
	{
		if ( CompositionArc.GetArcType() == pxr::PcpArcTypeReference )
		{
			pxr::SdfReferenceEditorProxy ReferenceEditor;
			pxr::SdfReference OldReference;

			if ( CompositionArc.GetIntroducingListEditor( &ReferenceEditor, &OldReference ) )
			{
				pxr::SdfReference NewReference = OldReference;
				NewReference.SetLayerOffset( pxr::SdfLayerOffset( LayerOffset.Offset, LayerOffset.Scale ) );

				ReferenceEditor.ReplaceItemEdits( OldReference, NewReference );

				return true;
			}
		}
		else if ( CompositionArc.GetArcType() == pxr::PcpArcTypePayload )
		{
			pxr::SdfPayloadEditorProxy PayloadEditor;
			pxr::SdfPayload OldPayload;

			if ( CompositionArc.GetIntroducingListEditor( &PayloadEditor, &OldPayload ) )
			{
				pxr::SdfPayload NewPayload = OldPayload;
				NewPayload.SetLayerOffset( pxr::SdfLayerOffset( LayerOffset.Offset, LayerOffset.Scale ) );

				PayloadEditor.ReplaceItemEdits( OldPayload, NewPayload );

				return true;
			}
		}
	}

	return false;
}

UE::FSdfLayerOffset UsdUtils::GetLayerToStageOffset( const pxr::UsdAttribute& Attribute )
{
	// Inspired by pxr::_GetLayerToStageOffset

	UE::FSdfLayer AttributeLayer = UsdUtils::FindLayerForAttribute( Attribute, pxr::UsdTimeCode::EarliestTime().GetValue() );

	FScopedUsdAllocs UsdAllocs;

	pxr::UsdResolveInfo ResolveInfo = Attribute.GetResolveInfo( pxr::UsdTimeCode::EarliestTime() );
	pxr::PcpNodeRef Node = ResolveInfo.GetNode();
	if ( !Node )
	{
		return UE::FSdfLayerOffset();
	}

	const pxr::PcpMapExpression& MapToRoot = Node.GetMapToRoot();
	if ( MapToRoot.IsNull() )
	{
		return UE::FSdfLayerOffset();
	}

	pxr::SdfLayerOffset NodeToRootNodeOffset = MapToRoot.GetTimeOffset();

	pxr::SdfLayerOffset LocalOffset = NodeToRootNodeOffset;

	if ( const pxr::SdfLayerOffset* LayerToRootLayerOffset = ResolveInfo.GetNode().GetLayerStack()->GetLayerOffsetForLayer( pxr::SdfLayerRefPtr( AttributeLayer ) ) )
	{
		LocalOffset = LocalOffset * (*LayerToRootLayerOffset);
	}

	return UE::FSdfLayerOffset( LocalOffset.GetOffset(), LocalOffset.GetScale() );
}

#undef LOCTEXT_NAMESPACE

#endif // #if USE_USD_SDK
