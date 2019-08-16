// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataprepOperations.h"

#include "DataPrepOperationsLibrary.h"

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

void UDataprepSetLODsOperation::OnExecution_Implementation(const FDataprepContext& InContext)
{
	// Collect start time to log amount of time spent to import incoming file
	uint64 StartTime = FPlatformTime::Cycles64();

	const int32 LODCount = FMath::Min( ReductionSettings.Num(), MAX_STATIC_MESH_LODS );
	if( LODCount == 0 )
	{
		FText OutReason = FText( LOCTEXT( "DatasmithMeshOperations_SetLODs", "No reduction settings. Aborting operation..." ) );
		LogInfo( OutReason );
		// #ueent_todo: Remove call to UE_LOG when DataprepLogger is operational
		UE_LOG(LogDataprep, Log, TEXT("UDataprepSetLODsOperation: %s"), *OutReason.ToString());
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

	// Execute operation
	UDataprepOperationsLibrary::SetLods( InContext.Objects, ReductionOptions );

	// Log time spent to import incoming file in minutes and seconds
	double ElapsedSeconds = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartTime);

	int ElapsedMin = int(ElapsedSeconds / 60.0);
	ElapsedSeconds -= 60.0 * (double)ElapsedMin;
	FText OutReason = FText::Format( LOCTEXT("DatasmithMeshOperations_SetLODsTime", "SetLODs took {0} min {1} s."), ElapsedMin, FText::FromString( FString::Printf( TEXT("%.3f"), ElapsedSeconds ) ) );
	LogInfo( OutReason );
	UE_LOG(LogDataprep, Log, TEXT("%s"), *OutReason.ToString());
}

UDataprepSetLODGroupOperation::UDataprepSetLODGroupOperation()
{
	TArray<FName> LODGroupNames;
	UStaticMesh::GetLODGroups( LODGroupNames );

	GroupName = LODGroupNames[0];
}

void UDataprepSetLODGroupOperation::OnExecution_Implementation(const FDataprepContext& InContext)
{
	// Collect start time to log amount of time spent to import incoming file
	uint64 StartTime = FPlatformTime::Cycles64();

	// Execute operation
	UDataprepOperationsLibrary::SetLODGroup( InContext.Objects, GroupName );

	// Log time spent to import incoming file in minutes and seconds
	double ElapsedSeconds = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartTime);

	int ElapsedMin = int(ElapsedSeconds / 60.0);
	ElapsedSeconds -= 60.0 * (double)ElapsedMin;
	FText OutReason = FText::Format( LOCTEXT("DatasmithMeshOperations_SetLODGroupTime", "SetLODs took {0} min {1} s."), ElapsedMin, FText::FromString( FString::Printf( TEXT("%.3f"), ElapsedSeconds ) ) );
	LogInfo( OutReason );
	UE_LOG(LogDataprep, Log, TEXT("%s"), *OutReason.ToString());
}

void UDataprepSetSimpleCollisionOperation::OnExecution_Implementation(const FDataprepContext& InContext)
{
	// Collect start time to log amount of time spent to import incoming file
	uint64 StartTime = FPlatformTime::Cycles64();

	// Execute operation
	UDataprepOperationsLibrary::SetSimpleCollision( InContext.Objects, ShapeType );

	// Log time spent to import incoming file in minutes and seconds
	double ElapsedSeconds = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartTime);

	int ElapsedMin = int(ElapsedSeconds / 60.0);
	ElapsedSeconds -= 60.0 * (double)ElapsedMin;
	FText OutReason = FText::Format( LOCTEXT("DatasmithMeshOperations_SetSimpleCollisionTime", "SetLODs took {0} min {1} s."), ElapsedMin, FText::FromString( FString::Printf( TEXT("%.3f"), ElapsedSeconds ) ) );
	LogInfo( OutReason );
	UE_LOG(LogDataprep, Log, TEXT("%s"), *OutReason.ToString());
}

