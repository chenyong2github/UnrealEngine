// Copyright Epic Games, Inc. All Rights Reserved.

#pragma  once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"


class IMassGameplayEditor;
class FAssetTypeActions_Base;
struct FGraphPanelNodeFactory;
class UMassSchematic;

/**
* The public interface to this module
*/
class MASSGAMEPLAYEDITOR_API FMassGameplayEditorModule : public IModuleInterface
{
public:
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAssetPropertiesChanged, class UMassSchematic* /*MassSchematic*/, const FPropertyChangedEvent& /*PropertyChangedEvent*/);

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

protected:
	void RegisterSectionMappings();
};
