// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Map.h"
#include "UObject/NameTypes.h"
#include "Templates/SharedPointer.h"
#include "Framework/Commands/UICommandInfo.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Commands/Commands.h"

class FEditorViewportClient;

class FGPUSkinCacheVisualizationMenuCommands : public TCommands<FGPUSkinCacheVisualizationMenuCommands>
{
public:
	enum class FGPUSkinCacheVisualizationType : uint8
	{
		Overview,
		Memory,
		RayTracingLODOffset
	};

	struct FGPUSkinCacheVisualizationRecord
	{
		FName Name;
		TSharedPtr<FUICommandInfo> Command;
		FGPUSkinCacheVisualizationType Type;

		FGPUSkinCacheVisualizationRecord()
			: Name()
			, Command()
			, Type(FGPUSkinCacheVisualizationType::Overview)
		{
		}
	};

	typedef TMultiMap<FName, FGPUSkinCacheVisualizationRecord> TGPUSkinCacheVisualizationModeCommandMap;
	typedef TGPUSkinCacheVisualizationModeCommandMap::TConstIterator TCommandConstIterator;

	UNREALED_API FGPUSkinCacheVisualizationMenuCommands();

	UNREALED_API TCommandConstIterator CreateCommandConstIterator() const;

	static UNREALED_API void BuildVisualisationSubMenu(FMenuBuilder& Menu);

	UNREALED_API virtual void RegisterCommands() override;

	UNREALED_API void BindCommands(FUICommandList& CommandList, const TSharedPtr<FEditorViewportClient>& Client) const;

	inline bool IsPopulated() const
	{
		return CommandMap.Num() > 0;
	}

private:
	UNREALED_API void BuildCommandMap();
	UNREALED_API bool AddCommandTypeToMenu(FMenuBuilder& Menu, const FGPUSkinCacheVisualizationType Type, bool bSeparatorBefore = false) const;

	static UNREALED_API void ChangeGPUSkinCacheVisualizationMode(TWeakPtr<FEditorViewportClient> WeakClient, FName InName);
	static UNREALED_API bool IsGPUSkinCacheVisualizationModeSelected(TWeakPtr<FEditorViewportClient> WeakClient, FName InName);

	TGPUSkinCacheVisualizationModeCommandMap CommandMap;
};
