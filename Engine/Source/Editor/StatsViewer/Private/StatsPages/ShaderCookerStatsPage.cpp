// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "StatsPages/ShaderCookerStatsPage.h"
#include "Serialization/Csv/CsvParser.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SComboButton.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "RHIDefinitions.h"
#include "RHIShaderFormatDefinitions.inl"
#include "ShaderCookerStatsPage.h"
#include "CoreGlobals.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"

#define LOCTEXT_NAMESPACE "Editor.StatsViewer.ShaderCookerStats"

FShaderCookerStatsPage& FShaderCookerStatsPage::Get()
{
	static FShaderCookerStatsPage* Instance = NULL;
	if( Instance == NULL )
	{
		Instance = new FShaderCookerStatsPage;
	}
	return *Instance;
}

class FShaderCookerStats
{
public:
	class FShaderCookerStatsSet
	{
	public:
		FString Name;
		TArray<UShaderCookerStats*> Stats;
		bool bInitialized;

	};

	/** Singleton accessor */
	static FShaderCookerStats& Get();
	FShaderCookerStats();
	~FShaderCookerStats();

	TArray<FString> GetStatSetNames()
	{
		TArray<FString> Temp;
		for(FShaderCookerStatsSet& Set : StatSets)
		{
			Temp.Emplace(Set.Name);
		}
		return Temp;
	}

	FString GetStatSetName(int32 Index)
	{
		return StatSets[Index].Name;
	}

	const TArray<UShaderCookerStats*>& GetShaderCookerStats(uint32 Index)
	{
		FShaderCookerStatsSet Set = StatSets[Index];
		if(!Set.bInitialized)
		{
			Initialize(Index);
		}
		return StatSets[Index].Stats;
	}
	uint32 NumSets()
	{
		return StatSets.Num();
	}

	TArray<FShaderCookerStatsSet> StatSets;

	void Initialize(uint32 Index);
};

void FShaderCookerStats::Initialize(uint32 Index)
{
	TArray<FString> PlatformNames;
	for (int32 Platform = 0; Platform < SP_NumPlatforms; ++Platform)
	{
		if(!IsDeprecatedShaderPlatform((EShaderPlatform)Platform))
		{
			FString FormatName = ShaderPlatformToShaderFormatName((EShaderPlatform)Platform).ToString();
			if (FormatName.StartsWith(TEXT("SF_")))
			{
				FormatName.MidInline(3, MAX_int32, false);
			}
			PlatformNames.Add(MoveTemp(FormatName));
		}
	}
	FShaderCookerStatsSet& Set = StatSets[Index];
	FString CSVData;
	if (FFileHelper::LoadFileToString(CSVData, *Set.Name))
	{
		FCsvParser CsvParser(CSVData);
		const FCsvParser::FRows& Rows = CsvParser.GetRows();
		int32 RowIndex = 0;
		int32 IndexPath = -1;
		int32 IndexPlatform = -1;
		int32 IndexCompiled = -1;
		int32 IndexCooked = -1;
		int32 IndexPermutations = -1;
		for (const TArray<const TCHAR*>& Row : Rows)
		{
			if (RowIndex == 0)
			{
				for (int32 Column = 0; Column < Row.Num(); ++Column)
				{
					FString Key = Row[Column];
					if (Key == TEXT("Path"))
					{
						IndexPath = Column;
					}
					else if (Key == TEXT("Name"))
					{
						if (IndexPath == -1)
						{
							IndexPath = Column;
						}
					}
					else if (Key == TEXT("Platform"))
					{
						IndexPlatform = Column;
					}
					else if (Key == TEXT("Compiled"))
					{
						IndexCompiled = Column;
					}
					else if (Key == TEXT("Cooked"))
					{
						IndexCooked = Column;
					}
					else if (Key == TEXT("Permutations"))
					{
						IndexPermutations = Column;
					}
				}
			}
			else
			{
				FString Path = IndexPath >= 0 && IndexPath < Row.Num() ? Row[IndexPath] : TEXT("?");
#define GET_INT(Index) (Index >= 0 && Index < Row.Num() ? FCString::Atoi(Row[Index]) : 424242)
				int32 Platform = GET_INT(IndexPlatform);
				int32 Compiled = GET_INT(IndexCompiled);
				int32 Cooked = GET_INT(IndexCooked);
				int32 Permutations = GET_INT(IndexPermutations);
#undef GET_INT


				UShaderCookerStats* Stat = NewObject<UShaderCookerStats>();

				int32 LastSlash = -1;
				int32 LastDot = -1;
				FString Name = Path;
				if (Path.FindLastChar('/', LastSlash) && Path.FindLastChar('.', LastDot))
				{
					Name = Path.Mid(LastSlash + 1, LastDot - LastSlash - 1);
				}
				Stat->Name = Name;
				Stat->Path = Path;
				Stat->Platform = Platform < PlatformNames.Num() ? PlatformNames[Platform] : TEXT("unknown");
				Stat->Compiled = Compiled;
				Stat->Cooked = Cooked;
				Stat->Permutations = Permutations;
				Set.Stats.Emplace(Stat);
			}
			RowIndex++;
		}
		//Set.Stats.Emplace(NewStats);
	}
	Set.bInitialized = true;
}


FShaderCookerStats& FShaderCookerStats::Get()
{
	static FShaderCookerStats* Instance = NULL;
	if (Instance == NULL)
	{
		Instance = new FShaderCookerStats;
	}
	return *Instance;
}

