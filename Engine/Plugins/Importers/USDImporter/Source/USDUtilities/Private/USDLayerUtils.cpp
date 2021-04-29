// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDLayerUtils.h"

#include "UnrealUSDWrapper.h"
#include "USDErrorUtils.h"
#include "USDLog.h"
#include "USDTypesConversion.h"

#include "UsdWrappers/SdfChangeBlock.h"
#include "UsdWrappers/SdfLayer.h"
#include "UsdWrappers/UsdStage.h"

#include "Framework/Application/SlateApplication.h"
#include "Misc/Paths.h"
#include "Widgets/SWidget.h"

#if WITH_EDITOR
	#include "DesktopPlatformModule.h"
#endif // WITH_EDITOR

#if USE_USD_SDK

#include "USDIncludesStart.h"
	#include "pxr/usd/pcp/layerStack.h"
	#include "pxr/usd/sdf/fileFormat.h"
	#include "pxr/usd/sdf/layer.h"
	#include "pxr/usd/sdf/layerUtils.h"
	#include "pxr/usd/sdf/textFileFormat.h"
	#include "pxr/usd/usd/editContext.h"
	#include "pxr/usd/usd/modelAPI.h"
	#include "pxr/usd/usd/primCompositionQuery.h"
	#include "pxr/usd/usd/stage.h"
	#include "pxr/usd/usdGeom/xform.h"
#include "USDIncludesEnd.h"

#define LOCTEXT_NAMESPACE "USDLayerUtils"

namespace UsdUtils
{
	std::string GetUESessionStateLayerDisplayName( const pxr::UsdStageRefPtr& Stage )
	{
		return pxr::SdfLayer::GetDisplayNameFromIdentifier( Stage->GetRootLayer()->GetIdentifier() ) + "-UE-session-state.usda";
	}
}

bool UsdUtils::InsertSubLayer( const pxr::SdfLayerRefPtr& ParentLayer, const TCHAR* SubLayerFile, int32 Index )
{
	if ( !ParentLayer )
	{
		return false;
	}

	FString RelativeSubLayerPath = SubLayerFile;
	MakePathRelativeToLayer( UE::FSdfLayer{ ParentLayer }, RelativeSubLayerPath );

	// If the relative path is just the same as the clean name (e.g. Layer.usda wants to add Layer.usda as a sublayer) then
	// just stop here as that is always an error
	FString ParentLayerPath = UsdToUnreal::ConvertString( ParentLayer->GetRealPath() );
	if ( FPaths::GetCleanFilename( ParentLayerPath ) == RelativeSubLayerPath )
	{
		UE_LOG( LogUsd, Warning, TEXT( "Tried to add layer '%s' as a sublayer of itself!" ), *ParentLayerPath );
		return false;
	}

	FScopedUsdAllocs UsdAllocs;

	ParentLayer->InsertSubLayerPath( UnrealToUsd::ConvertString( *RelativeSubLayerPath ).Get(), Index );

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
			if ( !DesktopPlatform->OpenFileDialog( ParentWindowHandle, LOCTEXT( "ChooseFile", "Choose file").ToString(), TEXT(""), TEXT(""), *FileTypes, EFileDialogFlags::None, OutFiles ) )
			{
				return {};
			}
			break;

		case EBrowseFileMode::Save :
			if ( !DesktopPlatform->SaveFileDialog( ParentWindowHandle, LOCTEXT( "ChooseFile", "Choose file").ToString(), TEXT(""), TEXT(""), *FileTypes, EFileDialogFlags::None, OutFiles ) )
			{
				return {};
			}
			break;

		default:
			break;
	}

	if ( OutFiles.Num() > 0 )
	{
		return MakePathRelativeToProjectDir( OutFiles[0] );
	}

	return {};
}

#endif // WITH_EDITOR

FString UsdUtils::MakePathRelativeToProjectDir( const FString& Path )
{
	FString PathConverted = FPaths::ConvertRelativePathToFull( Path );

	// Mirror behavior of RelativeToGameDir meta tag on the stage actor's RootLayer
	if ( FPaths::IsUnderDirectory( PathConverted, FPaths::ProjectDir() ) )
	{
		FPaths::MakePathRelativeTo( PathConverted, *FPaths::ProjectDir() );
	}

	return PathConverted;
}

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
	UsdUtils::InsertSubLayer( ParentLayer.Get(), LayerFilePath );

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

