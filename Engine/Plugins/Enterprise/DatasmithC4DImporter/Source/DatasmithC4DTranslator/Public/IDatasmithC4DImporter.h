// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Logging/LogMacros.h"
#include "MeshDescription.h"
#include "Stats/Stats.h"
#include "UObject/StrongObjectPtr.h"

#ifdef _MELANGE_SDK_
#include "DatasmithC4DMelangeSDKEnterGuard.h"
#include "c4d.h"
#include "c4d_browsecontainer.h"
#include "DatasmithC4DMelangeSDKLeaveGuard.h"
#endif

#if WITH_EDITOR
#include "DatasmithMeshExporter.h"
#include "DatasmithSceneExporter.h"
#endif //WITH_EDITOR

#include "AssetRegistryModule.h"
#include "Curves/RichCurve.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/Event.h"
#include "HAL/FileManager.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Math/Matrix.h"
#include "Math/Transform.h"
#include "MeshDescription.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "RawMesh.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"
#include "Widgets/Notifications/SNotificationList.h"

#include "ImathMatrixAlgo.h"

class FDatasmithSceneExporter;
class IDatasmithActorElement;
class IDatasmithCameraActorElement;
class IDatasmithLevelSequenceElement;
class IDatasmithLightActorElement;
class IDatasmithMasterMaterialElement;
class IDatasmithMeshActorElement;
class IDatasmithMeshElement;
class IDatasmithScene;
class IDatasmithTextureElement;
class UDatasmithStaticMeshImportData;
enum class EDatasmithTextureMode : uint8;
struct FCraneCameraAttributes;
struct FDatasmithImportContext;
struct FRichCurve;
struct FDatasmithC4DImportOptions;


/// <summary>
/// Interface for Static and Dynamic DatasmithC4DImporter
/// </summary>
class IDatasmithC4DImporter
{
public:

	/** Updates the used import options to InOptions */
	virtual void SetImportOptions(FDatasmithC4DImportOptions& InOptions) = 0;

	/** Open and load a .4cd file into C4dDocument */
	virtual bool OpenFile(const FString& InFilename) = 0;

	/** Parse the scene contained in the previously opened file and process its content according to parameters from incoming context */
	virtual bool ProcessScene() = 0;

	/** Unload melange resources after importing */
	virtual void UnloadScene() = 0;
	
	virtual void GetGeometriesForMeshElementAndRelease(const TSharedRef<IDatasmithMeshElement> MeshElement, TArray<FMeshDescription>& OutMeshDescriptions) = 0;
	virtual TSharedPtr<IDatasmithLevelSequenceElement> GetLevelSequence() = 0;
};

struct FDatasmithC4DImportOptions
{
	bool bImportEmptyMesh = false;
	bool bOptimizeEmptySingleChildActors = false;
	bool bAlwaysGenerateNormals = false;
	float ScaleVertices = 1.0;
	bool bExportToUDatasmith = false;
};
