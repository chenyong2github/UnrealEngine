// Copyright Epic Games, Inc. All Rights Reserved.
#include "WaveTableEditorModule.h"


DEFINE_LOG_CATEGORY(LogWaveTableEditor);

namespace WaveTable
{
	namespace Editor
	{
		void FModule::StartupModule()
		{
		}

		void FModule::ShutdownModule()
		{
		}
	} // namespace Editor
} // namespace WaveTable

IMPLEMENT_MODULE(WaveTable::Editor::FModule, WaveTableEditor);