void UsdUtils::AddTimeCodeRangeToLayer( const pxr::SdfLayerRefPtr& Layer, double StartTimeCode, double EndTimeCode )
{
	FScopedUsdAllocs UsdAllocs;

	if ( !Layer )
	{
		FUsdLogManager::LogMessage( EMessageSeverity::Warning, LOCTEXT( "AddTimeCode_InvalidLayer", "Trying to set timecodes on an invalid layer." ) );
		return;
	}

	// The HasTimeCode check is needed or else we can't author anything with a StartTimeCode lower than the default of 0
	if ( StartTimeCode < Layer->GetStartTimeCode() || !Layer->HasStartTimeCode() )
	{
		Layer->SetStartTimeCode( StartTimeCode );
	}

	if ( EndTimeCode > Layer->GetEndTimeCode() || !Layer->HasEndTimeCode() )
	{
		Layer->SetEndTimeCode( StartTimeCode );
	}
}

void UsdUtils::MakePathRelativeToLayer( const UE::FSdfLayer& Layer, FString& Path )
{
#if USE_USD_SDK
	FScopedUsdAllocs UsdAllocs;

	if ( pxr::SdfLayerRefPtr UsdLayer = (pxr::SdfLayerRefPtr)Layer )
	{
		std::string RepositoryPath = UsdLayer->GetRepositoryPath().empty() ? UsdLayer->GetRealPath() : UsdLayer->GetRepositoryPath();
		FString LayerAbsolutePath = UsdToUnreal::ConvertString( RepositoryPath );
		FPaths::MakePathRelativeTo( Path, *LayerAbsolutePath );
	}
#endif // #if USE_USD_SDK
}

UE::FSdfLayer UsdUtils::GetUEPersistentStateSublayer( const UE::FUsdStage& Stage, bool bCreateIfNeeded )
{
	UE::FSdfLayer StateLayer;
	if ( !Stage )
	{
		return StateLayer;
	}

	FScopedUsdAllocs Allocs;

	UE::FSdfChangeBlock ChangeBlock;

	FString PathPart;
	FString FilenamePart;
	FString ExtensionPart;
	FPaths::Split( Stage.GetRootLayer().GetRealPath(), PathPart, FilenamePart, ExtensionPart );

	FString ExpectedStateLayerPath = FPaths::Combine( PathPart, FString::Printf( TEXT( "%s-UE-persistent-state.%s" ), *FilenamePart, *ExtensionPart ) );
	FPaths::NormalizeFilename( ExpectedStateLayerPath );

	StateLayer = UE::FSdfLayer::FindOrOpen( *ExpectedStateLayerPath );

	if ( !StateLayer && bCreateIfNeeded )
	{
		StateLayer = pxr::SdfLayer::New(
			pxr::SdfFileFormat::FindById( pxr::SdfTextFileFormatTokens->Id ),
			UnrealToUsd::ConvertString( *ExpectedStateLayerPath ).Get()
		);
	}

	// Add the layer as a sublayer of the session layer, in the right location
	// Always check this because we need to do this even if we just loaded an existing state layer from disk
	if ( StateLayer )
	{
		UE::FSdfLayer SessionLayer = Stage.GetSessionLayer();

		// For consistency we always add the UEPersistentState sublayer as the weakest sublayer of the stage's session layer
		// Note that we intentionally only guarantee the UEPersistentLayer is weaker than the UESessionLayer when inserting,
		// so that the user may reorder these if he wants, for whatever reason
		bool bNeedsToBeAdded = true;
		for ( const FString& Path : SessionLayer.GetSubLayerPaths() )
		{
			if ( FPaths::IsSamePath( Path, ExpectedStateLayerPath ) )
			{
				bNeedsToBeAdded = false;
				break;
			}
		}

		if ( bNeedsToBeAdded )
		{
			// Always add it at the back, so it's weaker than the session layer
			InsertSubLayer( static_cast< pxr::SdfLayerRefPtr& >( SessionLayer ), *ExpectedStateLayerPath );
		}
	}

	return StateLayer;
}

