// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundEditorModule.h"

#include "AssetTypeActions_Base.h"
#include "Brushes/SlateImageBrush.h"
#include "CoreMinimal.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphUtilities.h"
#include "EditorStyleSet.h"
#include "IDetailCustomization.h"
#include "Metasound.h"
#include "MetasoundSource.h"
#include "MetasoundAssetTypeActions.h"
#include "MetasoundDetailCustomization.h"
#include "MetasoundLiteralDescriptionDetailCustomization.h"
#include "MetasoundEditorGraphBuilder.h"
#include "MetasoundEditorGraphNodeFactory.h"
#include "MetasoundEditorGraphSchema.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorDelegates.h"
#include "PropertyEditorModule.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleMacros.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"
#include "Templates/SharedPointer.h"

DEFINE_LOG_CATEGORY(LogMetasoundEditor);


namespace Metasound
{
	namespace Editor
	{
		static const FName AssetToolName = TEXT("AssetTools");

		template <typename T>
		void AddAssetAction(IAssetTools& AssetTools, TArray<TSharedPtr<FAssetTypeActions_Base>>& AssetArray)
		{
			TSharedPtr<T> AssetAction = MakeShared<T>();
			TSharedPtr<FAssetTypeActions_Base> AssetActionBase = StaticCastSharedPtr<FAssetTypeActions_Base>(AssetAction);
			AssetTools.RegisterAssetTypeActions(AssetAction.ToSharedRef());
			AssetArray.Add(AssetActionBase);
		}

		class FSlateStyle : public FSlateStyleSet
		{
		public:
			FSlateStyle()
				: FSlateStyleSet("MetasoundStyle")
			{
				SetParentStyleName(FEditorStyle::GetStyleSetName());

				SetContentRoot(FPaths::EnginePluginsDir() / TEXT("Runtime/Metasound/Content/Editor/Slate"));
				SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));

				static const FVector2D Icon20x20(20.0f, 20.0f);
				static const FVector2D Icon40x40(40.0f, 40.0f);

				// Metasound Editor
				{
					// Actions
					Set("MetasoundEditor.Play", new FSlateImageBrush(RootToContentDir(TEXT("Icons/play_40x.png")), Icon40x40));
					Set("MetasoundEditor.Play.Small", new FSlateImageBrush(RootToContentDir(TEXT("Icons/play_40x.png")), Icon20x20));
					Set("MetasoundEditor.Stop", new FSlateImageBrush(RootToContentDir(TEXT("Icons/stop_40x.png")), Icon40x40));
					Set("MetasoundEditor.Stop.Small", new FSlateImageBrush(RootToContentDir(TEXT("Icons/stop_40x.png")), Icon20x20));
					Set("MetasoundEditor.Import", new FSlateImageBrush(RootToContentDir(TEXT("/Icons/build_40x.png")), Icon40x40));
					Set("MetasoundEditor.Import.Small", new FSlateImageBrush(RootToContentDir(TEXT("/Icons/build_40x.png")), Icon20x20));
					Set("MetasoundEditor.Export", new FSlateImageBrush(RootToContentDir(TEXT("/Icons/build_40x.png")), Icon40x40));
					Set("MetasoundEditor.Export.Small", new FSlateImageBrush(RootToContentDir(TEXT("/Icons/build_40x.png")), Icon20x20));
					Set("MetasoundEditor.ExportError", new FSlateImageBrush(RootToContentDir(TEXT("/Icons/build_error_40x.png")), Icon40x40));
					Set("MetasoundEditor.ExportError.Small", new FSlateImageBrush(RootToContentDir(TEXT("/Icons/build_error_40x.png")), Icon20x20));

					// Graph Editor
					Set("MetasoundEditor.Graph.Node.Body.Input", new FSlateImageBrush(RootToContentDir(TEXT("/Graph/node_input_body_64x.png")), FVector2D(114.0f, 64.0f)));
					Set("MetasoundEditor.Graph.Node.Body.Default", new FSlateImageBrush(RootToContentDir(TEXT("/Graph/node_default_body_64x.png")), FVector2D(64.0f, 64.0f)));
					Set("MetasoundEditor.Graph.Node.Math.Add", new FSlateImageBrush(RootToContentDir(TEXT("/Graph/node_math_add_40x.png")), Icon40x40));
					Set("MetasoundEditor.Graph.Node.Math.Divide", new FSlateImageBrush(RootToContentDir(TEXT("/Graph/node_math_divide_40x.png")), Icon40x40));
					Set("MetasoundEditor.Graph.Node.Math.Multiply", new FSlateImageBrush(RootToContentDir(TEXT("/Graph/node_math_multiply_40x.png")), FVector2D(32.0f, 32.0f)));
					Set("MetasoundEditor.Graph.Node.Math.RandRange", new FSlateImageBrush(RootToContentDir(TEXT("/Graph/node_math_random_40x.png")), FVector2D(26.0f, 40.0f)));
					Set("MetasoundEditor.Graph.Node.Math.Subtract", new FSlateImageBrush(RootToContentDir(TEXT("/Graph/node_math_subtract_40x.png")), Icon40x40));

					// Misc
					Set("MetasoundEditor.Speaker", new FSlateImageBrush(RootToContentDir(TEXT("/Icons/speaker_144x.png")), FVector2D(144.0f, 144.0f)));
				}

