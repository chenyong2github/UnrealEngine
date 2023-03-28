// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDPrimAttributesViewModel.h"

#include "UnrealUSDWrapper.h"
#include "USDAttributeUtils.h"
#include "USDErrorUtils.h"
#include "USDLog.h"
#include "USDMemory.h"
#include "USDTypesConversion.h"
#include "USDValueConversion.h"

#include "UsdWrappers/SdfLayer.h"
#include "UsdWrappers/SdfPath.h"
#include "UsdWrappers/UsdAttribute.h"
#include "UsdWrappers/UsdPrim.h"
#include "UsdWrappers/VtValue.h"

#include "Framework/Notifications/NotificationManager.h"
#include "ScopedTransaction.h"
#include "Widgets/Notifications/SNotificationList.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"
	#include "pxr/base/tf/stringUtils.h"
	#include "pxr/base/vt/value.h"
	#include "pxr/usd/kind/registry.h"
	#include "pxr/usd/sdf/types.h"
	#include "pxr/usd/usd/attribute.h"
	#include "pxr/usd/usd/common.h"
	#include "pxr/usd/usd/editContext.h"
	#include "pxr/usd/usd/prim.h"
	#include "pxr/usd/usd/relationship.h"
	#include "pxr/usd/usdGeom/tokens.h"
#include "USDIncludesEnd.h"

#endif // #if USE_USD_SDK

#define LOCTEXT_NAMESPACE "USDPrimAttributesViewModel"

FUsdPrimAttributeViewModel::FUsdPrimAttributeViewModel( FUsdPrimAttributesViewModel* InOwner )
	: Owner( InOwner )
{
}

TArray< TSharedPtr< FString > > FUsdPrimAttributeViewModel::GetDropdownOptions() const
{
#if USE_USD_SDK
	if ( Label == TEXT("kind") )
	{
		TArray< TSharedPtr< FString > > Options;
		{
			FScopedUsdAllocs Allocs;

			std::vector< pxr::TfToken > Kinds = pxr::KindRegistry::GetAllKinds();
			Options.Reserve( Kinds.size() );

			for ( const pxr::TfToken& Kind : Kinds )
			{
				Options.Add( MakeShared< FString >( UsdToUnreal::ConvertToken( Kind ) ) );
			}

			// They are supposed to be in an unspecified order, so let's make them consistent
			Options.Sort( [](const TSharedPtr< FString >& A, const TSharedPtr< FString >& B )
				{
					return A.IsValid() && B.IsValid() && ( *A < *B );
				}
			);

		}
		return Options;
	}
	else if ( Label == UsdToUnreal::ConvertToken( pxr::UsdGeomTokens->purpose ) )
	{
		return TArray< TSharedPtr< FString > >
		{
			MakeShared< FString >( UsdToUnreal::ConvertToken( pxr::UsdGeomTokens->default_ ) ),
			MakeShared< FString >( UsdToUnreal::ConvertToken( pxr::UsdGeomTokens->proxy ) ),
			MakeShared< FString >( UsdToUnreal::ConvertToken( pxr::UsdGeomTokens->render) ),
			MakeShared< FString >( UsdToUnreal::ConvertToken( pxr::UsdGeomTokens->guide ) ),
		};
	}
	else if ( Label == UsdToUnreal::ConvertToken( pxr::UsdGeomTokens->upAxis ) )
	{
		return TArray< TSharedPtr< FString > >
		{
			MakeShared< FString >( UsdToUnreal::ConvertToken( pxr::UsdGeomTokens->y ) ),
			MakeShared< FString >( UsdToUnreal::ConvertToken( pxr::UsdGeomTokens->z ) ),
		};
	}
	else if ( Label == UsdToUnreal::ConvertToken( pxr::UsdGeomTokens->visibility ) )
	{
		return TArray< TSharedPtr< FString > >
		{
			MakeShared< FString >( UsdToUnreal::ConvertToken( pxr::UsdGeomTokens->inherited ) ),
			MakeShared< FString >( UsdToUnreal::ConvertToken( pxr::UsdGeomTokens->invisible ) ),
		};
	}
#endif // #if USE_USD_SDK

	return {};
}

