// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataprepOperations.h"

#include "DataprepOperationsLibrary.h"

#include "GenericPlatform/GenericPlatformTime.h"
#include "StaticMeshResources.h"

// UI related section
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Input/SComboButton.h"

#define LOCTEXT_NAMESPACE "DatasmithMeshOperations"

#ifdef LOG_TIME
namespace DataprepOperationTime
{
	typedef TFunction<void(FText)> FLogFunc;

	class FTimeLogger
	{
	public:
		FTimeLogger(const FString& InText, FLogFunc&& InLogFunc)
		: StartTime( FPlatformTime::Cycles64() )
		, Text( InText )
		, LogFunc(MoveTemp(InLogFunc))
		{
			UE_LOG( LogDataprep, Log, TEXT("%s ..."), *Text );
		}

		~FTimeLogger()
		{
			// Log time spent to import incoming file in minutes and seconds
			double ElapsedSeconds = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartTime);

			int ElapsedMin = int(ElapsedSeconds / 60.0);
			ElapsedSeconds -= 60.0 * (double)ElapsedMin;
			FText Msg = FText::Format( LOCTEXT("DataprepOperation_LogTime", "{0} took {1} min {2} s."), FText::FromString( Text ), ElapsedMin, FText::FromString( FString::Printf( TEXT("%.3f"), ElapsedSeconds ) ) );
			LogFunc( Msg );
		}

	private:
		uint64 StartTime;
		FString Text;
		FLogFunc LogFunc;
	};
}
#endif


void UDataprepSetLODsOperation::OnExecution_Implementation(const FDataprepContext& InContext)
{
	if(ReductionSettings.Num() > MAX_STATIC_MESH_LODS)
	{
		FText Message = FText::Format( LOCTEXT( "DatasmithMeshOperations_SetLODs_Max", "Limiting number of reduction settings to max allowed, {0}" ), MAX_STATIC_MESH_LODS );
		LogWarning( Message );
	}

	// Limit size of array to MAX_STATIC_MESH_LODS
	const int32 LODCount = FMath::Min( ReductionSettings.Num(), MAX_STATIC_MESH_LODS );
	if( LODCount == 0 )
	{
		FText OutReason = FText( LOCTEXT( "DatasmithMeshOperations_SetLODs", "No reduction settings. Aborting operation..." ) );
		LogInfo( OutReason );
		return;
	}

	// Fill up mesh reduction struct
	FEditorScriptingMeshReductionOptions ReductionOptions;

	ReductionOptions.bAutoComputeLODScreenSize = bAutoComputeLODScreenSize;
	ReductionOptions.ReductionSettings.SetNum( LODCount );

	for(int32 Index = 0; Index < LODCount; ++Index)
	{
		ReductionOptions.ReductionSettings[Index].PercentTriangles = FMath::Clamp( ReductionSettings[Index].PercentTriangles, 0.f, 1.f );
		ReductionOptions.ReductionSettings[Index].ScreenSize = FMath::Clamp( ReductionSettings[Index].ScreenSize, 0.f, 1.f );
	}

#ifdef LOG_TIME
	DataprepOperationTime::FTimeLogger TimeLogger( TEXT("SetLods"), [&]( FText Text) { this->LogInfo( Text ); });
#endif

	// Execute operation
	TArray<UObject*> ModifiedStaticMeshes;
	UDataprepOperationsLibrary::SetLods( InContext.Objects, ReductionOptions, ModifiedStaticMeshes );

	if(ModifiedStaticMeshes.Num() > 0)
	{
		AssetsModified( MoveTemp( ModifiedStaticMeshes ) );
	}
}

UDataprepSetLODGroupOperation::UDataprepSetLODGroupOperation()
{
	TArray<FName> LODGroupNames;
	UStaticMesh::GetLODGroups( LODGroupNames );

	GroupName = LODGroupNames[0];
}

void UDataprepSetLODGroupOperation::OnExecution_Implementation(const FDataprepContext& InContext)
{
#ifdef LOG_TIME
	DataprepOperationTime::FTimeLogger TimeLogger( TEXT("SetLODGroup"), [&]( FText Text) { this->LogInfo( Text ); });
#endif

	// Execute operation
	TArray<UObject*> ModifiedStaticMeshes;
	UDataprepOperationsLibrary::SetLODGroup( InContext.Objects, GroupName, ModifiedStaticMeshes );

	if(ModifiedStaticMeshes.Num() > 0)
	{
		AssetsModified( MoveTemp( ModifiedStaticMeshes ) );
	}
}

