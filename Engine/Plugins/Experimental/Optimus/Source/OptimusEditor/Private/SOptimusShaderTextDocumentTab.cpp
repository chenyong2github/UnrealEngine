#include "SOptimusShaderTextDocumentTab.h"
#include "IOptimusShaderTextProvider.h"
#include "SOptimusShaderTextDocumentSubTab.h"

#include "OptimusHLSLSyntaxHighlighter.h"

#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SWindow.h"
#include "Widgets/Docking/SDockTab.h"


#include "Framework/Docking/TabManager.h"
#include "Framework/Application/SlateApplication.h"


#define LOCTEXT_NAMESPACE "OptimusShaderTextDocumentTab"

const FName SOptimusShaderTextDocumentTab::DeclarationsTabId = TEXT("DeclarationsTab");
const FName SOptimusShaderTextDocumentTab::ShaderTextTabId = TEXT("ShaderTextTab");


TArray<FName> SOptimusShaderTextDocumentTab::GetAllTabIds()
{
	TArray<FName> TabIds;
	TabIds.AddUnique(DeclarationsTabId);
	TabIds.AddUnique(ShaderTextTabId);
	return TabIds;
}

SOptimusShaderTextDocumentTab::SOptimusShaderTextDocumentTab()
	:SyntaxHighlighterDeclarations(FOptimusHLSLSyntaxHighlighter::Create())
	,SyntaxHighlighterShaderText(FOptimusHLSLSyntaxHighlighter::Create())

{
}

SOptimusShaderTextDocumentTab::~SOptimusShaderTextDocumentTab()
{
	if (HasValidShaderTextProvider())
	{
		GetProviderInterface()->OnDiagnosticsUpdated().RemoveAll(this);
	}
}

void SOptimusShaderTextDocumentTab::Construct(const FArguments& InArgs, UObject* InShaderTextProviderObject, TSharedRef<SDockTab> InDocumentHostTab)
{
	ShaderTextProviderObject = InShaderTextProviderObject;

	check(HasValidShaderTextProvider());

	GetProviderInterface()->OnDiagnosticsUpdated().AddSP(this, &SOptimusShaderTextDocumentTab::OnDiagnosticsUpdated);

	TabManager = FGlobalTabmanager::Get()->NewTabManager(InDocumentHostTab);
	InDocumentHostTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateStatic(&SOptimusShaderTextDocumentTab::OnHostTabClosed) );
	
	TabManager->RegisterTabSpawner(
		DeclarationsTabId,
		FOnSpawnTab::CreateRaw(this, &SOptimusShaderTextDocumentTab::OnSpawnSubTab, DeclarationsTabId)
	);
	TabManager->RegisterTabSpawner(
		ShaderTextTabId,
		FOnSpawnTab::CreateRaw(this, &SOptimusShaderTextDocumentTab::OnSpawnSubTab, ShaderTextTabId)
	);

	const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("SOptimusShaderTextEditor_DocumentTab1.0") 
	->AddArea
	(
		FTabManager::NewPrimaryArea()
		->SetOrientation(EOrientation::Orient_Vertical)
		->Split
		(
			FTabManager::NewStack()
			->SetHideTabWell(true)
			->AddTab(DeclarationsTabId, ETabState::OpenedTab)
		)
		->Split
		(
			FTabManager::NewStack()
			->SetHideTabWell(true)
			->AddTab(ShaderTextTabId, ETabState::OpenedTab)	
		)
	);
	
	const TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow(InDocumentHostTab);

	ChildSlot
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.FillHeight(1.0)
		[
			TabManager->RestoreFrom(Layout, ParentWindow).ToSharedRef()
		]
	];
}

