// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RewindDebuggerInterface/Public/IRewindDebuggerExtension.h"
#include "RewindDebuggerInterface/Public/IRewindDebuggerView.h"
#include "RewindDebuggerInterface/Public/IRewindDebuggerViewCreator.h"
#include "Widgets/Views/SHeaderRow.h"
#include "PoseSearch/PoseSearch.h"
#include "PoseSearchDebugger.generated.h"

namespace TraceServices { class IAnalysisSession; }
enum class EPoseSearchFeatureDomain;
class UPoseSearchDatabase;
class IUnrealInsightsModule;
class ITableRow;
class IDetailsView;
class STableViewBase;
class SVerticalBox;
class SHorizontalBox;
class SScrollBox;
class SWidgetSwitcher;
class SSearchBox;
template <typename ItemType> class SListView;

namespace UE::PoseSearch
{
	namespace DebuggerDatabaseColumns { struct IColumn; }
	struct FDebugDrawParams;
	struct FTraceMotionMatchingStateMessage;
	class FFeatureVectorReader;
	class FDebuggerDatabaseRowData;
} // namespace UE::PoseSearch


/**
 * Used by the reflection UObject to encompass a set of feature vectors
 */
USTRUCT()
struct FPoseSearchDebuggerPoseVectorChannel
{
	GENERATED_BODY()

	// @TODO: Should be ideally enumerated based on all possible schema features

	UPROPERTY(VisibleDefaultsOnly, Category = "Channel")
	bool bShowPositions = false;
	
	UPROPERTY(VisibleDefaultsOnly, Category = "Channel")
	bool bShowLinearVelocities = false;

	UPROPERTY(VisibleDefaultsOnly, Category = "Channel")
	bool bShowFacingDirections = false;

    UPROPERTY(VisibleAnywhere, EditFixedSize, Category="Channel", meta=(EditCondition="bShowPositions", EditConditionHides))
	TArray<FVector> Positions;

    UPROPERTY(VisibleAnywhere, EditFixedSize, Category="Channel", meta=(EditCondition="bShowLinearVelocities", EditConditionHides))
	TArray<FVector> LinearVelocities;
	
    UPROPERTY(VisibleAnywhere, EditFixedSize, Category="Channel", meta=(EditCondition="bShowFacingDirections", EditConditionHides))
	TArray<FVector> FacingDirections;

	void Reset();
	bool IsEmpty() const { return Positions.IsEmpty() && LinearVelocities.IsEmpty() && FacingDirections.IsEmpty(); }
};

USTRUCT()
struct FPoseSearchDebuggerPoseVector
{
	GENERATED_BODY()


	UPROPERTY(VisibleDefaultsOnly, Category = "Pose Vector")
	bool bShowPose = false;

	UPROPERTY(VisibleDefaultsOnly, Category = "Pose Vector")
	bool bShowTrajectoryTimeBased = false;

	UPROPERTY(VisibleDefaultsOnly, Category = "Pose Vector")
	bool bShowTrajectoryDistanceBased = false;

	UPROPERTY(VisibleAnywhere, Category="Pose Vector", meta=(EditCondition="bShowPose", EditConditionHides))
	FPoseSearchDebuggerPoseVectorChannel Pose;

	UPROPERTY(VisibleAnywhere, Category="Pose Vector", meta=(EditCondition="bShowTrajectoryTimeBased", EditConditionHides, DisplayName="Trajectory (Time)"))
	FPoseSearchDebuggerPoseVectorChannel TrajectoryTimeBased;

	UPROPERTY(VisibleAnywhere, Category="Pose Vector", meta=(EditCondition="bShowTrajectoryDistanceBased", EditConditionHides, DisplayName="Trajectory (Distance)"))
	FPoseSearchDebuggerPoseVectorChannel TrajectoryDistanceBased;

	void Reset();
	void ExtractFeatures(const UE::PoseSearch::FFeatureVectorReader& Reader);
	bool IsEmpty() const { return Pose.IsEmpty() && TrajectoryTimeBased.IsEmpty() && TrajectoryDistanceBased.IsEmpty(); }
};

/**
 * Used by the reflection UObject to encompass draw options for the query and database selections
 */
USTRUCT()
struct FPoseSearchDebuggerFeatureDrawOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Draw Options")
    bool bDisable = false;

	UPROPERTY(EditAnywhere, Category="Draw Options", Meta=(EditCondition="!bDisable"))
	bool bDrawPoseFeatures = true;

	UPROPERTY(EditAnywhere, Category="Draw Options", Meta=(EditCondition="!bDisable"))
    bool bDrawTrajectoryFeatures = true;

	UPROPERTY(EditAnywhere, Category = "Draw Options", Meta = (EditCondition = "!bDisable"))
	bool bDrawSampleLabels = true;

	UPROPERTY(EditAnywhere, Category = "Draw Options", Meta = (EditCondition = "!bDisable"))
	bool bDrawSamplesWithColorGradient = true;
};

