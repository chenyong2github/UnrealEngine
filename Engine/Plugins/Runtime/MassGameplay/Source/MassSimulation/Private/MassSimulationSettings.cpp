// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassSimulationSettings.h"
#include "MassSchematic.h"

#if WITH_EDITOR
#include "MassEntityEditorModule.h"

void UMassSimulationSettings::PostInitProperties()
{
	Super::PostInitProperties();

	FPipeEditorModule* PipeEditorModule = FModuleManager::GetModulePtr<FPipeEditorModule>("MassEntityEditor");
	if (PipeEditorModule)
	{
		PipeEditorModule->GetOnAssetPropertiesChanged().AddUObject(this, &UMassSimulationSettings::OnAssetPropertiesChanged);
	}
}

void UMassSimulationSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	static const FName TickSchematicsName = GET_MEMBER_NAME_CHECKED(UMassSimulationSettings, TickSchematics);

	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property)
	{
		const FName PropName = PropertyChangedEvent.Property->GetFName();
		if (PropName == TickSchematicsName)
		{
			OnTickSchematicChanged.Broadcast();
		}
	}
}

void UMassSimulationSettings::OnAssetPropertiesChanged(UMassSchematic* MassSchematic, const FPropertyChangedEvent& PropertyChangedEvent)
{
	if (TickSchematics.Find(MassSchematic) != INDEX_NONE)
	{
		OnTickSchematicChanged.Broadcast();
	}
}
#endif // WITH_EDITOR
