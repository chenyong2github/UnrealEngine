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
#include "../../BlueprintGraph/Classes/EdGraphSchema_K2.h"

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

			virtual void RegisterDataType(FName InPinCategoryName, FName InPinSubCategoryName, const FDataTypeRegistryInfo& InRegistryInfo) override
			{
				DataTypeInfo.Add(InRegistryInfo.DataTypeName, FEditorDataType(InPinCategoryName, InPinSubCategoryName, InRegistryInfo));
			}

			void RegisterCoreDataTypes()
			{
				TArray<FName> DataTypeNames = Frontend::GetAllAvailableDataTypes();
				for (FName DataTypeName : DataTypeNames)
				{
					FDataTypeRegistryInfo RegistryInfo;
					Frontend::GetTraitsForDataType(DataTypeName, RegistryInfo);

					FName PinCategory = DataTypeName;
					FName PinSubCategory;

					// Execution path triggers are specialized
					if (DataTypeName == "Trigger")
					{
						PinCategory = FGraphBuilder::PinCategoryExec;
					}

					// GraphEditor by default designates specialized connection
					// specification for Int64, so use it even though literal is
					// boiled down to int32
					else if (DataTypeName == "Int64")
					{
						PinCategory = FGraphBuilder::PinCategoryInt64;
					}

					// Primitives
					else
					{
						switch (RegistryInfo.PreferredLiteralType)
						{
							case ELiteralType::Boolean:
							{
								PinCategory = FGraphBuilder::PinCategoryBoolean;
							}
							break;

							case ELiteralType::Float:
							{
								PinCategory = FGraphBuilder::PinCategoryFloat;

								// Doubles use the same preferred literal
								// but different colorization
								if (DataTypeName == "Double")
								{
									PinCategory = FGraphBuilder::PinCategoryFloat;
								}

								// Differentiate stronger numeric types associated with audio
								if (DataTypeName == "Frequency"
									|| DataTypeName == "Time"
									|| DataTypeName == "Time:HighResolution"
									|| DataTypeName == "Time:SampleResolution"
								)
								{
									PinSubCategory = FGraphBuilder::PinSubCategoryAudioNumeric;
								}
							}
							break;

							case ELiteralType::Integer:
							{
								PinCategory = FGraphBuilder::PinCategoryInt32;
							}
							break;

							case ELiteralType::String:
							{
								PinCategory = FGraphBuilder::PinCategoryString;
							}
							break;

							case ELiteralType::UObjectProxy:
							{
								PinCategory = FGraphBuilder::PinCategoryObject;
							}
							break;

							case ELiteralType::UObjectProxyArray:
							{
								PinCategory = FGraphBuilder::PinSubCategoryObjectArray;
							}
							break;

							// Register atypical primitives
							default:
							case ELiteralType::None:
							case ELiteralType::Invalid:
							{

								// Audio types are ubiquitous, so specialize
								if (DataTypeName == "Audio:Buffer"
									|| DataTypeName == "Audio:Unformatted"
									|| DataTypeName == "Audio:Mono"
									|| DataTypeName == "Audio:Stereo"
									|| DataTypeName == "Audio:Multichannel"
								)
								{
									PinSubCategory = FGraphBuilder::PinSubCategoryAudioFormat;
								}
								static_assert(static_cast<int32>(ELiteralType::Invalid) == 7, "Possible missing binding of pin category to primitive type");
							}
							break;
						}
					}

					DataTypeInfo.Add(DataTypeName, FEditorDataType(PinCategory, PinSubCategory, RegistryInfo));
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
