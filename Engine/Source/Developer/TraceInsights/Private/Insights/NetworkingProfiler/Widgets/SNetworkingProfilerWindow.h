// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Docking/TabManager.h"
#include "Input/Reply.h"
#include "Layout/Visibility.h"
#include "Misc/Guid.h"
#include "SlateFwd.h"
#include "TraceServices/Model/NetProfiler.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/SCompoundWidget.h"

// Insights
#include "Insights/InsightsManager.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

class FActiveTimerHandle;
class FMenuBuilder;
class FUICommandList;

class SPacketView;
class SPacketContentView;
class SNetStatsView;

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FNetworkingProfilerTabs
{
	// Tab identifiers
	static const FName ToolbarID;
	static const FName PacketViewID;
	static const FName PacketContentViewID;
	static const FName NetStatsViewID;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Implements the timing profiler window. */
class SNetworkingProfilerWindow : public SCompoundWidget
{
private:
	struct FGameInstanceItem
	{
		/** Conversion constructor. */
		FGameInstanceItem(const Trace::FNetProfilerGameInstance& InGameInstance)
			: GameInstance(InGameInstance)
		{}

		uint32 GetIndex() const { return GameInstance.GameInstanceIndex; }
		FText GetText() const;
		FText GetTooltipText() const;

		const Trace::FNetProfilerGameInstance GameInstance;
	};

	struct FConnectionItem
	{
		/** Conversion constructor. */
		FConnectionItem(const Trace::FNetProfilerConnection& InConnection)
			: Connection(InConnection)
		{}

		uint32 GetIndex() const { return Connection.ConnectionIndex; }
		FText GetText() const;
		FText GetTooltipText() const;

		const Trace::FNetProfilerConnection Connection;
	};

	struct FConnectionModeItem
	{
		/** Conversion constructor. */
		FConnectionModeItem(const Trace::ENetProfilerConnectionMode& InMode)
			: Mode(InMode)
		{}

		FText GetText() const;
		FText GetTooltipText() const;

		Trace::ENetProfilerConnectionMode Mode;
	};

public:
	/** Default constructor. */
	SNetworkingProfilerWindow();

	/** Virtual destructor. */
	virtual ~SNetworkingProfilerWindow();

	SLATE_BEGIN_ARGS(SNetworkingProfilerWindow) {}
	SLATE_END_ARGS()

	void Reset();

	/** Constructs this widget. */
	void Construct(const FArguments& InArgs, const TSharedRef<SDockTab>& ConstructUnderMajorTab, const TSharedPtr<SWindow>& ConstructUnderWindow);

	/** @return The viewport command list */
	const TSharedPtr<FUICommandList> GetCommandList() const { return CommandList; }

	/** @return The tab manager */
	TSharedPtr<FTabManager> GetTabManager() const { return TabManager; }

	void ShowTab(const FName& TabID);
	void HideTab(const FName& TabID);
	void ShowOrHideTab(const FName& TabID, bool bShow) { bShow ? ShowTab(TabID) : HideTab(TabID); }

	TSharedPtr<SPacketView> GetPacketView() const { return PacketView; }
	const bool IsPacketViewVisible() const { return PacketView.IsValid(); }
	void ShowOrHidePacketView(const bool bVisibleState) { ShowOrHideTab(FNetworkingProfilerTabs::PacketViewID, bVisibleState); }

	TSharedPtr<SPacketContentView> GetPacketContentView() const { return PacketContentView; }
	const bool IsPacketContentViewVisible() const { return PacketContentView.IsValid(); }
	void ShowOrHidePacketContentView(const bool bVisibleState) { ShowOrHideTab(FNetworkingProfilerTabs::PacketContentViewID, bVisibleState); }

	TSharedPtr<SNetStatsView> GetNetStatsView() const { return NetStatsView; }
	const bool IsNetStatsViewVisible() const { return NetStatsView.IsValid(); }
	void ShowOrHideNetStatsView(const bool bVisibleState) { ShowOrHideTab(FNetworkingProfilerTabs::NetStatsViewID, bVisibleState); }

	const Trace::FNetProfilerGameInstance* GetSelectedGameInstance() const { return SelectedGameInstance ? &SelectedGameInstance->GameInstance : nullptr; }
	uint32 GetSelectedGameInstanceIndex() const { return SelectedGameInstance ? SelectedGameInstance->GetIndex() : 0; }
	const Trace::FNetProfilerConnection* GetSelectedConnection() const { return SelectedConnection ? &SelectedConnection->Connection : nullptr; }
	uint32 GetSelectedConnectionIndex() const { return SelectedConnection ? SelectedConnection->GetIndex() : 0; }
	Trace::ENetProfilerConnectionMode GetSelectedConnectionMode() const { return SelectedConnectionMode ? SelectedConnectionMode->Mode : Trace::ENetProfilerConnectionMode::Outgoing; }

	TSharedRef<SWidget> CreateGameInstanceComboBox();
	TSharedRef<SWidget> CreateConnectionComboBox();
	TSharedRef<SWidget> CreateConnectionModeComboBox();

	void SetSelectedPacket(uint32 StartIndex, uint32 EndIndex, uint32 SinglePacketBitSize = 0);
	void SetSelectedBitRange(uint32 StartPos, uint32 EndPos);
	void SetSelectedEventTypeIndex(uint32 InEventTypeIndex);

private:
	TSharedRef<SDockTab> SpawnTab_Toolbar(const FSpawnTabArgs& Args);
	void OnToolbarTabClosed(TSharedRef<SDockTab> TabBeingClosed);

	TSharedRef<SDockTab> SpawnTab_PacketView(const FSpawnTabArgs& Args);
	void OnPacketViewTabClosed(TSharedRef<SDockTab> TabBeingClosed);

	TSharedRef<SDockTab> SpawnTab_PacketContentView(const FSpawnTabArgs& Args);
	void OnPacketContentViewTabClosed(TSharedRef<SDockTab> TabBeingClosed);

	TSharedRef<SDockTab> SpawnTab_NetStatsView(const FSpawnTabArgs& Args);
	void OnNetStatsViewTabClosed(TSharedRef<SDockTab> TabBeingClosed);

	//////////////////////////////////////////////////

	void BindCommands();

	//////////////////////////////////////////////////
	// Toggle Commands

#define DECLARE_TOGGLE_COMMAND(CmdName)\
public:\
	void Map_##CmdName(); /**< Maps UI command info CmdName with the specified UI command list. */\
	const FUIAction CmdName##_Custom(); /**< UI action for CmdName command. */\
private:\
	void CmdName##_Execute(); /**< Handles FExecuteAction for CmdName. */\
	bool CmdName##_CanExecute() const; /**< Handles FCanExecuteAction for CmdName. */\
	ECheckBoxState CmdName##_GetCheckState() const; /**< Handles FGetActionCheckState for CmdName. */

	DECLARE_TOGGLE_COMMAND(TogglePacketViewVisibility)
	DECLARE_TOGGLE_COMMAND(TogglePacketContentViewVisibility)
	DECLARE_TOGGLE_COMMAND(ToggleNetStatsViewVisibility)
#undef DECLARE_TOGGLE_COMMAND

	//////////////////////////////////////////////////

	void UpdateAggregatedNetStats();

	/**
	 * Fill the main menu with menu items.
	 *
	 * @param MenuBuilder The multi-box builder that should be filled with content for this pull-down menu.
	 * @param TabManager A Tab Manager from which to populate tab spawner menu items.
	 */
	static void FillMenu(FMenuBuilder& MenuBuilder, const TSharedPtr<FTabManager> TabManager);

	/** Callback for determining the visibility of the 'Select a session' overlay. */
	EVisibility IsSessionOverlayVisible() const;

	/** Callback for getting the enabled state of the profiler window. */
	bool IsProfilerEnabled() const;

	/** Updates the amount of time the profiler has been active. */
	EActiveTimerReturnType UpdateActiveDuration(double InCurrentTime, float InDeltaTime);

	//////////////////////////////////////////////////

	/**
	 * Ticks this widget. Override in derived classes, but always call the parent implementation.
	 *
	 * @param  AllottedGeometry The space allotted for this widget
	 * @param  InCurrentTime  Current absolute real time
	 * @param  InDeltaTime  Real time passed since last tick
	 */
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/**
	 * The system will use this event to notify a widget that the cursor has entered it. This event is NOT bubbled.
	 *
	 * @param MyGeometry The Geometry of the widget receiving the event
	 * @param MouseEvent Information about the input event
	 */
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	/**
	 * The system will use this event to notify a widget that the cursor has left it. This event is NOT bubbled.
	 *
	 * @param MouseEvent Information about the input event
	 */
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;

	/**
	 * Called after a key is pressed when this widget has focus
	 *
	 * @param MyGeometry The Geometry of the widget receiving the event
	 * @param  InKeyEvent  Key event
	 *
	 * @return  Returns whether the event was handled, along with other possible actions
	 */
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	/**
	 * Called when the user is dropping something onto a widget; terminates drag and drop.
	 *
	 * @param MyGeometry      The geometry of the widget receiving the event.
	 * @param DragDropEvent   The drag and drop event.
	 *
	 * @return A reply that indicated whether this event was handled.
	 */
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;

	/**
	 * Called during drag and drop when the the mouse is being dragged over a widget.
	 *
	 * @param MyGeometry      The geometry of the widget receiving the event.
	 * @param DragDropEvent   The drag and drop event.
	 *
	 * @return A reply that indicated whether this event was handled.
	 */
	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)  override;

	//////////////////////////////////////////////////

	void UpdateAvailableGameInstances();
	void UpdateAvailableConnections();
	void UpdateAvailableConnectionModes();

	TSharedRef<SWidget> GameInstance_OnGenerateWidget(TSharedPtr<FGameInstanceItem> InGameInstance) const;
	void GameInstance_OnSelectionChanged(TSharedPtr<FGameInstanceItem> NewGameInstance, ESelectInfo::Type SelectInfo);
	FText GameInstance_GetSelectedText() const;
	FText GameInstance_GetSelectedTooltipText() const;

	TSharedRef<SWidget> Connection_OnGenerateWidget(TSharedPtr<FConnectionItem> InConnection) const;
	void Connection_OnSelectionChanged(TSharedPtr<FConnectionItem> NewConnection, ESelectInfo::Type SelectInfo);
	FText Connection_GetSelectedText() const;
	FText Connection_GetSelectedTooltipText() const;

	TSharedRef<SWidget> ConnectionMode_OnGenerateWidget(TSharedPtr<FConnectionModeItem> InConnectionMode) const;
	void ConnectionMode_OnSelectionChanged(TSharedPtr<FConnectionModeItem> NewConnectionMode, ESelectInfo::Type SelectInfo);
	FText ConnectionMode_GetSelectedText() const;
	FText ConnectionMode_GetSelectedTooltipText() const;

	//////////////////////////////////////////////////

private:
	/** Commandlist used in the window (Maps commands to window specific actions) */
	TSharedPtr<FUICommandList> CommandList;

	/** Holds the tab manager that manages the front-end's tabs. */
	TSharedPtr<FTabManager> TabManager;

	/** The Packet View widget */
	TSharedPtr<SPacketView> PacketView;

	/** The Packet Content widget */
	TSharedPtr<SPacketContentView> PacketContentView;

	/** The Net Stats widget */
	TSharedPtr<SNetStatsView> NetStatsView;

	TSharedPtr<SComboBox<TSharedPtr<FGameInstanceItem>>> GameInstanceComboBox;
	TArray<TSharedPtr<FGameInstanceItem>> AvailableGameInstances;
	TSharedPtr<FGameInstanceItem> SelectedGameInstance;

	TSharedPtr<SComboBox<TSharedPtr<FConnectionItem>>> ConnectionComboBox;
	TArray<TSharedPtr<FConnectionItem>> AvailableConnections;
	TSharedPtr<FConnectionItem> SelectedConnection;

	TSharedPtr<SComboBox<TSharedPtr<FConnectionModeItem>>> ConnectionModeComboBox;
	TArray<TSharedPtr<FConnectionModeItem>> AvailableConnectionModes;
	TSharedPtr<FConnectionModeItem> SelectedConnectionMode;

	// [SelectedPacketStartIndex, SelectedPacketEndIndex) is the exclusive interval of selected packages.
	// NumSelectedPackets == SelectedPacketEndIndex - SelectedPacketStartIndex.
	uint32 SelectedPacketStartIndex;
	uint32 SelectedPacketEndIndex;

	// [SelectionStartPosition, SelectionEndPosition) is the exclusive selected bit range inside a single selected package.
	// Used only when NumSelectedPackets == SelectedPacketEndIndex - SelectedPacketStartIndex == 1.
	// SelectedBitSize == SelectionEndPosition - SelectionStartPosition.
	uint32 SelectionStartPosition;
	uint32 SelectionEndPosition;

	static const uint32 InvalidEventTypeIndex = uint32(-1);
	uint32 SelectedEventTypeIndex;

	/** The handle to the active update duration tick */
	TWeakPtr<FActiveTimerHandle> ActiveTimerHandle;

	/** The number of seconds the profiler has been active */
	float DurationActive;
};