void SOptimusShaderTextDocumentTab::OnHostTabClosed(TSharedRef<SDockTab> InDocumentHostTab)
{
	const TSharedPtr<FTabManager> SubTabManager = FGlobalTabmanager::Get()->GetTabManagerForMajorTab(InDocumentHostTab);

	const TArray<FName> TabIdsToFind = GetAllTabIds();

	for (const FName TabId : TabIdsToFind)
	{
		while(true)
		{
			TSharedPtr<SDockTab> SubTab = SubTabManager->FindExistingLiveTab(TabId);
			if (SubTab.IsValid())
			{
				// force close sub tabs
				SubTab->SetCanCloseTab(
					SDockTab::FCanCloseTab::CreateLambda([]()
					{
						return true;
					})
				);
				SubTab->RequestCloseTab();
			}
			else
			{
				break;
			}
		}
	}
}


TSharedRef<SDockTab> SOptimusShaderTextDocumentTab::OnSpawnSubTab(const FSpawnTabArgs& Args, FName SubTabID)
{
	const bool bIsDeclarations = SubTabID == DeclarationsTabId;
	
	const FText SubTabTitle =
		bIsDeclarations ?
			LOCTEXT("OptimusShaderTextDocumentTab_Declarations_Title", "Declarations (Read-Only)") : 
			LOCTEXT("OptimusShaderTextDocumentTab_ShaderText_Title", "Shader Text");

	
	const TSharedPtr<SDockTab> HostTab =
		SNew(SDockTab)
		.Label(SubTabTitle)
		// keep users from closing the tab since we have not
		// offered an easy way to reopen the tab
		// Owner of sub tab will override CanCloseTab to return true
		// when the owner wants to close the sub tabs
		.OnCanCloseTab_Lambda([](){return false;});

	if (bIsDeclarations)
	{
		DeclarationsSubTab =
			SNew(SOptimusShaderTextDocumentSubTab, HostTab)
			.TabTitle(SubTabTitle)
			.Text(this, &SOptimusShaderTextDocumentTab::GetDeclarationsAsText)
			.IsReadOnly(true)
			.Marshaller(SyntaxHighlighterDeclarations);
	}
	else
	{
		ShaderTextSubTab =
			SNew(SOptimusShaderTextDocumentSubTab, HostTab)
			.TabTitle(SubTabTitle)
			.Text(this, &SOptimusShaderTextDocumentTab::GetShaderTextAsText)
			.IsReadOnly(false)
			.Marshaller(SyntaxHighlighterShaderText)
			.OnTextChanged(this, &SOptimusShaderTextDocumentTab::OnShaderTextChanged);
	}

	const TSharedPtr<SOptimusShaderTextDocumentSubTab>& SubTabToSpawn =
		bIsDeclarations ? DeclarationsSubTab : ShaderTextSubTab;

	HostTab->SetContent(SubTabToSpawn.ToSharedRef());
	
	return HostTab.ToSharedRef();
}

bool SOptimusShaderTextDocumentTab::HasValidShaderTextProvider() const
{
	return (GetProviderInterface() != nullptr);
}

IOptimusShaderTextProvider* SOptimusShaderTextDocumentTab::GetProviderInterface() const
{
	return Cast<IOptimusShaderTextProvider>(ShaderTextProviderObject);
}

FText SOptimusShaderTextDocumentTab::GetDeclarationsAsText() const
{
	if (HasValidShaderTextProvider())
	{
		return FText::FromString(GetProviderInterface()->GetDeclarations());
	}
	return FText::GetEmpty();
}

FText SOptimusShaderTextDocumentTab::GetShaderTextAsText() const
{
	if (HasValidShaderTextProvider())
	{
		return FText::FromString(GetProviderInterface()->GetShaderText());
	}
	return FText::GetEmpty();
}

void SOptimusShaderTextDocumentTab::OnShaderTextChanged(const FText& InText) const
{
	if (HasValidShaderTextProvider())
	{
		GetProviderInterface()->SetShaderText(InText.ToString());
	}
}

void SOptimusShaderTextDocumentTab::OnDiagnosticsUpdated() const
{
	SyntaxHighlighterShaderText->SetCompilerMessages(GetProviderInterface()->GetCompilationDiagnostics());
	ShaderTextSubTab->Refresh();
}

#undef LOCTEXT_NAMESPACE