void FUsdPrimAttributeViewModel::SetAttributeValue( const UsdUtils::FConvertedVtValue& InValue )
{
	Owner->SetPrimAttribute( Label, InValue );
}

template<typename T>
void FUsdPrimAttributesViewModel::CreatePrimAttribute(
	EAttributeModelType Type,
	const FString& AttributeName,
	const T& Value,
	UsdUtils::EUsdBasicDataTypes SourceType,
	const FString& ValueRole,
	bool bReadOnly
)
{
	UsdUtils::FConvertedVtValue VtValue;
	VtValue.Entries = { { UsdUtils::FConvertedVtValueComponent{ TInPlaceType<T>(), Value } } };
	VtValue.SourceType = SourceType;

	FUsdPrimAttributeViewModel Property( this );
	Property.Type = Type;
	Property.Label = AttributeName;
	Property.Value = VtValue;
	Property.ValueRole = ValueRole;
	Property.bReadOnly = bReadOnly;

	PrimAttributes.Add( MakeSharedUnreal< FUsdPrimAttributeViewModel >( MoveTemp( Property ) ) );
}

void FUsdPrimAttributesViewModel::CreatePrimAttribute(
	EAttributeModelType Type,
	const FString& AttributeName,
	const UsdUtils::FConvertedVtValue& Value,
	bool bReadOnly
)
{
	FUsdPrimAttributeViewModel Property( this );
	Property.Type = Type;
	Property.Label = AttributeName;
	Property.Value = Value;
	Property.bReadOnly = bReadOnly;

	PrimAttributes.Add( MakeSharedUnreal< FUsdPrimAttributeViewModel >( MoveTemp( Property ) ) );
}

