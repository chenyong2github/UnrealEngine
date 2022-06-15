// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CurveEditorTypes.h"
#include "Curves/CurveOwnerInterface.h"
#include "EditorUndoClient.h"
#include "Framework/Docking/TabManager.h"
#include "Math/Color.h"
#include "Misc/NotifyHook.h"
#include "WaveTableCurveEditorViewStacked.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Toolkits/IToolkitHost.h"
#include "WaveTableSettings.h"
#include "Widgets/SWidget.h"


// Forward Declarations
class FCurveEditor;
class IToolkitHost;
class SCurveEditorPanel;
class UCurveFloat;

namespace WaveTable
{
	namespace Editor
	{
		class WAVETABLEEDITOR_API FWaveTableBankEditor : public FAssetEditorToolkit, public FNotifyHook, public FEditorUndoClient
		{
		public:
			FWaveTableBankEditor();
			virtual ~FWaveTableBankEditor() = default;

			void Init(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UObject* InParentObject);

			/** FAssetEditorToolkit interface */
			virtual FName GetToolkitFName() const override;
			virtual FText GetBaseToolkitName() const override;
			virtual FString GetWorldCentricTabPrefix() const override;
			virtual FLinearColor GetWorldCentricTabColorScale() const override;
			virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
			virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;

			/** FNotifyHook interface */
			virtual void NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged) override;

		protected:
			struct FCurveData
			{
				FCurveModelID ModelID;
				TSharedPtr<FRichCurve> ExpressionCurve;

				FCurveData()
					: ModelID(FCurveModelID::Unique())
				{
				}
			};

			virtual void PostUndo(bool bSuccess) override;
			virtual void PostRedo(bool bSuccess) override;

			virtual bool GetIsPropertyEditorDisabled() const = 0;

			/** Generates expression curve at the given index. */
			virtual void GenerateExpressionCurve(FCurveData& OutCurveData, int32 InInputIndex, EWaveTableCurveSource InSource, bool bInIsUnset = false) = 0;

			virtual TUniquePtr<FWaveTableCurveModelBase> ConstructCurveModel(FRichCurve& InRichCurve, UObject* InParentObject, EWaveTableCurveSource InSource) = 0;

			virtual EWaveTableCurve GetCurveType(int32 InInputIndex) const = 0;
			virtual FRichCurve& GetCustomCurveChecked(int32 InInputIndex) const = 0;
			virtual UCurveFloat* GetSharedCurve(int32 InInputIndex) const = 0;
			virtual int32 GetNumCurves() const = 0;

			void SetCurve(int32 InInputIndex, FRichCurve& InRichCurve, EWaveTableCurveSource InSource);

		private:
			void ResetCurves();
			void InitCurves();

			/** Updates & redraws curves. */
			void RefreshCurves();

			/**	Spawns the tab allowing for editing/viewing the output curve(s) */
			TSharedRef<SDockTab> SpawnTab_OutputCurve(const FSpawnTabArgs& Args);

			/**	Spawns the tab allowing for editing/viewing the output curve(s) */
			TSharedRef<SDockTab> SpawnTab_Properties(const FSpawnTabArgs& Args);

			/** Get the orientation for the snap value controls. */
			EOrientation GetSnapLabelOrientation() const;

			/** Trims keys out-of-bounds in provided curve */
			static void TrimKeys(FRichCurve& OutCurve);

			/** Clears the expression curve at the given input index */
			void ClearExpressionCurve(int32 InInputIndex);

			bool RequiresNewCurve(int32 InInputIndex, const FRichCurve& InRichCurve) const;

			TSharedPtr<FUICommandList> ToolbarCurveTargetCommands;

			TSharedPtr<FCurveEditor> CurveEditor;
			TSharedPtr<SCurveEditorPanel> CurvePanel;

			TArray<FCurveData> CurveData;

			/** Properties tab */
			TSharedPtr<IDetailsView> PropertiesView;

			/** Settings Editor App Identifier */
			static const FName AppIdentifier;
			static const FName CurveTabId;
			static const FName PropertiesTabId;
		};
	} // namespace Editor
} // namespace WaveTable