UE::FSdfLayer UsdUtils::GetUESessionStateSublayer( const UE::FUsdStage& Stage, bool bCreateIfNeeded )
{
	UE::FSdfLayer StateLayer;
	if ( !Stage )
	{
		return StateLayer;
	}

	FScopedUsdAllocs Allocs;

	const pxr::UsdStageRefPtr UsdStage{ Stage };
	pxr::SdfLayerRefPtr UsdSessionLayer = UsdStage->GetSessionLayer();

	FString PathPart;
	FString FilenamePart;
	FString ExtensionPart;
	FPaths::Split( Stage.GetRootLayer().GetRealPath(), PathPart, FilenamePart, ExtensionPart );

	FString ExpectedStateLayerDisplayName = FString::Printf( TEXT( "%s-UE-session-state.%s" ), *FilenamePart, *ExtensionPart );
	FPaths::NormalizeFilename( ExpectedStateLayerDisplayName );

	std::string UsdExpectedStateLayerDisplayName = UnrealToUsd::ConvertString( *ExpectedStateLayerDisplayName ).Get();

	// Check if we already have an existing utils layer in this stage
	std::string ExistingUESessionStateIdentifier;
	{
		std::unordered_set<std::string> SessionLayerSubLayerIdentifiers;
		for ( const std::string& SubLayerIdentifier : UsdSessionLayer->GetSubLayerPaths() )
		{
			SessionLayerSubLayerIdentifiers.insert( SubLayerIdentifier );
		}
		if ( SessionLayerSubLayerIdentifiers.size() > 0 )
		{
			const bool bIncludeSessionLayers = true;
			for ( const pxr::SdfLayerHandle& Layer : UsdStage->GetLayerStack( bIncludeSessionLayers ) )
			{
				// All session layers always come before the root layer
				if ( Layer == UsdStage->GetRootLayer() )
				{
					break;
				}

				const std::string& Identifier = Layer->GetIdentifier();
				if ( Layer->IsAnonymous() && Layer->GetDisplayName() == UsdExpectedStateLayerDisplayName && SessionLayerSubLayerIdentifiers.count( Identifier ) > 0 )
				{
					ExistingUESessionStateIdentifier = Identifier;
					break;
				}
			}
		}
	}

	if ( ExistingUESessionStateIdentifier.size() > 0 )
	{
		StateLayer = UE::FSdfLayer::FindOrOpen( *UsdToUnreal::ConvertString( ExistingUESessionStateIdentifier ) );
	}

	// We only need to add as sublayer when creating the StateLayer layers, because they are always transient and never saved/loaded from disk
	// so if it exists already, it was created right here, where we add it as a sublayer
	if ( !StateLayer && bCreateIfNeeded )
	{
		pxr::SdfLayerRefPtr UsdStateLayer = pxr::SdfLayer::CreateAnonymous( UsdExpectedStateLayerDisplayName );
		UsdSessionLayer->InsertSubLayerPath( UsdStateLayer->GetIdentifier(), 0 ); // Always add it at the front, so it's stronger than the persistent layer

		StateLayer = UsdStateLayer;
	}

	return StateLayer;
}

UE::FSdfLayer UsdUtils::FindLayerForIdentifier( const TCHAR* Identifier, const UE::FUsdStage& Stage )
{
	FScopedUsdAllocs UsdAllocs;

	std::string IdentifierStr = UnrealToUsd::ConvertString( Identifier ).Get();
	if ( pxr::SdfLayer::IsAnonymousLayerIdentifier( IdentifierStr ) )
	{
		std::string DisplayName = pxr::SdfLayer::GetDisplayNameFromIdentifier( IdentifierStr );

		if ( pxr::UsdStageRefPtr UsdStage = static_cast< pxr::UsdStageRefPtr >( Stage ) )
		{
			const bool bIncludeSessionLayers = true;
			for ( const pxr::SdfLayerHandle& Layer : UsdStage->GetLayerStack( bIncludeSessionLayers ) )
			{
				if ( Layer->GetDisplayName() == DisplayName )
				{
					return UE::FSdfLayer{ Layer };
				}
			}
		}
	}
	else
	{
		if ( pxr::SdfLayerRefPtr Layer = pxr::SdfLayer::FindOrOpen( IdentifierStr ) )
		{
			return UE::FSdfLayer{ Layer };
		}
	}

	return UE::FSdfLayer{};
}

#undef LOCTEXT_NAMESPACE

#endif // #if USE_USD_SDK
