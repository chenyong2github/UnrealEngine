// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigModule.h"
#include "Modules/ModuleManager.h"
#include "Sequencer/ControlRigObjectSpawner.h"
#include "ILevelSequenceModule.h"
#include "ControlRigObjectVersion.h"
#include "UObject/DevObjectVersion.h"

// Unique Control Rig Object version id
const FGuid FControlRigObjectVersion::GUID(0xA7820CFB, 0x20A74359, 0x8C542C14, 0x9623CF50);
// Register AnimPhys custom version with Core
FDevVersionRegistration GRegisterControlRigObjectVersion(FControlRigObjectVersion::GUID, FControlRigObjectVersion::LatestVersion, TEXT("Dev-ControlRig"));

void FControlRigModule::StartupModule()
{
	ILevelSequenceModule& LevelSequenceModule = FModuleManager::LoadModuleChecked<ILevelSequenceModule>("LevelSequence");
	OnCreateMovieSceneObjectSpawnerHandle = LevelSequenceModule.RegisterObjectSpawner(FOnCreateMovieSceneObjectSpawner::CreateStatic(&FControlRigObjectSpawner::CreateObjectSpawner));

	ManipulatorMaterial = LoadObject<UMaterial>(nullptr, TEXT("/ControlRig/M_Manip.M_Manip"));
}

void FControlRigModule::ShutdownModule()
{
	ILevelSequenceModule* LevelSequenceModule = FModuleManager::GetModulePtr<ILevelSequenceModule>("LevelSequence");
	if (LevelSequenceModule)
	{
		LevelSequenceModule->UnregisterObjectSpawner(OnCreateMovieSceneObjectSpawnerHandle);
	}
}

IMPLEMENT_MODULE(FControlRigModule, ControlRig)
