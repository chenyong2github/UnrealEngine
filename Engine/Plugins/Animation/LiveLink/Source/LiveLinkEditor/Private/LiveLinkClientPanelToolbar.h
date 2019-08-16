// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "UObject/GCObject.h"

#include "Input/Reply.h"
#include "LiveLinkPreset.h"
#include "LiveLinkSourceFactory.h"
#include "Types/SlateEnums.h"
#include "UObject/WeakObjectPtrTemplates.h"

class FMenuBuilder;
class FLiveLinkClient;
class ILiveLinkSource;
class IMenu;
class SComboButton;
class STextEntryPopup;
class ULiveLinkSourceFactory;
struct FAssetData;

class SLiveLinkClientPanelToolbar : public SCompoundWidget, public FGCObject
{
	SLATE_BEGIN_ARGS(SLiveLinkClientPanelToolbar){}
	SLATE_END_ARGS()


	void Construct(const FArguments& Args, FLiveLinkClient* InClient);

	//~ FEditorUndoClient interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

private:
	TSharedRef<SWidget> OnGenerateSourceMenu();
	void OnGeneratedSourceMenuOpenChanged(bool bOpen);
	void RetrieveFactorySourcePanel(FMenuBuilder& MenuBuilder, int32 FactoryIndex);
	void ExecuteCreateSource(int32 FactoryIndex);
	void OnSourceCreated(TSharedPtr<ILiveLinkSource> NewSource, FString ConnectionString, TSubclassOf<ULiveLinkSourceFactory> Factory);

	TSharedRef<SWidget> OnPresetGeneratePresetsMenu();
	void OnSaveAsPreset();
	void OnImportPreset(const FAssetData& InPreset);
	FReply OnRevertChanges();
	bool HasLoadedLiveLinkPreset() const;

	void AddVirtualSubject();

private:
	FLiveLinkClient* Client;

	TWeakPtr<IMenu> AddSubjectMenu;
	TWeakPtr<IMenu> VirtualSubjectMenu;
	TWeakPtr<STextEntryPopup> VirtualSubjectPopup;
	TSharedPtr<SComboButton> AddSourceButton;

	TWeakObjectPtr<ULiveLinkPreset> LiveLinkPreset;
	TArray<ULiveLinkSourceFactory*> Factories;
};