/**
 * Reflection UObject being observed in the details view panel of the debugger
 */
UCLASS()
class POSESEARCHEDITOR_API UPoseSearchDebuggerReflection : public UObject
{
	GENERATED_BODY()

public:

	UPROPERTY(VisibleAnywhere, Category="Motion Matching State", Meta=(DisplayName="Current Database"))
	FString CurrentDatabaseName = "";

	/** Time since last PoseSearch jump */
	UPROPERTY(VisibleAnywhere, Category="Motion Matching State")
	float ElapsedPoseJumpTime = 0.0f;

	/** Whether it is playing the loop following the expended animation runway */
	UPROPERTY(VisibleAnywhere, Category="Motion Matching State")
	bool bFollowUpAnimation = false;

	UPROPERTY(EditAnywhere, Category="Draw Options", Meta=(DisplayName="Query"))
	FPoseSearchDebuggerFeatureDrawOptions QueryDrawOptions;

	UPROPERTY(EditAnywhere, Category="Draw Options", Meta=(DisplayName="Selected Pose"))
	FPoseSearchDebuggerFeatureDrawOptions SelectedPoseDrawOptions;

    UPROPERTY(VisibleAnywhere, Category="Pose Vectors")
	FPoseSearchDebuggerPoseVector QueryPoseVector;
    	
    UPROPERTY(VisibleAnywhere, Category="Pose Vectors")
	FPoseSearchDebuggerPoseVector ActivePoseVector;

	UPROPERTY(VisibleAnywhere, Category="Pose Vectors")
	FPoseSearchDebuggerPoseVector SelectedPoseVector;

	UPROPERTY(VisibleAnywhere, Category="Pose Vectors")
	FPoseSearchDebuggerPoseVector CostVector;
};


namespace UE::PoseSearch {

DECLARE_DELEGATE(FOnPoseSelectionChanged)

/**
 * Database panel view widget of the PoseSearch debugger
 */

class SDebuggerDatabaseView : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SDebuggerDatabaseView) {}
		SLATE_ARGUMENT(TWeakPtr<class SDebuggerView>, Parent)
		SLATE_EVENT(FOnPoseSelectionChanged, OnPoseSelectionChanged)
	SLATE_END_ARGS()
	
	void Construct(const FArguments& InArgs);
	void Update(const FTraceMotionMatchingStateMessage& State, const UPoseSearchDatabase& Database);

	const TSharedPtr<SListView<TSharedRef<FDebuggerDatabaseRowData>>>& GetActiveRow() const { return ActiveView.ListView; }
	const TSharedPtr<SListView<TSharedRef<FDebuggerDatabaseRowData>>>& GetDatabaseRows() const { return FilteredDatabaseView.ListView; }

	/** Used by database rows to acquire column-specific information */
	using FColumnMap = TMap<FName, TSharedRef<DebuggerDatabaseColumns::IColumn>>;

private:
	/** Deletes existing columns and initializes a new set */
	void RefreshColumns();

	/** Adds a column to the existing list */
	void AddColumn(TSharedRef<DebuggerDatabaseColumns::IColumn>&& Column);

	/** Retrieves current column map, used as an attribute by rows */
	const FColumnMap* GetColumnMap() const { return &Columns; }

	/** Creates widgets for every pose in the database, initializing the static data in the process */
	void CreateRows(const UPoseSearchDatabase& Database);

	/** Sorts the database by the current sort predicate, updating the view order */
	void SortDatabaseRows();

	void FilterDatabaseRows();

	/** Sets dynamic data for each row, such as score at the current time */
	void UpdateRows(const FTraceMotionMatchingStateMessage& State, const UPoseSearchDatabase& Database);

	/** Acquires sort predicate for the given column */
	EColumnSortMode::Type GetColumnSortMode(const FName ColumnId) const;

	/** Gets active column width, used to align active and database view */
	float GetColumnWidth(const FName ColumnId) const;
	
	/** Updates the active sort predicate, setting the sorting order of all other columns to none
	 * (to be dependent on active column */
	void OnColumnSortModeChanged(const EColumnSortPriority::Type SortPriority, const FName & ColumnId, const EColumnSortMode::Type InSortMode);

	/** Aligns the active and database views */
	void OnColumnWidthChanged(const float NewWidth, FName ColumnId) const;

	/** Called when the text in the filter box is modified to update the filtering */
	void OnFilterTextChanged(const FText& SearchText);

	void OnDatabaseRowSelectionChanged(TSharedPtr<FDebuggerDatabaseRowData> Row, ESelectInfo::Type SelectInfo);

	/** Generates a database row widget for the given data */
	TSharedRef<ITableRow> HandleGenerateDatabaseRow(TSharedRef<FDebuggerDatabaseRowData> Item, const TSharedRef<STableViewBase>& OwnerTable) const;
	
	/** Generates the active row widget for the given data */
	TSharedRef<ITableRow> HandleGenerateActiveRow(TSharedRef<FDebuggerDatabaseRowData> Item, const TSharedRef<STableViewBase>& OwnerTable) const;

	TWeakPtr<SDebuggerView> ParentDebuggerViewPtr;

	FOnPoseSelectionChanged OnPoseSelectionChanged;

	/** Current column to sort by */
	FName SortColumn = "";

	/** Current sorting mode */
	EColumnSortMode::Type SortMode = EColumnSortMode::Ascending;
	
	/** Column data container, used to emplace defined column structures of various types */
    FColumnMap Columns;

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

	/** All database poses */
	TArray<TSharedRef<FDebuggerDatabaseRowData>> UnfilteredDatabaseRows;

	/** Database listing for filtered poses */
	FTable FilteredDatabaseView;

	/** Search box widget */
	TSharedPtr<SSearchBox> FilterBox;

	/** Text used to filter DatabaseView */
	FText FilterText;
};