FShaderCookerStats::FShaderCookerStats()
{
	TArray<FString> Files;
	FString BasePath = FString::Printf(TEXT("%s/MaterialStats/"), *FPaths::ProjectSavedDir());
	FPlatformFileManager::Get().GetPlatformFile().FindFiles(Files, *BasePath, TEXT("csv"));

	FString MirrorLocation;
	GConfig->GetString(TEXT("/Script/Engine.ShaderCompilerStats"), TEXT("MaterialStatsLocation"), MirrorLocation, GGameIni);
	FParse::Value(FCommandLine::Get(), TEXT("MaterialStatsMirror="), MirrorLocation);
	if (!MirrorLocation.IsEmpty())
	{
		TArray<FString> RemoteFiles;
		FString RemotePath = FPaths::Combine(*MirrorLocation, FApp::GetProjectName(), *FApp::GetBranchName());
		FPlatformFileManager::Get().GetPlatformFile().FindFiles(RemoteFiles, *RemotePath, TEXT("csv"));
		Files.Append(RemoteFiles);
	}

	for (FString Filename : Files)
	{
		FShaderCookerStatsSet Set;
		Set.Name = Filename;
		Set.bInitialized = false;
		StatSets.Emplace(Set);
	}

}
FShaderCookerStats::~FShaderCookerStats()
{

}

TSharedPtr<SWidget> FShaderCookerStatsPage::GetCustomWidget(TWeakPtr<IStatsViewer> InParentStatsViewer)
{
	if (!CustomWidget.IsValid())
	{
		SAssignNew(CustomWidget, SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f)
			.HAlign(HAlign_Fill)
			[
				SAssignNew(PlatformComboButton, SComboButton)
				.ContentPadding(3)
				.OnGetMenuContent(this, &FShaderCookerStatsPage::OnGetPlatformButtonMenuContent, InParentStatsViewer)
				.ButtonContent()
				[
					SNew(STextBlock)
					.Text(this, &FShaderCookerStatsPage::OnGetPlatformMenuLabel)
					.ToolTipText(LOCTEXT("Platform_ToolTip", "Platform"))
				]
			];
	}

	return CustomWidget;
}

TSharedRef<SWidget> FShaderCookerStatsPage::OnGetPlatformButtonMenuContent(TWeakPtr<IStatsViewer> InParentStatsViewer) const
{
	FMenuBuilder MenuBuilder(true, NULL);
	FShaderCookerStats& Stats = FShaderCookerStats::Get();
	for (int32 Index = 0; Index < (int32)Stats.NumSets(); ++Index)
	{
		FString Name = Stats.GetStatSetName(Index);
		FText MenuText = FText::FromString(Name);

		MenuBuilder.AddMenuEntry(
			MenuText,
			MenuText,
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(const_cast<FShaderCookerStatsPage*>(this), &FShaderCookerStatsPage::OnPlatformClicked, InParentStatsViewer, Index),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &FShaderCookerStatsPage::IsPlatformSetSelected, Index)
			),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);
	}
	return MenuBuilder.MakeWidget();
}

void FShaderCookerStatsPage::OnPlatformClicked(TWeakPtr<IStatsViewer> InParentStatsViewer, int32 Index)
{
	bool bChanged = SelectedPlatform != Index;
	SelectedPlatform = Index;
	if(bChanged)
	{
		InParentStatsViewer.Pin()->Refresh();
	}
}

bool FShaderCookerStatsPage::IsPlatformSetSelected(int32 Index) const
{
	return SelectedPlatform == Index;
}

FText FShaderCookerStatsPage::OnGetPlatformMenuLabel() const
{
	FString ActiveSetName = FShaderCookerStats::Get().GetStatSetName(SelectedPlatform);
	FText Text = FText::FromString(ActiveSetName);
	return Text;
}

void FShaderCookerStatsPage::Generate( TArray< TWeakObjectPtr<UObject> >& OutObjects ) const
{
	FShaderCookerStats& Stats = FShaderCookerStats::Get();
	const TArray<UShaderCookerStats*>& CookStats = Stats.GetShaderCookerStats(SelectedPlatform);
	for(UShaderCookerStats* Stat: CookStats)
	{
		OutObjects.Add(Stat);
	}
}

void FShaderCookerStatsPage::GenerateTotals( const TArray< TWeakObjectPtr<UObject> >& InObjects, TMap<FString, FText>& OutTotals ) const
{
	if(InObjects.Num())
	{
		UShaderCookerStats* TotalEntry = NewObject<UShaderCookerStats>();

		for( auto It = InObjects.CreateConstIterator(); It; ++It )
		{
			UShaderCookerStats* StatsEntry = Cast<UShaderCookerStats>( It->Get() );
			TotalEntry->Compiled += StatsEntry->Compiled;
			TotalEntry->Cooked += StatsEntry->Cooked;
			TotalEntry->Permutations += StatsEntry->Permutations;
		}

		OutTotals.Add( TEXT("Compiled"), FText::AsNumber( TotalEntry->Compiled) );
		OutTotals.Add( TEXT("Cooked"), FText::AsNumber( TotalEntry->Cooked) );
		OutTotals.Add( TEXT("Permutations"), FText::AsNumber( TotalEntry->Permutations) );
	}
}
void FShaderCookerStatsPage::OnShow( TWeakPtr< IStatsViewer > InParentStatsViewer )
{
}

void FShaderCookerStatsPage::OnHide()
{
}

#undef LOCTEXT_NAMESPACE
