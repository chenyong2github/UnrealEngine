// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataprepLevelWriter.h"

#include "IDataprepLogger.h"
#include "IDataprepProgressReporter.h"

#include "Internationalization/Internationalization.h"

#define LOCTEXT_NAMESPACE "DataprepLevelWriter"

const FText LevelWriterLabel( LOCTEXT( "DataprepLevelWriterLabel", "Map file writer" ) );
const FText LevelWriterDescription( LOCTEXT( "DataprepLevelWriterDesc", "Writes world's current level and assets to disk" ) );

bool UDataprepLevelWriter::Run()
{
	return true;
}

const FText& UDataprepLevelWriter::GetLabel() const
{
	return LevelWriterLabel;
}

const FText& UDataprepLevelWriter::GetDescription() const
{
	return LevelWriterDescription;
}

#undef LOCTEXT_NAMESPACE