void FUsdPrimAttributesViewModel::SetPrimAttribute( const FString& AttributeName, const UsdUtils::FConvertedVtValue& InValue )
{
	bool bSuccess = false;

#if USE_USD_SDK
	if ( !UsdStage )
	{
		return;
	}

	// Transact here as setting this attribute may trigger USD events that affect assets/components
	FScopedTransaction Transaction( FText::Format(
		LOCTEXT( "SetPrimAttribute", "Set value for field '{0}' of prim '{1}'" ),
		FText::FromString( AttributeName ),
		FText::FromString( PrimPath )
	));

	const bool bIsStageAttribute = PrimPath == TEXT( "/" ) || PrimPath.IsEmpty();
	bool bIsPropertyPath = false;

	UE::FVtValue VtValue;
	if ( UnrealToUsd::ConvertValue( InValue, VtValue ) )
	{
		if ( bIsStageAttribute )
		{
			FScopedUsdAllocs UsdAllocs;

			// To set stage metadata the edit target must be the root or session layer
			pxr::UsdStageRefPtr Stage{ UsdStage };
			pxr::UsdEditContext( Stage, Stage->GetRootLayer() );
			bSuccess = UsdStage.SetMetadata( *AttributeName, VtValue );
		}
		else if ( UE::FUsdPrim UsdPrim = UsdStage.GetPrimAtPath( UE::FSdfPath( *PrimPath ) ) )
		{
			FScopedUsdAllocs UsdAllocs;

			pxr::UsdPrim PxrUsdPrim{UsdPrim};
			pxr::TfToken AttributeNameToken = UnrealToUsd::ConvertToken(*AttributeName).Get();

			// Single value, single component of FString
			if ( AttributeName == TEXT( "kind" ) && InValue.Entries.Num() == 1 && InValue.Entries[ 0 ].Num() == 1 && InValue.Entries[ 0 ][ 0 ].IsType<FString>() )
			{
				bSuccess = IUsdPrim::SetKind(
					UsdPrim,
					UnrealToUsd::ConvertToken( *( InValue.Entries[ 0 ][ 0 ].Get<FString>() ) ).Get()
				);
			}
			else if ( UE::FUsdAttribute Attribute = UsdPrim.GetAttribute( *AttributeName ) )
			{
				bSuccess = Attribute.Set( VtValue );
				UsdUtils::NotifyIfOverriddenOpinion( Attribute );
			}
			else if (pxr::UsdRelationship Relationship = PxrUsdPrim.GetRelationship(AttributeNameToken))
			{
				if (InValue.Entries.Num() == 1 && InValue.Entries[0].Num() == 1 && InValue.Entries[0][0].IsType<FString>())
				{
					const FString& NewValue = InValue.Entries[0][0].Get<FString>();

					// We only allow editing relationship attributes that have a single target anyway
					bSuccess = Relationship.SetTargets(std::vector<pxr::SdfPath>{UnrealToUsd::ConvertPath(*NewValue).Get()});
					UsdUtils::NotifyIfOverriddenOpinion(Relationship);
				}
			}
			else if (PxrUsdPrim.HasMetadata(AttributeNameToken))
			{
				bSuccess = PxrUsdPrim.SetMetadata(AttributeNameToken, VtValue.GetUsdValue());
			}
		}
		// Trying to set property metadata
		else if (!PrimPath.IsEmpty())
		{
			FScopedUsdAllocs Allocs;

			pxr::SdfPath UsdPath = UnrealToUsd::ConvertPath(*PrimPath).Get();
			if (UsdPath.IsPropertyPath())
			{
				bIsPropertyPath = true;

				pxr::SdfPath PxrPrimPath = UsdPath.GetPrimPath();
				pxr::TfToken PropertyName = UsdPath.GetNameToken();

				pxr::UsdStageRefPtr PxrUsdStage{UsdStage};
				if (pxr::UsdPrim Prim = PxrUsdStage->GetPrimAtPath(PxrPrimPath))
				{
					if (pxr::UsdProperty Property = Prim.GetProperty(PropertyName))
					{
						pxr::TfToken AttributeNameToken = UnrealToUsd::ConvertToken(*AttributeName).Get();
						if (Property.HasMetadata(AttributeNameToken))
						{
							bSuccess = Property.SetMetadata(AttributeNameToken, VtValue.GetUsdValue());
						}
					}
				}
			}
		}
	}

	if ( !bSuccess )
	{
		const FText ErrorMessage = FText::Format( LOCTEXT( "FailToSetAttributeMessage", "Failed to set '{0}' on {1} '{2}'" ),
			FText::FromString( AttributeName ),
			FText::FromString( bIsStageAttribute ? TEXT( "stage" ) : bIsPropertyPath ? TEXT("property") : TEXT("prim")),
			FText::FromString( bIsStageAttribute ? UsdStage.GetRootLayer().GetRealPath() : PrimPath )
		);

		FNotificationInfo ErrorToast( ErrorMessage );
		ErrorToast.ExpireDuration = 5.0f;
		ErrorToast.bFireAndForget = true;
		ErrorToast.Image = FCoreStyle::Get().GetBrush( TEXT( "MessageLog.Warning" ) );
		FSlateNotificationManager::Get().AddNotification( ErrorToast );

		FUsdLogManager::LogMessage( EMessageSeverity::Warning, ErrorMessage );
	}
#endif // #if USE_USD_SDK
}

