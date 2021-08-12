// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RewindDebuggerInterface/Public/IRewindDebuggerExtension.h"
#include "RewindDebuggerInterface/Public/IRewindDebuggerView.h"
#include "RewindDebuggerInterface/Public/IRewindDebuggerViewCreator.h"
#include "Widgets/Views/SHeaderRow.h"
#include "PoseSearchDebugger.generated.h"

namespace TraceServices { class IAnalysisSession; }
class IUnrealInsightsModule;
class UPoseSearchDatabase;
class ITableRow;
class STableViewBase;
class IDetailsView;
class SVerticalBox;
class SHorizontalBox;
class SScrollBox;
class SWidgetSwitcher;
template <typename ItemType> class SListView;

/**
 * Used by the reflection UObject to encompass a set of features
 */
USTRUCT()
struct FPoseSearchDebuggerFeatureReflection
{
	GENERATED_USTRUCT_BODY()

	// @TODO: Should be ideally enumerated based on all possible schema features
    UPROPERTY(VisibleAnywhere, Category="Query Data")
	TArray<FVector> Positions;
    UPROPERTY(VisibleAnywhere, Category="Query Data")
	TArray<FVector> LinearVelocities;
    UPROPERTY(VisibleAnywhere, Category="Query Data")
	TArray<FVector> AngularVelocities;

	/** Empty contents of above arrays */
	void EmptyAll();
};

/**
 * Reflection UObject being observed in the details view panel of the debugger
 */
UCLASS()
class POSESEARCHEDITOR_API UPoseSearchDebuggerReflection : public UObject
{
	GENERATED_UCLASS_BODY()

	/** Time since last PoseSearch jump */
	UPROPERTY(VisibleAnywhere, Category="Motion Matching State")
	float ElapsedPoseJumpTime = 0.0f;

	/** Whether it is playing the loop following the expended animation runway */
	UPROPERTY(VisibleAnywhere, Category="Motion Matching State")
	bool bFollowUpAnimation = false;

	/** Pose features of the current query vector */
    UPROPERTY(EditAnywhere, Category="Query Data")
	FPoseSearchDebuggerFeatureReflection PoseFeatures;
    	
	/** Time-based trajectory features of the current query vector */
    UPROPERTY(EditAnywhere, Category="Query Data")
	FPoseSearchDebuggerFeatureReflection TimeTrajectoryFeatures;

	/** Distance-based trajectory features of the current query vector */
	UPROPERTY(EditAnywhere, Category="Query Data")
    FPoseSearchDebuggerFeatureReflection DistanceTrajectoryFeatures;
};


namespace UE { namespace PoseSearch {

namespace DebuggerDatabaseColumns { struct IColumn; }
struct FTraceMotionMatchingStateMessage;
class FDebuggerDatabaseRowData;


/**
 * Database panel view widget of the PoseSearch debugger
 */
class SDebuggerDatabaseView : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SDebuggerDatabaseView) {}
	SLATE_END_ARGS()
	
	void Construct(const FArguments& InArgs);
	void Update(const FTraceMotionMatchingStateMessage* State, const UPoseSearchDatabase* Database);

private:
	void RefreshColumns();
	void AddColumn(TSharedRef<DebuggerDatabaseColumns::IColumn>&& Column);
	void CreateRows(const UPoseSearchDatabase* Database);
	void SortDatabaseRows();
	void UpdateRows(const FTraceMotionMatchingStateMessage* State, const UPoseSearchDatabase* Database);
	EColumnSortMode::Type GetColumnSortMode(const FName ColumnId) const;
	float GetColumnWidth(const FName ColumnId) const;
	void OnColumnSortModeChanged(const EColumnSortPriority::Type SortPriority, const FName & ColumnId, const EColumnSortMode::Type InSortMode);
	void OnColumnWidthChanged(const float NewWidth, FName ColumnId) const;
	TSharedRef<ITableRow> HandleGenerateDatabaseRow(TSharedRef<FDebuggerDatabaseRowData> Item, const TSharedRef<STableViewBase>& OwnerTable) const;
	TSharedRef<ITableRow> HandleGenerateActiveRow(TSharedRef<FDebuggerDatabaseRowData> Item, const TSharedRef<STableViewBase>& OwnerTable) const;

	/** Current column to sort by */
	FName SortColumn = "";
	/** Current sorting mode */
	EColumnSortMode::Type SortMode = EColumnSortMode::Ascending;
	