				FSlateStyleRegistry::RegisterSlateStyle(*this);
			}
		};

		class FMetasoundGraphPanelPinFactory : public FGraphPanelPinFactory
		{
		};

		class FModule : public IMetasoundEditorModule
		{
			virtual const FEditorDataType& FindDataType(FName InDataTypeName) const override
			{
				return DataTypeInfo.FindChecked(InDataTypeName);
			}

			virtual void IterateDataTypes(TUniqueFunction<void(const FEditorDataType&)> InDataTypeFunction) const override
			{
				for (const TPair<FName, FEditorDataType>& Pair : DataTypeInfo)
				{
					InDataTypeFunction(Pair.Value);
				}
			}

			virtual void RegisterDataType(FName InPinCategoryName, const FDataTypeRegistryInfo& InRegistryInfo) override
			{
				DataTypeInfo.Add(InRegistryInfo.DataTypeName, FEditorDataType(InPinCategoryName, InRegistryInfo));
			}

			void RegisterCoreDataTypes()
			{
				TArray<FName> DataTypeNames = Frontend::GetAllAvailableDataTypes();
				for (FName DataTypeName : DataTypeNames)
				{
					FDataTypeRegistryInfo RegistryInfo;
					Frontend::GetTraitsForDataType(DataTypeName, RegistryInfo);

					FName PinType = DataTypeName;

					// Special Flowers

					// Execution path triggers are specialized
					if (DataTypeName == "Primitive:Trigger")
					{
						PinType = FGraphBuilder::PinPrimitiveTrigger;
					}

					// GraphEditor by default designates specialized connection
					// specification for Int64, so use it
					else if (DataTypeName == "Primitive:Int64")
					{
						PinType = FGraphBuilder::PinPrimitiveInt64;
					}

					// Differentiate stronger numeric types associated with audio
					else if (DataTypeName == "Primitive:Frequency"
						|| DataTypeName == "Primitive:Time"
						|| DataTypeName == "Primitive:Time:HighResolution"
						|| DataTypeName == "Primitive:Time:SampleResolution"
						)
					{
						PinType = FGraphBuilder::PinAudioNumeric;
					}

					// Audio types are ubiquitous, so specialize
					else if (DataTypeName == "Audio:Buffer"
						|| DataTypeName == "Audio:Unformatted"
						|| DataTypeName == "Audio:Mono"
						|| DataTypeName == "Audio:Stereo"
						|| DataTypeName == "Audio:Multichannel"
						)
					{
						PinType = FGraphBuilder::PinAudioFormat;
					}

					// Primitives
					else
					{
						switch (RegistryInfo.PreferredLiteralType)
						{
							case ELiteralType::Boolean:
							{
								PinType = FGraphBuilder::PinPrimitiveBoolean;
							}
							break;

							case ELiteralType::Float:
							{
								PinType = FGraphBuilder::PinPrimitiveFloat;
							}
							break;

							case ELiteralType::Integer:
							{
								PinType = FGraphBuilder::PinPrimitiveInt32;
							}
							break;

							case ELiteralType::String:
							{
								PinType = FGraphBuilder::PinPrimitiveString;
							}
							break;

							case ELiteralType::UObjectProxy:
							{
								PinType = FGraphBuilder::PinPrimitiveUObject;
							}
							break;

							case ELiteralType::UObjectProxyArray:
							{
								PinType = FGraphBuilder::PinPrimitiveUObjectArray;
							}
							break;

							// Register atypical primitives
							default:
							case ELiteralType::None:
							case ELiteralType::Invalid:
							{
								static_assert(static_cast<int32>(ELiteralType::Invalid) == 7, "Possible missing binding of pin category to primitive type");
							}
							break;
						}
					}

					DataTypeInfo.Add(DataTypeName, FEditorDataType(PinType, RegistryInfo));
				}
			}

			virtual void StartupModule() override
			{
				// Register Metasound asset type actions
				IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(AssetToolName).Get();
				AddAssetAction<FAssetTypeActions_Metasound>(AssetTools, AssetActions);
				AddAssetAction<FAssetTypeActions_MetasoundSource>(AssetTools, AssetActions);

				FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
				
				PropertyModule.RegisterCustomClassLayout(
					UMetasound::StaticClass()->GetFName(),
					FOnGetDetailCustomizationInstance::CreateLambda([]() { return MakeShared<FMetasoundDetailCustomization>(UMetasound::GetDocumentPropertyName()); }));

				PropertyModule.RegisterCustomClassLayout(
					UMetasoundSource::StaticClass()->GetFName(),
					FOnGetDetailCustomizationInstance::CreateLambda([]() { return MakeShared<FMetasoundDetailCustomization>(UMetasoundSource::GetDocumentPropertyName()); }));

				PropertyModule.RegisterCustomPropertyTypeLayout(
					"MetasoundFrontendLiteral",
					FOnGetPropertyTypeCustomizationInstance::CreateLambda([]() { return MakeShared<FMetasoundFrontendLiteralDetailCustomization>(); }));

				StyleSet = MakeShared<FSlateStyle>();

				RegisterCoreDataTypes();

				GraphConnectionFactory = MakeShared<FGraphConnectionDrawingPolicyFactory>();
				FEdGraphUtilities::RegisterVisualPinConnectionFactory(GraphConnectionFactory);

				GraphNodeFactory = MakeShared<FMetasoundGraphNodeFactory>();
				FEdGraphUtilities::RegisterVisualNodeFactory(GraphNodeFactory);

				GraphPanelPinFactory = MakeShared<FMetasoundGraphPanelPinFactory>();
				FEdGraphUtilities::RegisterVisualPinFactory(GraphPanelPinFactory);
			}

			virtual void ShutdownModule() override
			{
				if (FModuleManager::Get().IsModuleLoaded(AssetToolName))
				{
					IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>(AssetToolName).Get();
					for (TSharedPtr<FAssetTypeActions_Base>& AssetAction : AssetActions)
					{
						AssetTools.UnregisterAssetTypeActions(AssetAction.ToSharedRef());
					}
				}

				if (GraphConnectionFactory.IsValid())
				{
					FEdGraphUtilities::UnregisterVisualPinConnectionFactory(GraphConnectionFactory);
				}

				if (GraphNodeFactory.IsValid())
				{
					FEdGraphUtilities::UnregisterVisualNodeFactory(GraphNodeFactory);
					GraphNodeFactory.Reset();
				}

				if (GraphPanelPinFactory.IsValid())
				{
					FEdGraphUtilities::UnregisterVisualPinFactory(GraphPanelPinFactory);
					GraphPanelPinFactory.Reset();
				}

				AssetActions.Reset();
				DataTypeInfo.Reset();
			}

			TArray<TSharedPtr<FAssetTypeActions_Base>> AssetActions;
			TMap<FName, FEditorDataType> DataTypeInfo;

			TSharedPtr<FMetasoundGraphNodeFactory> GraphNodeFactory;
			TSharedPtr<FGraphPanelPinConnectionFactory> GraphConnectionFactory;
			TSharedPtr<FMetasoundGraphPanelPinFactory> GraphPanelPinFactory;
			TSharedPtr<FSlateStyleSet> StyleSet;
		};
	} // namespace Editor
} // namespace Metasound

IMPLEMENT_MODULE(Metasound::Editor::FModule, MetasoundEditor);
