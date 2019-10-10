// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "ICurveEditorModule.h"
#include "CurveEditorCommands.h"
#include "CurveEditor.h"
#include "CurveEditorViewRegistry.h"
#include "Framework/MultiBox/MultiBoxExtender.h"

class FCurveEditorModule : public ICurveEditorModule
{
public:
	virtual void StartupModule() override
	{
		if (GIsEditor)
		{
			FModuleManager::Get().LoadModule("EditorStyle");
			FCurveEditorCommands::Register();
		}
	}

	virtual void ShutdownModule() override
	{
		FCurveEditorCommands::Unregister();
	}

	virtual FDelegateHandle RegisterEditorExtension(FOnCreateCurveEditorExtension InOnCreateCurveEditorExtension) override
	{
		EditorExtensionDelegates.Add(InOnCreateCurveEditorExtension);
		FDelegateHandle Handle = EditorExtensionDelegates.Last().GetHandle();
		
		return Handle;
	}

	virtual void UnregisterEditorExtension(FDelegateHandle InHandle) override
	{
		EditorExtensionDelegates.RemoveAll([=](const FOnCreateCurveEditorExtension& Delegate) { return Delegate.GetHandle() == InHandle; });
	}


	virtual FDelegateHandle RegisterToolExtension(FOnCreateCurveEditorToolExtension InOnCreateCurveEditorToolExtension) override
	{
		ToolExtensionDelegates.Add(InOnCreateCurveEditorToolExtension);
		FDelegateHandle Handle = ToolExtensionDelegates.Last().GetHandle();

		return Handle;
	}

	virtual void UnregisterToolExtension(FDelegateHandle InHandle) override
	{
		ToolExtensionDelegates.RemoveAll([=](const FOnCreateCurveEditorToolExtension& Delegate) { return Delegate.GetHandle() == InHandle; });
	}

	virtual ECurveEditorViewID RegisterView(FOnCreateCurveEditorView InCreateViewDelegate) override
	{
		return FCurveEditorViewRegistry::Get().RegisterCustomView(InCreateViewDelegate);
	}

	virtual void UnregisterView(ECurveEditorViewID InViewID) override
	{
		return FCurveEditorViewRegistry::Get().UnregisterCustomView(InViewID);
	}

	virtual TArray<FCurveEditorMenuExtender>& GetAllToolBarMenuExtenders() override
	{
		return ToolBarMenuExtenders;
	}

	virtual TArrayView<const FOnCreateCurveEditorExtension> GetEditorExtensions() const override
	{
		return EditorExtensionDelegates;
	}

	virtual TArrayView<const FOnCreateCurveEditorToolExtension> GetToolExtensions() const override
	{
		return ToolExtensionDelegates;
	}

private:
	/** List of editor extension handler delegates Curve Editors will execute when they are created. */
	TArray<FOnCreateCurveEditorExtension> EditorExtensionDelegates;

	/** List of tool extension handler delegates Curve Editors will execute when they are created. */
	TArray<FOnCreateCurveEditorToolExtension> ToolExtensionDelegates;

	/** List of Extenders that that should be called when building the Curve Editor toolbar. */
	TArray<FCurveEditorMenuExtender> ToolBarMenuExtenders;

};

IMPLEMENT_MODULE(FCurveEditorModule, CurveEditor)