	/** Column data container, used to emplace defined column structures of various types */
    TMap<FName, TSharedRef<DebuggerDatabaseColumns::IColumn>> Columns;

	struct FTable
	{
		/** Header row*/
		TSharedPtr<SHeaderRow> HeaderRow;

		/** Widget for displaying the list of row objects */
		TSharedPtr<SListView<TSharedRef<FDebuggerDatabaseRowData>>> ListView;

		// @TODO: Explore options for active row other than displaying array of 1 element
		/** List of row objects */
		TArray<TSharedRef<FDebuggerDatabaseRowData>> Rows;

		/** Background style for the list view */
		FTableRowStyle RowStyle;

		/** Row color */
		FSlateBrush RowBrush;

		/** Scroll bar for the data table */
		TSharedPtr<SScrollBar> ScrollBar;
	};

	/** Active row at the top of the view */
	FTable ActiveView;
	/** Database listings for all poses */
	FTable DatabaseView;
};

/**
 * Details panel view widget of the PoseSearch debugger
 */
class SDebuggerDetailsView : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SDebuggerDetailsView) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UPoseSearchDebuggerReflection* Reflection);
private:
	/** Details widget constructed for the MM node */
	TSharedPtr<IDetailsView> Details;
};


/**
 * Callback to update the debugger when node selection is changed
 */
DECLARE_DELEGATE_OneParam(FOnSelectionChanged, int32 NodeId);

/**
 * Callback to update the debugger when view update occurs, updating the motion matching state
 * relative to the selected anim instance.
 */
DECLARE_DELEGATE_OneParam(FOnUpdate, uint64 AnimInstanceId);

/**
 * Entire view of the PoseSearch debugger, containing all sub-widgets
 */
class SDebuggerView : public IRewindDebuggerView
{
public:
	SDebuggerView() = default;

	SLATE_BEGIN_ARGS(SDebuggerView){}
		SLATE_ATTRIBUTE(const FTraceMotionMatchingStateMessage*, MotionMatchingState)
		SLATE_ATTRIBUTE(UPoseSearchDebuggerReflection*, Reflection)
		SLATE_ATTRIBUTE(const UPoseSearchDatabase*, PoseSearchDatabase)
		SLATE_ATTRIBUTE(const TArray<int32>*, MotionMatchingNodeIds)
		SLATE_ATTRIBUTE(bool, IsPIESimulating)
		SLATE_ATTRIBUTE(bool, IsRecording)
		SLATE_ATTRIBUTE(double, RecordingDuration)
		SLATE_ATTRIBUTE(int32, ActiveNodesNum)
		SLATE_EVENT(FOnSelectionChanged, OnSelectionChanged)
		SLATE_EVENT(FOnUpdate, OnUpdate)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, uint64 InAnimInstanceId);
	virtual void SetTimeMarker(double InTimeMarker) override;
	virtual FName GetName() const override;
	virtual uint64 GetObjectId() const override;

