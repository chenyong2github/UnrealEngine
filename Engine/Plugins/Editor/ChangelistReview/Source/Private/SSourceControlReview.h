// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "ISourceControlProvider.h"
#include "SourceControlOperations.h"
#include "Widgets/Views/SHeaderRow.h"

template <typename OptionType> class SComboBox;

class SEditableText;
class STableViewBase;
class STextBlock;
template <typename ItemType> class SListView;

class SSourceControlReviewEntry;
class SChangelistEditableTextBox;
class SProgressBar;

namespace SourceControlReview
{
	enum class ESourceControlAction : uint8
	{
		Add,
		Edit,
		Delete,
		Branch,
		Integrate,
		Unset,

		// keep this last
		ActionCount
	};

	enum class EChangelistState : uint8
	{
		Submitted,
		Pending
	};

	struct FChangelistFileData
	{
		FChangelistFileData()
		{
		}

		FChangelistFileData(const FString& InAssetName, const FString& InReviewFilePkgName,
							const FString& InReviewFileRevisionNum, const FString& InPreviousFilePkgName,
							const FString& InPreviousFileRevisionNum)
			: AssetName(InAssetName)
			, ReviewFileName(InReviewFilePkgName)
			, ReviewFileRevisionNum(InReviewFileRevisionNum)
			, PreviousAssetName() // assume no rename by default
			, PreviousFileName(InPreviousFilePkgName)
			, PreviousFileRevisionNum(InPreviousFileRevisionNum)
		{
		}

		bool IsDataValidForEntry() const
		{
			return FileSourceControlAction != ESourceControlAction::Unset && !ReviewFileName.IsEmpty() &&
				(!PreviousFileName.IsEmpty() || FileSourceControlAction == ESourceControlAction::Add || FileSourceControlAction == ESourceControlAction::Branch);
		}

		FString AssetName;

		FString ReviewFileName;

		FString ReviewFileRevisionNum;

		FDateTime ReviewFileDateTime;

		FString PreviousAssetName;

		FString PreviousFileName;

		FString PreviousFileRevisionNum;

		FString RelativeFilePath;

		FString AssetFilePath;

		int32 ChangelistNum = INDEX_NONE;

		EChangelistState ChangelistState = EChangelistState::Submitted;

		ESourceControlAction FileSourceControlAction = ESourceControlAction::Unset;

		const UClass* GetIconClass();
		
	private:
		TOptional<const UClass*> CachedIconClass;
	};
	
	namespace ColumnIds
	{
		inline const FName Status = TEXT("Status");
		inline const FName File = TEXT("File");
		inline const FName Tools = TEXT("Tools");
	}

	struct FChangelistLightInfo
	{
		explicit FChangelistLightInfo(const FText& Number)
			: Number(Number)
		{}
		FChangelistLightInfo(const FText& Number, const FText& Author, const FText& Description)
			: Number(Number), Author(Author), Description(Description)
		{}
		
		FText Number;
		FText Author;
		FText Description;
	};
}


/**
 * Used to select a changelist and diff it's changes
 */
class SSourceControlReview : public SCompoundWidget
{
public:
	using ESourceControlAction = SourceControlReview::ESourceControlAction;
	using EChangelistState = SourceControlReview::EChangelistState;
	using FChangelistFileData = SourceControlReview::FChangelistFileData;
	
	SLATE_BEGIN_ARGS(SSourceControlReview) {}
	SLATE_END_ARGS()
	
	/** Constructs the widget */
	void Construct(const FArguments& InArgs);

	virtual ~SSourceControlReview() override;

	/**
	 * Pulls up changelist record from source control
	 * @param Changelist Changelist number to get
	 */
	void LoadChangelist(const FString& Changelist);
	
private:
	void OnChangelistLoadComplete(const FSourceControlOperationRef& InOperation, ECommandResult::Type InResult, FString Changelist);
	FReply OnLoadChangelistClicked();
	void OnChangelistNumChanged(const FText& Text);
	void OnChangelistNumCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	TSharedRef<SWidget> MakeCLComboOption(TSharedPtr<SourceControlReview::FChangelistLightInfo>Item) const;
	void OnCLComboSelection(TSharedPtr<SourceControlReview::FChangelistLightInfo> Item, ESelectInfo::Type SelectInfo) const;
	void SaveCLHistory();
	void LoadCLHistory();
	
	/**
	 * Called when file is retrieved from
	 * @param ChangelistFileData carried through worker to be filled with information about retrieved file from source control
	 */
	void OnGetFileFromSourceControl(TSharedPtr<FChangelistFileData> ChangelistFileData);

	/**
	 * Sets bChangelistLoading and then fires the BP_SetLoading event
	 * @param bInLoading when true flips visibility on loading bar and is also blocking from loading multiple changelists at the same time until loading is finished
	 */
	void SetLoading(bool bInLoading);
	
	/**
	 * True if the tool is currently loading a changelist
	 */
	bool IsLoading() const;

	/**
	 * Checks changelist information to make sure it's valid for the diff tool
	 * @param InRecord changelist record
	 */
	bool IsChangelistRecordValid(const TArray<TMap<FString, FString>>& InRecord) const;

	/**
	 * Sets the source control action type for the loaded file
	 * @param ChangelistFileData carried through worker to be filled with information about retrieved file from source control
	 * @param SourceControlAction source action operation type retrieved from source control
	 */
	static void SetFileSourceControlAction(TSharedPtr<FChangelistFileData> ChangelistFileData,
	                                       const FString& SourceControlAction);

	/**
	 * Sets changelist information Author, Description, Status and Depot name that changelist is going to
	 */
	void SetChangelistInfo(const TMap<FString, FString>& InChangelistRecord);

	TSharedRef<ITableRow> OnGenerateFileRow(TSharedPtr<FChangelistFileData> FileData, const TSharedRef<STableViewBase>& Table) const;

	/**
	 * Removes CurrentChangelistInfo.SharedPath from the beginning of FullCLPath
	 */
	FString TrimSharedPath(FString FullCLPath) const;

	static SHeaderRow::FColumn::FArguments HeaderColumn(FName HeaderName);

	/**
	 * Removes the game directory from the beginning of the FullCLPath
	 */
	static FString AsAssetPath(const FString& FullCLPath);

	// used for asynchronous changelist loading
	bool bChangelistLoading = false;
	uint32 FilesToLoad = 0;
	uint32 FilesLoaded = 0;
	TArray<TSharedPtr<FChangelistFileData>> ChangelistFiles;
	TMap<FString, TWeakPtr<FChangelistFileData>> RedirectorsFound;
	TSharedPtr<FGetChangelistDetails> GetChangelistDetailsCommand;
	TArray<TSharedPtr<SourceControlReview::FChangelistLightInfo>> CLHistory;
	bool bUncommittedChangelistNum = false;
	
	TSharedPtr<SComboBox<TSharedPtr<SourceControlReview::FChangelistLightInfo>>> ChangelistNumComboBox;
	TSharedPtr<SEditableText> ChangelistNumText;
	TSharedPtr<STextBlock> EnterChangelistTextBlock;
	TSharedPtr<STextBlock> LoadingTextBlock;
	TSharedPtr<SProgressBar> LoadingProgressBar;
	TSharedPtr<SWidget> ChangelistInfoWidget;
	TSharedPtr<SListView<TSharedPtr<FChangelistFileData>>> ChangelistEntriesWidget;

	// Info about the current chagnelist
	struct FChangelistInfo
	{
		FText Author;
		FText SharedPath;
		FText Status;
		FText Description;
	} CurrentChangelistInfo;
};