void UDataprepSetConvexDecompositionCollisionOperation::OnExecution_Implementation(const FDataprepContext& InContext)
{
	// Collect start time to log amount of time spent to import incoming file
	uint64 StartTime = FPlatformTime::Cycles64();

	// Execute operation
	UDataprepOperationsLibrary::SetConvexDecompositionCollision( InContext.Objects, HullCount, MaxHullVerts, HullPrecision );

	// Log time spent to import incoming file in minutes and seconds
	double ElapsedSeconds = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartTime);

	int ElapsedMin = int(ElapsedSeconds / 60.0);
	ElapsedSeconds -= 60.0 * (double)ElapsedMin;
	FText OutReason = FText::Format( LOCTEXT("DatasmithMeshOperations_SetConvexDecompositionCollisionTime", "SetLODs took {0} min {1} s."), ElapsedMin, FText::FromString( FString::Printf( TEXT("%.3f"), ElapsedSeconds ) ) );
	LogInfo( OutReason );
	UE_LOG(LogDataprep, Log, TEXT("%s"), *OutReason.ToString());
}

void UDataprepSetGenerateLightmapUVsOperation::OnExecution_Implementation(const FDataprepContext& InContext)
{
	// Collect start time to log amount of time spent to import incoming file
	uint64 StartTime = FPlatformTime::Cycles64();

	// Execute operation
	UDataprepOperationsLibrary::SetGenerateLightmapUVs( InContext.Objects, bGenerateLightmapUVs );

	// Log time spent to import incoming file in minutes and seconds
	double ElapsedSeconds = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartTime);

	int ElapsedMin = int(ElapsedSeconds / 60.0);
	ElapsedSeconds -= 60.0 * (double)ElapsedMin;
	FText OutReason = FText::Format( LOCTEXT("DatasmithMeshOperations_SetGenerateLightmapUVsTime", "SetLODs took {0} min {1} s."), ElapsedMin, FText::FromString( FString::Printf( TEXT("%.3f"), ElapsedSeconds ) ) );
	LogInfo( OutReason );
	UE_LOG(LogDataprep, Log, TEXT("%s"), *OutReason.ToString());
}

void UDataprepSetMobilityOperation::OnExecution_Implementation(const FDataprepContext& InContext)
{
	// Collect start time to log amount of time spent to import incoming file
	uint64 StartTime = FPlatformTime::Cycles64();

	// Execute operation
	UDataprepOperationsLibrary::SetMobility( InContext.Objects, MobilityType );

	// Log time spent to import incoming file in minutes and seconds
	double ElapsedSeconds = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartTime);

	int ElapsedMin = int(ElapsedSeconds / 60.0);
	ElapsedSeconds -= 60.0 * (double)ElapsedMin;
	FText OutReason = FText::Format( LOCTEXT("DatasmithMeshOperations_SetMobilityTime", "SetLODs took {0} min {1} s."), ElapsedMin, FText::FromString( FString::Printf( TEXT("%.3f"), ElapsedSeconds ) ) );
	LogInfo( OutReason );
	UE_LOG(LogDataprep, Log, TEXT("%s"), *OutReason.ToString());
}

void UDataprepSetMaterialOperation::OnExecution_Implementation(const FDataprepContext& InContext)
{
	if(MaterialSubstitute == nullptr)
	{
		FText OutReason = FText( LOCTEXT( "DatasmithMeshOperations_SetMaterial", "No material specified. Aborting operation..." ) );
		LogInfo( OutReason );
		// #ueent_todo: Remove call to UE_LOG when DataprepLogger is operational
		UE_LOG(LogDataprep, Log, TEXT("UDataprepSetMaterialOperation: %s"), *OutReason.ToString());
		return;
	}

	// Collect start time to log amount of time spent to import incoming file
	uint64 StartTime = FPlatformTime::Cycles64();

	// Execute operation
	UDataprepOperationsLibrary::SetMaterial( InContext.Objects, MaterialSubstitute );

	// Log time spent to import incoming file in minutes and seconds
	double ElapsedSeconds = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartTime);

	int ElapsedMin = int(ElapsedSeconds / 60.0);
	ElapsedSeconds -= 60.0 * (double)ElapsedMin;
	FText OutReason = FText::Format( LOCTEXT("DatasmithMeshOperations_SetMaterialTime", "SetLODs took {0} min {1} s."), ElapsedMin, FText::FromString( FString::Printf( TEXT("%.3f"), ElapsedSeconds ) ) );
	LogInfo( OutReason );
	UE_LOG(LogDataprep, Log, TEXT("%s"), *OutReason.ToString());
}