private:
	void UpdateViews();
	
	/** Returns an int32 appropriate to the index of our widget selector */
	int32 SelectView() const;

	FReply UpdateNodeSelection(int32 InSelectedNode);
	
	TSharedRef<SWidget> GenerateNoDataMessageView();
	TSharedRef<SWidget> GenerateReturnButtonView();
	TSharedRef<SWidget> GenerateNodeDebuggerView();

	/** Gets all MM nodes being traced in this frame */
	TAttribute<const TArray<int32>*> MotionMatchingNodeIds;
	/** Retrieves the reflection UObject from the debugger */
	TAttribute<UPoseSearchDebuggerReflection*> Reflection;
	/** Retrieves the MM state from the debugger */
	TAttribute<const FTraceMotionMatchingStateMessage*> MotionMatchingState;
	/** Retrieves the PoseSearch Database from the debugger */
	TAttribute<const UPoseSearchDatabase*> PoseSearchDatabase;
	/** Whether the game is un-paused and currently simulating */
	TAttribute<bool> IsPIESimulating;
	/** Whether the Rewind Debugger is currently recording gameplay */
	TAttribute<bool> IsRecording;
 	/** Length of the recorded sequence (if any) */
	TAttribute<double> RecordingDuration;
 	/** Number of active nodes at the current frame */
	TAttribute<int32> ActiveNodesNum;
	/** Update current debugger data when node selection is changed */
	FOnSelectionChanged OnSelectionChanged;
	/** Update current debugger data when update occurs */
	FOnUpdate OnUpdate;

	/** Active node being debugged */
	int32 SelectedNode = -1;

	/** List of all nodes being traced (stored for selection) */
	TSet<int32> StoredNodes;

	/** Database view of the motion matching node */
	TSharedPtr<SDebuggerDatabaseView> DatabaseView;
	/** Details panel for introspecting the motion matching node */
	TSharedPtr<SDebuggerDetailsView> DetailsView;
	/** Node debugger view hosts the above two views */
	TSharedPtr<SSplitter> NodeDebuggerView;

	/** Selection view before node is selected */
	TSharedPtr<SVerticalBox> SelectionView;
	
	/** Gray box occluding the debugger view when simulating */
	TSharedPtr<SVerticalBox> SimulatingView;
	
	/** Used to switch between views in the switcher, int32 maps to index in the SWidgetSwitcher */
	enum ESwitcherViewType : int32
	{
		Selection = 0,
		Debugger = 1,
		StoppedMsg = 2,
		RecordingMsg = 3,
		NoDataMsg = 4
	} SwitcherViewType = StoppedMsg;
	
	/** Contains all the above, switches between them depending on context */
	TSharedPtr<SWidgetSwitcher> Switcher;

	/** Contains the switcher, the entire debugger view */
	TSharedPtr<SVerticalBox> DebuggerView;

	/** AnimInstance this view was created for */
	uint64 AnimInstanceId = 0;

	/** Current position of the time marker */
	double TimeMarker = -1.0;

	/** Tracks if the current time has been updated yet (delayed) */
	bool bUpdated = false;

	/** Tracks number of consecutive frames, once it reaches threshold it will update the view */
	int32 CurrentConsecutiveFrames = 0;

	/** Once the frame count has reached this value, an update will trigger for the view */
	static constexpr int32 ConsecutiveFramesUpdateThreshold = 10;
};


/**
 * PoseSearch debugger, containing the data to be acquired and relayed to the view
 */
class FDebugger : public IRewindDebuggerExtension, public TSharedFromThis<FDebugger>
{
public:
	virtual void Update(float DeltaTime, IRewindDebugger* InRewindDebugger) override;
	virtual ~FDebugger() = default;

	static FDebugger* Instance() { return InternalInstance; }
	static void Initialize();
	static void Shutdown();
	static const FName ModularFeatureName;

	/** Generates the slate debugger view widget */
	static TSharedPtr<SDebuggerView> GenerateView(uint64 InAnimInstanceId);

private:
	// Used for view callbacks
	const FTraceMotionMatchingStateMessage* GetMotionMatchingState() const;
	const UPoseSearchDatabase* GetPoseSearchDatabase() const;
	UPoseSearchDebuggerReflection* GetReflection() const;
	bool IsPIESimulating() const;
	const TArray<int32>* GetNodeIds() const;
	bool IsRecording() const;
	double GetRecordingDuration() const;
	int32 GetActiveNodesNum() const;
	void OnUpdate(uint64 InAnimInstanceId);
	void OnSelectionChanged(int32 InNodeId);

	/** Updates the current reflection data relative to the MM state */
	void UpdateReflection() const;
	void UpdateMotionMatchingStates(uint64 InAnimInstanceId);
	
	/** Last stored Rewind Debugger */
	const IRewindDebugger* RewindDebugger = nullptr;
	/** List of all Node IDs associated with motion matching states */
	TArray<int32> NodeIds;
	/** List of all updated motion matching states per node */
	TArray<const FTraceMotionMatchingStateMessage*> MotionMatchingStates;
	/** Last stored MM state (updated from OnSelectionChanged) */
	const FTraceMotionMatchingStateMessage* ActiveMotionMatchingState = nullptr;
	/** Last updated reflection data relative to MM state */
	TObjectPtr<UPoseSearchDebuggerReflection> Reflection = nullptr;

	static FDebugger* InternalInstance;
};

/**
 * Creates the slate widgets associated with the PoseSearch debugger
 * when prompted by the Rewind Debugger
 */
class FDebuggerViewCreator : public IRewindDebuggerViewCreator
{
public:
	virtual ~FDebuggerViewCreator() = default;
	virtual FName GetName() const override;
	virtual FText GetTitle() const override;
	virtual FSlateIcon GetIcon() const override;
	virtual FName GetTargetTypeName() const override;
	virtual TSharedPtr<IRewindDebuggerView> CreateDebugView(uint64 ObjectId, double CurrentTime, const TraceServices::IAnalysisSession& InAnalysisSession) const override;
};


}}
