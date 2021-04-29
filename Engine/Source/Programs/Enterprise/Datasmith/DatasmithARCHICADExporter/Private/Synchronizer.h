// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Utils/AddonTools.h"
#include "Utils/ViewState.h"

#include "SyncContext.h"

#include "Model.hpp"

class FDatasmithDirectLink;

BEGIN_NAMESPACE_UE_AC

// Class to synchronize 3D data thru  DirecLink
class FSynchronizer
{
	FDatasmithDirectLink& DatasmithDirectLink;
	FSyncDatabase*		  SyncDatabase = nullptr;
	FViewState			  ViewState;

  public:
	// Constructor
	FSynchronizer();

	// Destructor
	~FSynchronizer();

	// Return the synchronizer (create it if not already created)
	static FSynchronizer& Get();

	// Return the current synchronizer if any
	static FSynchronizer* GetCurrent();

	// FreeData is called, so we must free all our stuff
	static void DeleteSingleton();

	// Delete the database (Usualy because document has changed)
	void Reset(const utf8_t* InReason);

	// Inform that a new project has been open
	void ProjectOpen();

	// Inform that current project has been save (maybe name changed)
	void ProjectSave();

	// Inform that the project has been closed
	void ProjectClosed();

	// Do a snapshot of the model 3D data
	void DoSnapshot(const ModelerAPI::Model& InModel);

	// Return true if view or at least one material changed
	bool NeedLiveLinkUpdate() const;

	static void GetProjectPathAndName(GS::UniString* OutPath, GS::UniString* OutName);

	static void DumpScene(const TSharedRef< IDatasmithScene >& InScene);
};

END_NAMESPACE_UE_AC