void UDataprepSetSimpleCollisionOperation::OnExecution_Implementation(const FDataprepContext& InContext)
{
#ifdef LOG_TIME
	DataprepOperationTime::FTimeLogger TimeLogger( TEXT("SetSimpleCollision"), [&]( FText Text) { this->LogInfo( Text ); });
#endif

	// Execute operation
	TArray<UObject*> ModifiedStaticMeshes;
	UDataprepOperationsLibrary::SetSimpleCollision( InContext.Objects, ShapeType, ModifiedStaticMeshes );

	if(ModifiedStaticMeshes.Num() > 0)
	{
		AssetsModified( MoveTemp( ModifiedStaticMeshes ) );
	}
}

void UDataprepSetConvexDecompositionCollisionOperation::OnExecution_Implementation(const FDataprepContext& InContext)
{
#ifdef LOG_TIME
	DataprepOperationTime::FTimeLogger TimeLogger( TEXT("SetConvexDecompositionCollision"), [&]( FText Text) { this->LogInfo( Text ); });
#endif

	// Execute operation
	TArray<UObject*> ModifiedStaticMeshes;
	UDataprepOperationsLibrary::SetConvexDecompositionCollision( InContext.Objects, HullCount, MaxHullVerts, HullPrecision, ModifiedStaticMeshes );

	if(ModifiedStaticMeshes.Num() > 0)
	{
		AssetsModified( MoveTemp( ModifiedStaticMeshes ) );
	}
}

void UDataprepSetMobilityOperation::OnExecution_Implementation(const FDataprepContext& InContext)
{
#ifdef LOG_TIME
	DataprepOperationTime::FTimeLogger TimeLogger( TEXT("SetMobility"), [&]( FText Text) { this->LogInfo( Text ); });
#endif

	// Execute operation
	UDataprepOperationsLibrary::SetMobility( InContext.Objects, MobilityType );
}

void UDataprepSetMaterialOperation::OnExecution_Implementation(const FDataprepContext& InContext)
{
	if(Material == nullptr)
	{
		FText OutReason = FText( LOCTEXT( "DatasmithMeshOperations_SetMaterial", "No material specified. Aborting operation..." ) );
		LogInfo( OutReason );
		return;
	}

#ifdef LOG_TIME
	DataprepOperationTime::FTimeLogger TimeLogger( TEXT("SetMaterial"), [&]( FText Text) { this->LogInfo( Text ); });
#endif

	// Execute operation
	UDataprepOperationsLibrary::SetMaterial( InContext.Objects, Material );
}

void UDataprepSubstituteMaterialOperation::OnExecution_Implementation(const FDataprepContext& InContext)
{
	if(MaterialSubstitute == nullptr)
	{
		FText OutReason = FText( LOCTEXT( "DatasmithDirProducer_SubstituteMaterial", "No material specified. Aborting operation..." ) );
		LogInfo( OutReason );
		return;
	}

#ifdef LOG_TIME
	DataprepOperationTime::FTimeLogger TimeLogger( TEXT("SubstituteMaterial"), [&]( FText Text) { this->LogInfo( Text ); });
#endif

	// Execute operation
	UDataprepOperationsLibrary::SubstituteMaterial( InContext.Objects, MaterialSearch, StringMatch, MaterialSubstitute );
}

void UDataprepSubstituteMaterialByTableOperation::OnExecution_Implementation(const FDataprepContext& InContext)
{
	if(MaterialDataTable == nullptr)
	{
		FText OutReason = FText( LOCTEXT( "DatasmithDirProducer_SubstituteMaterialByTable", "No data table specified. Aborting operation..." ) );
		LogInfo( OutReason );
		return;
	}

#ifdef LOG_TIME
	DataprepOperationTime::FTimeLogger TimeLogger( TEXT("SubstituteMaterialsByTable"), [&]( FText Text) { this->LogInfo( Text ); });
#endif

	// Execute operation
	UDataprepOperationsLibrary::SubstituteMaterialsByTable( InContext.Objects, MaterialDataTable );
}

