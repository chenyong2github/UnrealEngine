// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDStageImportOptions.h"

#include "USDSchemasModule.h"
#include "USDSchemaTranslator.h"

#include "Modules/ModuleManager.h"
#include "UObject/UnrealType.h"

UUsdStageImportOptions::UUsdStageImportOptions(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bImportActors = true;
	bImportGeometry = true;
	bImportSkeletalAnimations = true;
	bImportLevelSequences = true;
	bImportMaterials = true;

	PurposesToImport = (int32) (EUsdPurpose::Default | EUsdPurpose::Proxy | EUsdPurpose::Render | EUsdPurpose::Guide);
	NaniteTriangleThreshold = INT32_MAX;
	IUsdSchemasModule& UsdSchemasModule = FModuleManager::Get().LoadModuleChecked< IUsdSchemasModule >( TEXT("USDSchemas") );
	RenderContextToImport = UsdSchemasModule.GetRenderContextRegistry().GetUnrealRenderContext();
	MaterialPurpose = *UnrealIdentifiers::MaterialAllPurpose;
	bOverrideStageOptions = false;
	StageOptions.MetersPerUnit = 0.01f;
	StageOptions.UpAxis = EUsdUpAxis::ZAxis;

	ExistingActorPolicy = EReplaceActorPolicy::Replace;
	ExistingAssetPolicy = EReplaceAssetPolicy::Replace;

	bPrimPathFolderStructure = false;
	KindsToCollapse = ( int32 ) ( EUsdDefaultKind::Component | EUsdDefaultKind::Subcomponent );
	bMergeIdenticalMaterialSlots = true;
	bCollapseTopLevelPointInstancers = false;
	bInterpretLODs = true;
}

void UUsdStageImportOptions::EnableActorImport( bool bEnable )
{
	for ( TFieldIterator<FProperty> PropertyIterator( UUsdStageImportOptions::StaticClass() ); PropertyIterator; ++PropertyIterator )
	{
		FProperty* Property = *PropertyIterator;
		if ( Property && Property->GetFName() == GET_MEMBER_NAME_CHECKED( UUsdStageImportOptions, bImportActors ) )
		{
			if ( bEnable )
			{
				Property->SetMetaData(TEXT("ToolTip"), TEXT("Whether to spawn imported actors into the current level"));
				Property->ClearPropertyFlags(CPF_EditConst);
			}
			else
			{
				bImportActors = false;
				Property->SetMetaData(TEXT("ToolTip"), TEXT("Actor import is disabled when importing via the Content Browser. Use File->\"Import into Level...\" to also import actors."));
				Property->SetPropertyFlags(CPF_EditConst);
			}
			break;
		}
	}
}

void UUsdStageImportOptions::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		SaveConfig();
	}
}
