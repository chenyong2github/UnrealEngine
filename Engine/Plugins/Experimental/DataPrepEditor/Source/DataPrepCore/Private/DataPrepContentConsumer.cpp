// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataPrepContentConsumer.h"

#include "EditorLevelUtils.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "DataprepContentConsumer"

UDataprepContentConsumer::UDataprepContentConsumer()
{
	TargetContentFolder = FPaths::GetPath( GetOutermost()->GetPathName() );
}

bool UDataprepContentConsumer::Initialize( const ConsumerContext& InContext, FString& OutReason )
{
	Context = InContext;

	// Set package path if empty
	if( TargetContentFolder.IsEmpty() )
	{
		TargetContentFolder = FPaths::GetPath( GetOutermost()->GetPathName() );
	}

	return Context.WorldPtr.IsValid();
}

void UDataprepContentConsumer::Reset()
{
	Context.WorldPtr.Reset();
	Context.ProgressReporterPtr.Reset();
	Context.LoggerPtr.Reset();
	Context.Assets.Empty();

	Context = ConsumerContext();
}

bool UDataprepContentConsumer::SetTargetContentFolder(const FString& InTargetContentFolder)
{
	if( InTargetContentFolder.IsEmpty() )
	{
		TargetContentFolder = FPaths::GetPath( GetOutermost()->GetPathName() );
		OnChanged.Broadcast();
		return true;
	}

	// Pretend creating a dummy package to verify packages could be created under this content folder.
	FString LongPackageName = InTargetContentFolder / TEXT("DummyPackageName");
	if( FPackageName::IsValidLongPackageName( LongPackageName ) )
	{
		TargetContentFolder = InTargetContentFolder;
		OnChanged.Broadcast();
		return true;
	}

	return false;
}

bool UDataprepContentConsumer::SetLevelName(const FString & InLevelName, FText& OutReason)
{
	OutReason = LOCTEXT( "DataprepContentConsumer_SetLevelName", "Not implemented" );
	return false;
}

#undef LOCTEXT_NAMESPACE

