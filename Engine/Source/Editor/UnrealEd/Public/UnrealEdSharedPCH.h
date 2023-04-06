// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EngineSharedPCH.h"

// From Core:
#include "Math/BasicMathExpressionEvaluator.h"
#include "Misc/ExpressionParserTypes.h"
#include "Misc/FilterCollection.h"
#include "Misc/IFilter.h"

// From CoreUObject:
#include "Misc/NotifyHook.h"
#include "UObject/ObjectKey.h"

// From SlateCore:
#include "Layout/LayoutUtils.h"

// From Slate:
#include "Framework/Docking/LayoutService.h"
#include "Framework/MarqueeRect.h"
#include "Widgets/Input/NumericTypeInterface.h"
#include "Widgets/Notifications/SErrorText.h"
#include "Widgets/SToolTip.h"

// From Messaging:
#include "IMessageContext.h"

// From AssetRegistry:
#include "AssetRegistry/ARFilter.h"

// From Engine:
#include "BoneIndices.h"
#include "DataTableUtils.h"
#include "EdGraph/EdGraphNodeUtils.h"
#include "Engine/CurveTable.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "Engine/LevelStreaming.h"
#include "LatentActions.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsSettingsEnums.h"
#include "PreviewScene.h"
#include "Vehicles/TireType.h"
#include "VisualLogger/VisualLogger.h"
#include "VisualLogger/VisualLoggerTypes.h"

// From BlueprintGraph:
#include "BlueprintNodeSignature.h"
#include "K2Node.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_EditablePinBase.h"

// From GameplayTasks:
#include "GameplayTaskOwnerInterface.h"
#include "GameplayTaskTypes.h"
#include "GameplayTask.h"

// From UnrealEd:
#include "AssetThumbnail.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Editor/UnrealEdTypes.h"
#include "EditorComponents.h"
#include "EditorUndoClient.h"
#include "EditorViewportClient.h"
#include "Factories/Factory.h"
#include "GraphEditor.h"
#include "ScopedTransaction.h"
#include "Settings/EditorLoadingSavingSettings.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Settings/LevelEditorViewportSettings.h"
#include "TickableEditorObject.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Toolkits/BaseToolkit.h"
#include "Toolkits/IToolkit.h"
#include "Toolkits/IToolkitHost.h"
#include "UnrealWidgetFwd.h"
#include "Viewports.h"

// From ToolMenus
#include "ToolMenus.h"