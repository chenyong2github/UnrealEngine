// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Map.h"
#include "UObject/NameTypes.h"
#include "Templates/SharedPointer.h"
#include "Framework/Commands/UICommandInfo.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Commands/Commands.h"

class FEditorViewportClient;

class FRayTracingDebugVisualizationMenuCommands : public TCommands<FRayTracingDebugVisualizationMenuCommands>
{
public:
	struct FRayTracingDebugVisualizationRecord
	{
		uint32 Index;
		FName Name;
		TSharedPtr<FUICommandInfo> Command;

		FRayTracingDebugVisualizationRecord()
			: Index(0),
				Name(),
				Command()
		{
		}
	};

	UNREALED_API FRayTracingDebugVisualizationMenuCommands();

	static UNREALED_API void BuildVisualisationSubMenu(FMenuBuilder& Menu);

	UNREALED_API virtual void RegisterCommands() override;

	UNREALED_API void BindCommands(FUICommandList& CommandList, const TSharedPtr<FEditorViewportClient>& Client) const;

	static UNREALED_API bool DebugModeShouldBeTonemapped(const FName& RayTracingDebugModeName);

private:
	void BuildCommandMap();

	void CreateRayTracingDebugVisualizationCommands();

	void AddRayTracingDebugVisualizationCommandsToMenu(FMenuBuilder& menu) const;

	static void ChangeRayTracingDebugVisualizationMode(TWeakPtr<FEditorViewportClient> WeakClient, FName InName);
	static bool IsRayTracingDebugVisualizationModeSelected(TWeakPtr<FEditorViewportClient> WeakClient, FName InName);

	TArray<FRayTracingDebugVisualizationRecord> RayTracingDebugVisualizationCommands;

	static TArray<FText> RayTracingDebugModeNames;
};