/**
 * Details panel view widget of the PoseSearch debugger
 */
class SDebuggerDetailsView : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SDebuggerDetailsView) {}
		SLATE_ARGUMENT(TWeakPtr<class SDebuggerView>, Parent)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void Update(const FTraceMotionMatchingStateMessage& State, const UPoseSearchDatabase& Database) const;

	/** Get a const version of our reflection object */
	const TObjectPtr<UPoseSearchDebuggerReflection>& GetReflection() const { return Reflection; }
	
	virtual ~SDebuggerDetailsView() override;

private:
	/** Update our details view object with new state information */
	void UpdateReflection(const FTraceMotionMatchingStateMessage& State, const UPoseSearchDatabase& Database) const;
	
	TWeakPtr<SDebuggerView> ParentDebuggerViewPtr;

	/** Details widget constructed for the MM node */
	TSharedPtr<IDetailsView> Details;

	/** Last updated reflection data relative to MM state */
	TObjectPtr<UPoseSearchDebuggerReflection> Reflection = nullptr;
};

/** Callback to update the debugger when node is actively selected */
DECLARE_DELEGATE_OneParam(FOnUpdateSelection, int32 NodeId);

/**
 * Callback to update the debugger when view update occurs, updating the motion matching state
 * relative to the selected anim instance.
 */
DECLARE_DELEGATE_OneParam(FOnUpdate, uint64 AnimInstanceId);

/** Callback to relay closing of the view to destroy the debugger instance */
DECLARE_DELEGATE_OneParam(FOnViewClosed, uint64 AnimInstanceId);

/**
 * Entire view of the PoseSearch debugger, containing all sub-widgets
 */
class SDebuggerView : public IRewindDebuggerView
{
public:
	SDebuggerView() = default;

	SLATE_BEGIN_ARGS(SDebuggerView){}
		SLATE_ATTRIBUTE(const FTraceMotionMatchingStateMessage*, MotionMatchingState)
		SLATE_ATTRIBUTE(const UPoseSearchDatabase*, PoseSearchDatabase)
		SLATE_ATTRIBUTE(const TArray<int32>*, MotionMatchingNodeIds)
		SLATE_ATTRIBUTE(bool, IsPIESimulating)
		SLATE_ATTRIBUTE(bool, IsRecording)
		SLATE_ATTRIBUTE(double, RecordingDuration)
		SLATE_ATTRIBUTE(int32, NodesNum)
		SLATE_ATTRIBUTE(const UWorld*, World)
		SLATE_ATTRIBUTE(const FTransform*, RootTransform)
		SLATE_EVENT(FOnUpdateSelection, OnUpdateSelection)
		SLATE_EVENT(FOnUpdate, OnUpdate)
		SLATE_EVENT(FOnViewClosed, OnViewClosed)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, uint64 InAnimInstanceId);
	virtual void SetTimeMarker(double InTimeMarker) override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual FName GetName() const override;
	virtual uint64 GetObjectId() const override;

	TArray<TSharedRef<FDebuggerDatabaseRowData>> GetSelectedDatabaseRows() const;

	virtual ~SDebuggerView() override;

