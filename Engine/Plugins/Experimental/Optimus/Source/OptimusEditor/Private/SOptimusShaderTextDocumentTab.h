// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class IOptimusShaderTextProvider;
class FOptimusHLSLSyntaxHighlighter;

class SVerticalBox;
class FSpawnTabArgs;
class FTabManager;
class SOptimusShaderTextDocumentSubTab;
class SDockTab;


class SOptimusShaderTextDocumentTab : public SCompoundWidget
{
public:
	static const FName DeclarationsTabId;
	static const FName ShaderTextTabId;
	static TArray<FName> GetAllTabIds();
	static void OnHostTabClosed(TSharedRef<SDockTab> InDocumentHostTab);
	
	
	SOptimusShaderTextDocumentTab();
	virtual ~SOptimusShaderTextDocumentTab() override;

	SLATE_BEGIN_ARGS(SOptimusShaderTextDocumentTab) {};
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UObject* InShaderTextProviderObject, TSharedRef<SDockTab> InDocumentHostTab);
	
private:
	TSharedRef<SDockTab> OnSpawnSubTab(const FSpawnTabArgs& Args, FName SubTabID);

	bool HasValidShaderTextProvider() const;
	IOptimusShaderTextProvider* GetProviderInterface() const;
	FText GetDeclarationsAsText() const;
	FText GetShaderTextAsText() const;
	void OnShaderTextChanged(const FText& InText) const;
	void OnDiagnosticsUpdated() const;

	TSharedRef<FOptimusHLSLSyntaxHighlighter> SyntaxHighlighterDeclarations;
	TSharedRef<FOptimusHLSLSyntaxHighlighter> SyntaxHighlighterShaderText;

	TSharedPtr<FTabManager> TabManager;
	
	TWeakObjectPtr<UObject> ShaderTextProviderObject;

	// ptr needed for text search
	TSharedPtr<SOptimusShaderTextDocumentSubTab> DeclarationsSubTab;
	TSharedPtr<SOptimusShaderTextDocumentSubTab> ShaderTextSubTab;
};
