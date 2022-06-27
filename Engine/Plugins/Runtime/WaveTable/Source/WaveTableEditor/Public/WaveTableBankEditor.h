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
struct FWaveTableTransform;
class IToolkitHost;
class SCurveEditorPanel;
class UCurveFloat;

namespace WaveTable
{
	namespace Editor
	{
		class WAVETABLEEDITOR_API FBankEditorBase : public FAssetEditorToolkit, public FNotifyHook, public FEditorUndoClient
		{
		public:
			FBankEditorBase();
			virtual ~FBankEditorBase() = default;

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

			virtual bool GetIsPropertyEditorDisabled() const { return false; }

			virtual EWaveTableResolution GetBankResolution() const = 0;
			virtual bool GetBankIsBipolar() const = 0;

			virtual TUniquePtr<FWaveTableCurveModel> ConstructCurveModel(FRichCurve& InRichCurve, UObject* InParentObject, EWaveTableCurveSource InSource) = 0;

			virtual FWaveTableTransform* GetTransform (int32 InIndex) const = 0;

			virtual int32 GetNumTransforms() const = 0;

			void SetCurve(int32 InTransformIndex, FRichCurve& InRichCurve, EWaveTableCurveSource InSource);

		private:
			/** Generates expression curve at the given index. */
			void GenerateExpressionCurve(FCurveData& OutCurveData, int32 InTransformIndex, EWaveTableCurveSource InSource, bool bInIsUnset = false);

			void InitCurves();

			void ResetCurves();

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
			void ClearExpressionCurve(int32 InTransformIndex);

			bool RequiresNewCurve(int32 InTransformIndex, const FRichCurve& InRichCurve) const;

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

		class FBankEditor : public WaveTable::Editor::FBankEditorBase
		{
		public:
			FBankEditor() = default;
			virtual ~FBankEditor() = default;

		protected:
			virtual EWaveTableResolution GetBankResolution() const override;
			virtual bool GetBankIsBipolar() const override;
			virtual int32 GetNumTransforms() const override;
			virtual FWaveTableTransform* GetTransform(int32 InIndex) const override;

			virtual TUniquePtr<WaveTable::Editor::FWaveTableCurveModel> ConstructCurveModel(FRichCurve& InRichCurve, UObject* InParentObject, EWaveTableCurveSource InSource) override;
		};
	} // namespace Editor
} // namespace WaveTable