private:
	/** Called each frame to draw features of the query vector & database selections */
	void DrawFeatures(const UWorld& DebuggerWorld, const FTraceMotionMatchingStateMessage& State, const UPoseSearchDatabase& Database, const FTransform& Transform) const;
	
	/** Check if a node selection was made, true if a node is selected */
	bool UpdateSelection();

	/** Update the database and details views */
	void UpdateViews() const;

	void DrawVisualization() const;
	
	/** Returns an int32 appropriate to the index of our widget selector */
	int32 SelectView() const;

	/** Callback when a button in the selection view is clicked */
	FReply OnUpdateNodeSelection(int32 InSelectedNodeId);

	void OnPoseSelectionChanged();

	/** Generates the message view relaying that there is no data */
	TSharedRef<SWidget> GenerateNoDataMessageView();

	/** Generates the return button to go back to the selection mode */
	TSharedRef<SWidget> GenerateReturnButtonView();

	/** Generates the entire node debugger widget, including database and details view */
	TSharedRef<SWidget> GenerateNodeDebuggerView();

	/** Gets all MM nodes being traced in this frame */
	TAttribute<const TArray<int32>*> MotionMatchingNodeIds;
	
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
	TAttribute<int32> NodesNum;
	
	/** Current world the Rewind Debugger is functioning in */
	TAttribute<const UWorld*> World;
	
	/** Current Component to World Space transform of the Skeletal Mesh Component */
	TAttribute<const FTransform*> RootTransform;
	
	/** Update current debugger data when node selection is changed */
	FOnUpdateSelection OnUpdateSelection;
	
	/** Update current debugger data when update occurs */
	FOnUpdate OnUpdate;
	
	/** Destroy the debugger instanced when closed */
	FOnViewClosed OnViewClosed;

	/** Active node being debugged */
	int32 SelectedNodeId = -1;

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

	/** Previous position of the time marker */
	double PreviousTimeMarker = -1.0;

	/** Tracks if the current time has been updated yet (delayed) */
	bool bUpdated = false;

	/** Tracks number of consecutive frames, once it reaches threshold it will update the view */
	int32 CurrentConsecutiveFrames = 0;

	/** Once the frame count has reached this value, an update will trigger for the view */
	static constexpr int32 ConsecutiveFramesUpdateThreshold = 10;
};

class FDebuggerInstance : public TSharedFromThis<FDebuggerInstance>
{
public:
	explicit FDebuggerInstance(uint64 InAnimInstanceId);

private:
	// Used for view callbacks
    const FTraceMotionMatchingStateMessage* GetMotionMatchingState() const;
    const UPoseSearchDatabase* GetPoseSearchDatabase() const;
	const TArray<int32>* GetNodeIds() const;
	int32 GetNodesNum() const;
	const FTransform* GetRootTransform() const;

	/** Update motion matching states for frame */
	void OnUpdate(uint64 InAnimInstanceId);
	
	/** Updates active motion matching state based on node selection */
	void OnUpdateSelection(int32 InNodeId);

	/** Update the list of states for this frame */	
	void UpdateMotionMatchingStates(uint64 InAnimInstanceId);
	
	/** List of all Node IDs associated with motion matching states */
	TArray<int32> NodeIds;
	
	/** List of all updated motion matching states per node */
	TArray<const FTraceMotionMatchingStateMessage*> MotionMatchingStates;
	
	/** Currently active motion matching state based on node selection in the view */
	const FTraceMotionMatchingStateMessage* ActiveMotionMatchingState = nullptr;

	/** Stored Rewind Debugger instance provided by the extension */
	const IRewindDebugger* RewindDebugger = nullptr;

	/** Anim Instance associated with this debugger instance */
	uint64 AnimInstanceId = 0;

	/** Limits some public API */
	friend class FDebugger;
};


/**
 * PoseSearch debugger, containing the data to be acquired and relayed to the view
 */
class FDebugger : public TSharedFromThis<FDebugger>, public IRewindDebuggerExtension
{
public:
	virtual void Update(float DeltaTime, IRewindDebugger* InRewindDebugger) override;
	virtual ~FDebugger() = default;

	static FDebugger* Get() { return Debugger; }
	static void Initialize();
	static void Shutdown();
	static const FName ModularFeatureName;

	/** Generates the slate debugger view widget */
	TSharedPtr<SDebuggerView> GenerateInstance(uint64 InAnimInstanceId);
private:
	bool IsPIESimulating() const;
	bool IsRecording() const;
	double GetRecordingDuration() const;
	const UWorld* GetWorld() const;
	void OnViewClosed(uint64 InAnimInstanceId);

	/** Last stored Rewind Debugger */
	const IRewindDebugger* RewindDebugger = nullptr;

	/** List of all active debugger instances */
	TArray<TSharedRef<FDebuggerInstance>> DebuggerInstances;
	
	static FDebugger* Debugger;
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
	
	/** Creates the PoseSearch Slate view for the provided AnimInstance */
	virtual TSharedPtr<IRewindDebuggerView> CreateDebugView(uint64 ObjectId, double CurrentTime, const TraceServices::IAnalysisSession& InAnalysisSession) const override;
};


} // namespace UE::PoseSearch
