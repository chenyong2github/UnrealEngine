// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AddonTools.h"

BEGIN_NAMESPACE_UE_AC

// Static class that execute command (called from menu, palette or module command)
class FCommander
{
  public:
	static void DoSnapshot();
	static void ShowConnectionsDialog();
	static void Export3DToFile();
	static void ShowMessagesDialog();
	static void CopySelection2Clipboard();
	static void ShowHidePalette();
	static void ShowAboutOf();
	static void ZapDB();

  private:
	static void DoSnapshotOrExport(const IO::Location* InExportedFile);
};

END_NAMESPACE_UE_AC