void FDataprepSetLOGGroupDetails::OnLODGroupChanged(TSharedPtr<FString> NewValue, ESelectInfo::Type /*SelectInfo*/)
{
	int32 Index = LODGroupOptions.Find(NewValue);
	if (Index != INDEX_NONE && LodGroupPropertyHandle.IsValid() )
	{
		LodGroupPropertyHandle->SetValue( LODGroupNames[Index] );
	}
}

TSharedRef< SWidget > FDataprepSetLOGGroupDetails::CreateWidget()
{
	// Build list of LODGroup names the user will choose from
	LODGroupNames.Reset();
	UStaticMesh::GetLODGroups( LODGroupNames );

	LODGroupOptions.Reset();
	TArray<FText> LODGroupDisplayNames;
	UStaticMesh::GetLODGroupsDisplayNames( LODGroupDisplayNames );
	for (int32 GroupIndex = 0; GroupIndex < LODGroupDisplayNames.Num(); ++GroupIndex)
	{
		LODGroupOptions.Add( MakeShareable( new FString( LODGroupDisplayNames[GroupIndex].ToString() ) ) );
	}

	// Set displayed value to what is used by the SetLODGroup operation
	int32 SelectedIndex = LODGroupNames.Find( DataprepOperation->GroupName );
	if(SelectedIndex == INDEX_NONE)
	{
		SelectedIndex = 0;
		DataprepOperation->GroupName = LODGroupNames[SelectedIndex];
	}
	

	// Create widget
	return	SNew( STextComboBox )
		.OptionsSource( &LODGroupOptions )
		.InitiallySelectedItem(LODGroupOptions[SelectedIndex])
		.OnSelectionChanged( this, &FDataprepSetLOGGroupDetails::OnLODGroupChanged );
}

void FDataprepSetLOGGroupDetails::CustomizeDetails(IDetailLayoutBuilder & DetailBuilder)
{
	TArray< TWeakObjectPtr< UObject > > Objects;
	DetailBuilder.GetObjectsBeingCustomized( Objects );
	check( Objects.Num() > 0 );

	DataprepOperation = Cast< UDataprepSetLODGroupOperation >(Objects[0].Get());
	check( DataprepOperation );

	// #ueent_todo: Remove handling of warning category when this is not considered experimental anymore
	TArray<FName> CategoryNames;
	DetailBuilder.GetCategoryNames( CategoryNames );
	CategoryNames.Remove( FName(TEXT("Warning")) );

	DetailBuilder.HideCategory(FName( TEXT( "Warning" ) ) );

	FName CategoryName = CategoryNames.Num() > 0 ? CategoryNames[0] : FName( TEXT("SetLOGGroup_Internal") );
	IDetailCategoryBuilder& ImportSettingsCategoryBuilder = DetailBuilder.EditCategory( CategoryName, FText::GetEmpty(), ECategoryPriority::Important );

	LodGroupPropertyHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDataprepSetLODGroupOperation, GroupName));

	// Hide GroupName property as it is replaced with custom widget
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDataprepSetLODGroupOperation, GroupName));

	FDetailWidgetRow& CustomAssetImportRow = ImportSettingsCategoryBuilder.AddCustomRow( FText::FromString( TEXT( "LODGroup" ) ) );

	CustomAssetImportRow.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("DatasmithMeshOperationsLabel", "LODGroupName"))
		.ToolTipText(LOCTEXT("DatasmithMeshOperationsTooltip", "List of predefined LODGroup"))
		.Font( DetailBuilder.GetDetailFont() )
	];

	CustomAssetImportRow.ValueContent()
	[
		CreateWidget()
	];
}

void UDataprepSetMeshOperation::OnExecution_Implementation(const FDataprepContext& InContext)
{
	if(StaticMesh == nullptr)
	{
		FText OutReason = FText( LOCTEXT( "DatasmithMeshOperations_SetMesh", "No mesh specified. Aborting operation..." ) );
		LogInfo( OutReason );
		return;
	}

#ifdef LOG_TIME
	DataprepOperationTime::FTimeLogger TimeLogger( TEXT("SetMesh"), [&]( FText Text) { this->LogInfo( Text ); });
#endif

	// Execute operation
	UDataprepOperationsLibrary::SetMesh( InContext.Objects, StaticMesh );
}

#undef LOCTEXT_NAMESPACE
