// Copyright Epic Games, Inc. All Rights Reserved.

#include "SUSDPrimInfo.h"

#include "USDIntegrationUtils.h"
#include "USDStageActor.h"
#include "USDStageModule.h"
#include "USDTypesConversion.h"
#include "Widgets/SUSDIntegrationsPanel.h"
#include "Widgets/SUSDObjectFieldList.h"
#include "Widgets/SUSDReferencesList.h"
#include "Widgets/SUSDVariantSetsList.h"

#include "UsdWrappers/UsdStage.h"
#include "UsdWrappers/UsdPrim.h"
#include "UsdWrappers/SdfPath.h"

#include "Styling/AppStyle.h"

#include "Widgets/Views/SListView.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SSeparator.h"
#include "Modules/ModuleManager.h"
#include "Engine/World.h"
#include "Algo/Find.h"

#if USE_USD_SDK

#define LOCTEXT_NAMESPACE "SUSDPrimInfo"

void SUsdPrimInfo::Construct( const FArguments& InArgs )
{
	ChildSlot
	[
		SNew(SVerticalBox)

		+SVerticalBox::Slot()
		.FillHeight(1.f)
		[
			SNew(SBox)
			.Content()
			[
				SAssignNew(PropertiesList, SUsdObjectFieldList)
				.NameColumnText(LOCTEXT("NameColumnText", "Name"))
				.OnSelectionChanged_Lambda([this](const TSharedPtr<FUsdObjectFieldViewModel>& NewSelection, ESelectInfo::Type SelectionType)
				{
					// Display property metadata if we have exactly one selected
					TArray<FString> SelectedFields = PropertiesList->GetSelectedFieldNames();
					if (PropertiesList && SelectedFields.Num() == 1 && NewSelection &&
						(NewSelection->Type == EObjectFieldType::Attribute || NewSelection->Type == EObjectFieldType::Relationship)
					)
					{
						PropertyMetadataPanel->SetObjectPath(
							PropertiesList->GetUsdStage(),
							*(FString{PropertiesList->GetObjectPath()} + "." + SelectedFields[0])
						);
						PropertyMetadataPanel->SetVisibility(EVisibility::Visible);
					}
					else
					{
						PropertyMetadataPanel->SetObjectPath({}, TEXT(""));
						PropertyMetadataPanel->SetVisibility(EVisibility::Collapsed);
					}
				})
			]
		]

		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew( SBox )
			.Content()
			[
				SAssignNew(PropertyMetadataPanel, SUsdObjectFieldList)
				.NameColumnText_Lambda([this]() -> FText
				{
					FString PropertyName = FPaths::GetExtension(PropertyMetadataPanel->GetObjectPath());
					return FText::FromString(FString::Printf(TEXT("%s metadata"), *PropertyName));
				})
				.Visibility(EVisibility::Collapsed)
			]
		]

		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew( SBox )
			.Content()
			[
				SAssignNew( IntegrationsPanel, SUsdIntegrationsPanel )
			]
		]

		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew( SBox )
			.Content()
			[
				SAssignNew( VariantsList, SVariantsList )
			]
		]

		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew( SBox )
			.Content()
			[
				SAssignNew( ReferencesList, SUsdReferencesList )
			]
		]
	];
}

void SUsdPrimInfo::SetPrimPath(const UE::FUsdStageWeak& UsdStage, const TCHAR* PrimPath)
{
	if ( PropertiesList )
	{
		PropertiesList->SetObjectPath(UsdStage, PrimPath);

		PropertyMetadataPanel->SetObjectPath({}, TEXT(""));
		PropertyMetadataPanel->ClearSelection();
	}

	if ( IntegrationsPanel )
	{
		IntegrationsPanel->SetPrimPath( UsdStage, PrimPath );
	}

	if ( VariantsList )
	{
		VariantsList->SetPrimPath( UsdStage, PrimPath );
	}

	if ( ReferencesList )
	{
		ReferencesList->SetPrimPath( UsdStage, PrimPath );
	}
}

#undef LOCTEXT_NAMESPACE

#endif // #if USE_USD_SDK
