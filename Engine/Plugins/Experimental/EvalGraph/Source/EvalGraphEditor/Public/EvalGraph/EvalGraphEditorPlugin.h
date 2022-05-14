// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
//#include "Toolkits/IToolkit.h"

//class ISlateStyle;
//class FSlateStyleSet;
class FEvalGraphSNodeFactory;
class FEvalGraphAssetActions;

/**
 * The public interface to this module
 */
class IEvalGraphEditorPlugin : public IModuleInterface
{
public:
	virtual void StartupModule();
	virtual void ShutdownModule();

	TSharedRef<FAssetEditorToolkit> CreateEvalGraphAssetEditor(const EToolkitMode::Type Mode, const TSharedPtr< IToolkitHost >& InitToolkitHost, UObject* EvalGraph);


	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static inline IEvalGraphEditorPlugin& Get()
	{
		return FModuleManager::LoadModuleChecked< IEvalGraphEditorPlugin >( "EvalGraphEditor" );
	}

	/**
	 * Checks to see if this module is loaded and ready.  It is only valid to call Get() if IsAvailable() returns true.
	 *
	 * @return True if the module is loaded and ready to use
	 */
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded( "EvalGraphEditor" );
	}

	//TSharedPtr<FSlateStyleSet> GetStyleSet() { return StyleSet; }
	//static FName GetEditorStyleName();
	//static const ISlateStyle* GetEditorStyle();

private:
	void RegisterMenus();

private:

	FEvalGraphAssetActions* EvalGraphAssetActions;

	//TSharedPtr<FSlateStyleSet> StyleSet;
	TSharedPtr<FEvalGraphSNodeFactory> EvalGraphSNodeFactory;

};

