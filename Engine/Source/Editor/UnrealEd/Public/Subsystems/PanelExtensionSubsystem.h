// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorSubsystem.h"

#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "Templates/SubclassOf.h"
#include "Containers/Queue.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

#include "PanelExtensionSubsystem.generated.h"

struct UNREALED_API FPanelExtensionFactory
{
public:
	FPanelExtensionFactory();

	DECLARE_DELEGATE_RetVal_OneParam(TSharedRef<SWidget>, FGenericCreateWidget, const TArray<UObject*>&);

	/** An identifier to allow removal later on. */
	FName Identifier;

	/** Delegate that generates a the widget. */
	FGenericCreateWidget CreateWidget;
};

class UNREALED_API SExtensionPanel : public SCompoundWidget
{
public:
	~SExtensionPanel();

	SLATE_BEGIN_ARGS(SExtensionPanel)
		: _ExtensionPanelID()
		, _DefaultWidget()
	{}

		/** The ID to identify this Extension point */
		SLATE_ATTRIBUTE(FName, ExtensionPanelID)
		SLATE_ATTRIBUTE(TSharedPtr<SWidget>, DefaultWidget)

	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs);

private:
	void RebuildWidget();

	FName ExtensionPanelID;
	TSharedPtr<SWidget> DefaultWidget;
};

/**
 * UPanelExtensionSubsystem
 * Subsystem for creating extensible panels in the Editor
 */
UCLASS()
class UNREALED_API UPanelExtensionSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	UPanelExtensionSubsystem();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void RegisterPanelFactory(FName ExtensionPanelID, const FPanelExtensionFactory& InPanelExtensionFactory);
	void UnregisterPanelFactory(FName Identifier, FName ExtensionPanelID = NAME_None);

protected:
	friend class SExtensionPanel;
	TSharedRef<SWidget> GetWidget(FName ExtensionPanelID);

	DECLARE_MULTICAST_DELEGATE(FPanelFactoryRegistryChanged);
	FPanelFactoryRegistryChanged& OnPanelFactoryRegistryChanged(FName ExtensionPanelID);

private:
	TMap<FName, TArray<FPanelExtensionFactory>> ExtensionPointMap;
	TMap<FName, FPanelFactoryRegistryChanged> PanelFactoryRegistryChangedCallbackMap;

};