void FUsdPrimAttributesViewModel::Refresh( const UE::FUsdStageWeak& InUsdStage, const TCHAR* InPrimPath, float TimeCode )
{
	FScopedUnrealAllocs UnrealAllocs;

	UsdStage = InUsdStage;
	PrimPath = InPrimPath;

	PrimAttributes.Reset();

#if USE_USD_SDK
	TFunction<void(const FString& MetadataName, const pxr::VtValue& Value)> DisplayEntryAsString =
		[this](const FString& MetadataName, const pxr::VtValue& Value)
		{
			const bool bAttrReadOnly = true;
			FString Stringified = UsdToUnreal::ConvertString(pxr::TfStringify(Value));

			// STextBlock can get very slow calculating its desired size for very long string so chop it if needed
			const int32 MaxValueLength = 300;
			if (Stringified.Len() > MaxValueLength)
			{
				Stringified.LeftInline(MaxValueLength);
				Stringified.Append(TEXT("..."));
			}

			CreatePrimAttribute(
				EAttributeModelType::Metadata,
				MetadataName,
				Stringified,
				UsdUtils::EUsdBasicDataTypes::String,
				TEXT(""),
				bAttrReadOnly
			);
		};

	// Lambda to add rows to the model based on USD object metadata. Extracted here because we can reuse it for
	// properties and prims
	TFunction<void(pxr::UsdObject)> AddMetadataEntries = [this, &DisplayEntryAsString](pxr::UsdObject Object)
	{
		std::map<class pxr::TfToken, pxr::VtValue, pxr::TfDictionaryLessThan> MetadataMap = Object.GetAllMetadata();
		for (std::map<class pxr::TfToken, pxr::VtValue, pxr::TfDictionaryLessThan>::iterator Iter = MetadataMap.begin();
			 Iter != MetadataMap.end();
			 ++Iter)
		{
			const pxr::TfToken& Key = Iter->first;
			const pxr::VtValue& Value = Iter->second;

			FString MetadataName = UsdToUnreal::ConvertToken(Key);

			UsdUtils::FConvertedVtValue ConvertedValue;
			if (UsdToUnreal::ConvertValue(UE::FVtValue{Value}, ConvertedValue))
			{
				if (Value.IsArrayValued())
				{
					DisplayEntryAsString(MetadataName, Value);
				}
				else
				{
					const bool bAttrReadOnly = false;
					CreatePrimAttribute(
						EAttributeModelType::Metadata,
						MetadataName,
						ConvertedValue,
						bAttrReadOnly
					);
				}
			}
			// If we don't know how to display this type, try stringifying it
			else
			{
				DisplayEntryAsString(MetadataName, Value);
			}
		}
	};

	if ( UsdStage )
	{
		// Show info about the stage
		if ( PrimPath.Equals( TEXT( "/" ) ) || PrimPath.IsEmpty() )
		{
			const bool bReadOnly = true;
			const FString Role = TEXT( "" );
			CreatePrimAttribute(
				EAttributeModelType::Metadata,
				TEXT( "path" ),
				UsdStage.GetRootLayer().GetRealPath(),
				UsdUtils::EUsdBasicDataTypes::String,
				Role,
				bReadOnly
			);

			FScopedUsdAllocs UsdAllocs;

			std::vector<pxr::TfToken> TokenVector = pxr::SdfSchema::GetInstance().GetMetadataFields( pxr::SdfSpecType::SdfSpecTypePseudoRoot );
			for ( const pxr::TfToken& Token : TokenVector )
			{
				pxr::VtValue VtValue;
				if ( pxr::UsdStageRefPtr( UsdStage )->GetMetadata( Token, &VtValue ) )
				{
					FString AttributeName = UsdToUnreal::ConvertToken( Token );

					UsdUtils::FConvertedVtValue ConvertedValue;
					if ( !UsdToUnreal::ConvertValue( UE::FVtValue{ VtValue }, ConvertedValue ) )
					{
						continue;
					}

					const bool bAttrReadOnly = ConvertedValue.bIsArrayValued;
					CreatePrimAttribute(
						EAttributeModelType::Metadata,
						AttributeName,
						ConvertedValue,
						bAttrReadOnly
					);
				}
			}

		}
		// Show info about a prim
		else if ( UE::FUsdPrim UsdPrim = UsdStage.GetPrimAtPath( UE::FSdfPath( InPrimPath ) ) )
		{
			// For now we can't rename/reparent prims through this
			const bool bReadOnly = true;
			const FString Role = TEXT("");
			CreatePrimAttribute(
				EAttributeModelType::Metadata,
				TEXT("name"),
				UsdPrim.GetName().ToString(),
				UsdUtils::EUsdBasicDataTypes::String,
				Role,
				bReadOnly
			);
			CreatePrimAttribute(
				EAttributeModelType::Metadata,
				TEXT("path"),
				FString(InPrimPath),
				UsdUtils::EUsdBasicDataTypes::String,
				Role,
				bReadOnly
			);
			CreatePrimAttribute(
				EAttributeModelType::Metadata,
				TEXT("kind"),
				UsdToUnreal::ConvertString(IUsdPrim::GetKind(UsdPrim).GetString()),
				UsdUtils::EUsdBasicDataTypes::Token
			);

			FScopedUsdAllocs UsdAllocs;

			pxr::UsdPrim PxrUsdPrim{UsdPrim};

			for ( const pxr::UsdAttribute& PrimAttribute : PxrUsdPrim.GetAttributes() )
			{
				FString AttributeName = UsdToUnreal::ConvertString( PrimAttribute.GetName().GetString() );

				pxr::VtValue VtValue;
				PrimAttribute.Get( &VtValue, TimeCode );

				// Just show arrays as readonly strings for now
				if ( VtValue.IsArrayValued() )
				{
					const bool bAttrReadOnly = true;
					FString Stringified = UsdToUnreal::ConvertString( pxr::TfStringify( VtValue ) );

					// STextBlock can get very slow calculating its desired size for very long string so chop it if needed
					const int32 MaxValueLength = 300;
					if ( Stringified.Len() > MaxValueLength )
					{
						Stringified.LeftInline( MaxValueLength );
						Stringified.Append( TEXT( "..." ) );
					}

					CreatePrimAttribute(
						EAttributeModelType::Attribute,
						AttributeName,
						Stringified,
						UsdUtils::EUsdBasicDataTypes::String,
						TEXT(""),
						bAttrReadOnly
					);
				}
				else
				{
					UsdUtils::FConvertedVtValue ConvertedValue;
					if ( UsdToUnreal::ConvertValue( UE::FVtValue{ VtValue }, ConvertedValue ) )
					{
						const bool bAttrReadOnly = false;
						CreatePrimAttribute(
							EAttributeModelType::Attribute,
							AttributeName,
							ConvertedValue,
							bAttrReadOnly
						);
					}

					if ( PrimAttribute.HasAuthoredConnections() )
					{
						const FString ConnectionAttributeName = AttributeName + TEXT(":connect");

						pxr::SdfPathVector ConnectedSources;
						PrimAttribute.GetConnections( &ConnectedSources );

						for ( pxr::SdfPath& ConnectedSource : ConnectedSources )
						{
							UsdUtils::FConvertedVtValueEntry Entry;
							Entry.Emplace( TInPlaceType< FString >(), UsdToUnreal::ConvertPath( ConnectedSource ) );

							UsdUtils::FConvertedVtValue ConnectionPropertyValue;
							ConnectionPropertyValue.SourceType = UsdUtils::EUsdBasicDataTypes::String;
							ConnectionPropertyValue.Entries = { Entry };

							const bool bConnectionValueReadOnly = true;
							CreatePrimAttribute(
								EAttributeModelType::Attribute,
								ConnectionAttributeName,
								ConnectionPropertyValue,
								bConnectionValueReadOnly
							);
						}
					}
				}
			}

			for (const pxr::UsdRelationship& Relationship : PxrUsdPrim.GetRelationships())
			{
				FString RelationshipName = UsdToUnreal::ConvertString(Relationship.GetName().GetString());

				std::vector<pxr::SdfPath> Targets;
				if (Relationship.GetTargets(&Targets))
				{
					if (Targets.size() == 1)
					{
						FString UETarget = UsdToUnreal::ConvertPath(Targets[0]);
						const bool bAttrReadOnly = false;
						CreatePrimAttribute(
							EAttributeModelType::Relationship,
							RelationshipName,
							UETarget,
							UsdUtils::EUsdBasicDataTypes::String,
							TEXT(""),
							bAttrReadOnly
						);
					}
					else if (Targets.size() > 1)
					{
						FString CombinedTargets;
						for (const pxr::SdfPath& Target : Targets)
						{
							CombinedTargets += UsdToUnreal::ConvertPath(Target) + TEXT(", ");
						}
						CombinedTargets.RemoveFromEnd(TEXT(", "));

						// STextBlock can get very slow calculating its desired size for very long string so chop it if needed
						const int32 MaxValueLength = 300;
						if (CombinedTargets.Len() > MaxValueLength)
						{
							CombinedTargets.LeftInline(MaxValueLength);
							CombinedTargets.Append(TEXT("..."));
						}

						const bool bAttrReadOnly = true;
						CreatePrimAttribute(
							EAttributeModelType::Relationship,
							RelationshipName,
							CombinedTargets,
							UsdUtils::EUsdBasicDataTypes::String,
							TEXT(""),
							bAttrReadOnly
						);
					}
				}
			}

			AddMetadataEntries(PxrUsdPrim);
		}
		// It's a property path?
		else if (!PrimPath.IsEmpty())
		{
			FScopedUsdAllocs Allocs;

			pxr::SdfPath UsdPath = UnrealToUsd::ConvertPath(*PrimPath).Get();
			if (UsdPath.IsPropertyPath())
			{
				pxr::SdfPath PxrPrimPath = UsdPath.GetPrimPath();
				pxr::TfToken PropertyName = UsdPath.GetNameToken();

				pxr::UsdStageRefPtr PxrUsdStage{UsdStage};
				if (pxr::UsdPrim Prim = PxrUsdStage->GetPrimAtPath(PxrPrimPath))
				{
					if (pxr::UsdProperty Property = Prim.GetProperty(PropertyName))
					{
						AddMetadataEntries(Property);
					}
				}
			}
		}
	}

	Sort();
#endif // #if USE_USD_SDK
}

