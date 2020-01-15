// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AsyncLoading2.h: Unreal async loading #2 definitions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"

class IAsyncPackageLoader;
class FIoDispatcher;
class IEDLBootNotificationManager;

/**
 * Event node.
 */
enum EEventLoadNode2 : uint8
{
	Package_ExportsSerialized,
	Package_PostLoad,
	Package_Delete,
	Package_NumPhases,

	ExportBundle_Process = 0,
	ExportBundle_NumPhases,
};

/**
 * Export filter flags.
 */
enum class EExportFilterFlags : uint8
{
	None,
	NotForClient,
	NotForServer
};

/**
 * Package summary.
 */
struct FPackageSummary
{
	uint32 PackageFlags;
	int32 NameMapOffset;
	int32 ImportMapOffset;
	int32 ExportMapOffset;
	int32 ExportBundlesOffset;
	int32 GraphDataOffset;
	int32 GraphDataSize;
	int32 BulkDataStartOffset;
	int32 GlobalImportIndex;
	int32 Pad;
};

/**
 * Export bundle entry.
 */
struct FExportBundleEntry
{
	enum EExportCommandType
	{
		ExportCommandType_Create,
		ExportCommandType_Serialize
	};
	uint32 LocalExportIndex;
	uint32 CommandType;
};

/**
 * Export bundle meta entry.
 */
struct FExportBundleMetaEntry
{
	uint32 LoadOrder = ~uint32(0);
	uint32 PayloadSize = ~uint32(0);
};

/**
 * Creates a new instance of the AsyncPackageLoader #2.
 *
 * @param InIoDispatcher				The I/O dispatcher.
 * @param InEDLBootNotificationManager	The EDL boot notification manager.
 *
 * @return The async package loader.
 */
IAsyncPackageLoader* MakeAsyncPackageLoader2(FIoDispatcher& InIoDispatcher, IEDLBootNotificationManager& InEDLBootNotificationManager);