void UDataprepSubstituteMaterialOperation::OnExecution_Implementation(const FDataprepContext& InContext)
{
	if(MaterialSubstitute == nullptr)
	{
		FText OutReason = FText( LOCTEXT( "DatasmithDirProducer_SubstituteMaterial", "No material specified. Aborting operation..." ) );
		LogInfo( OutReason );
		// #ueent_todo: Remove call to UE_LOG when DataprepLogger is operational
		UE_LOG(LogDataprep, Log, TEXT("UDataprepSubstituteMaterialOperation: %s"), *OutReason.ToString());
		return;
	}

	// Collect start time to log amount of time spent to import incoming file
	uint64 StartTime = FPlatformTime::Cycles64();

	// Execute operation
	UDataprepOperationsLibrary::SubstituteMaterial( InContext.Objects, MaterialSearch, StringMatch, MaterialSubstitute );

	// Log time spent to import incoming file in minutes and seconds
	double ElapsedSeconds = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartTime);

	int ElapsedMin = int(ElapsedSeconds / 60.0);
	ElapsedSeconds -= 60.0 * (double)ElapsedMin;
	FText OutReason = FText::Format( LOCTEXT("DatasmithMeshOperations_SubstituteMaterialTime", "SetLODs took {0} min {1} s."), ElapsedMin, FText::FromString( FString::Printf( TEXT("%.3f"), ElapsedSeconds ) ) );
	LogInfo( OutReason );
	UE_LOG(LogDataprep, Log, TEXT("%s"), *OutReason.ToString());
}

void UDataprepSubstituteMaterialByTableOperation::OnExecution_Implementation(const FDataprepContext& InContext)
{
	if(MaterialDataTable == nullptr)
	{
		FText OutReason = FText( LOCTEXT( "DatasmithDirProducer_SubstituteMaterialByTable", "No data table specified. Aborting operation..." ) );
		LogInfo( OutReason );
		// #ueent_todo: Remove call to UE_LOG when DataprepLogger is operational
		UE_LOG(LogDataprep, Log, TEXT("UDataprepSubstituteMaterialByTableOperation: %s"), *OutReason.ToString());
		return;
	}

	// Collect start time to log amount of time spent to import incoming file
	uint64 StartTime = FPlatformTime::Cycles64();

	// Execute operation
	UDataprepOperationsLibrary::SubstituteMaterialsByTable( InContext.Objects, MaterialDataTable );

	// Log time spent to import incoming file in minutes and seconds
	double ElapsedSeconds = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartTime);

	int ElapsedMin = int(ElapsedSeconds / 60.0);
	ElapsedSeconds -= 60.0 * (double)ElapsedMin;
	FText OutReason = FText::Format( LOCTEXT("DatasmithMeshOperations_SubstituteMaterialsByTableTime", "SetLODs took {0} min {1} s."), ElapsedMin, FText::FromString( FString::Printf( TEXT("%.3f"), ElapsedSeconds ) ) );
	LogInfo( OutReason );
	UE_LOG(LogDataprep, Log, TEXT("%s"), *OutReason.ToString());
}

void UDataprepRemoveObjectsOperation::OnExecution_Implementation(const FDataprepContext& InContext)
{
	// Collect start time to log amount of time spent to import incoming file
	uint64 StartTime = FPlatformTime::Cycles64();
	int32 ObjectsCount = InContext.Objects.Num();

	UDataprepOperationsLibrary::RemoveObjects( InContext.Objects );

	// Log time spent to import incoming file in minutes and seconds
	double ElapsedSeconds = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartTime);

	int ElapsedMin = int(ElapsedSeconds / 60.0);
	ElapsedSeconds -= 60.0 * (double)ElapsedMin;
	UE_LOG( LogDataprep, Log, TEXT("Removal of %d object(s) took [%d min %.3f s]"), ObjectsCount, ElapsedMin, ElapsedSeconds );
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

#undef LOCTEXT_NAMESPACE