void FUsdPrimAttributesViewModel::Sort()
{
	// When sorting by type, we'll sort according to the types first, but still sort alphabetically within each type
	if (CurrentSortColumn == PrimAttributeColumnIds::TypeColumn)
	{
		if (CurrentSortMode == EColumnSortMode::Ascending)
		{
			PrimAttributes.Sort([](const TSharedPtr<FUsdPrimAttributeViewModel>& A, const TSharedPtr<FUsdPrimAttributeViewModel>& B)
			{
				if(A->Type == B->Type)
				{
					return A->Label < B->Label;
				}
				else
				{
					return A->Type < B->Type;
				}
			});
		}
		else
		{
			PrimAttributes.Sort([](const TSharedPtr<FUsdPrimAttributeViewModel>& A, const TSharedPtr<FUsdPrimAttributeViewModel>& B)
			{
				if(A->Type == B->Type)
				{
					return A->Label < B->Label;
				}
				else
				{
					return A->Type > B->Type;
				}
			});
		}
	}
	// When sorting by name we'll sort alphabetically regardless of type
	else if (CurrentSortColumn == PrimAttributeColumnIds::NameColumn)
	{
		if (CurrentSortMode == EColumnSortMode::Ascending)
		{
			PrimAttributes.Sort([](const TSharedPtr<FUsdPrimAttributeViewModel>& A, const TSharedPtr<FUsdPrimAttributeViewModel>& B)
			{
				return A->Label < B->Label;
			});
		}
		else
		{
			PrimAttributes.Sort([](const TSharedPtr<FUsdPrimAttributeViewModel>& A, const TSharedPtr<FUsdPrimAttributeViewModel>& B)
			{
				return A->Label > B->Label;
			});
		}
	}
}

UE::FUsdStageWeak FUsdPrimAttributesViewModel::GetUsdStage() const
{
	return UsdStage;
}

FString FUsdPrimAttributesViewModel::GetPrimPath() const
{
	return PrimPath;
}

#undef LOCTEXT_NAMESPACE


