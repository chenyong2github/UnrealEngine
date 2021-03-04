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
#include "ISettingsModule.h"
#include "Metasound.h"
#include "MetasoundSource.h"
#include "MetasoundAssetTypeActions.h"
#include "MetasoundDetailCustomization.h"
#include "MetasoundInputNodeDetailCustomization.h"
#include "MetasoundEditorGraphBuilder.h"
#include "MetasoundEditorGraphNodeFactory.h"
#include "MetasoundEditorGraphSchema.h"
#include "MetasoundEditorSettings.h"
#include "MetasoundFrontendRegistries.h"
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
					Set("MetasoundEditor.Graph.Node.Math.Multiply", new FSlateImageBrush(RootToContentDir(TEXT("/Graph/node_math_multiply_40x.png")), Icon40x40));
					Set("MetasoundEditor.Graph.Node.Math.Subtract", new FSlateImageBrush(RootToContentDir(TEXT("/Graph/node_math_subtract_40x.png")), Icon40x40));
					Set("MetasoundEditor.Graph.Node.Math.Modulo", new FSlateImageBrush(RootToContentDir(TEXT("/Graph/node_math_modulo_40x.png")), Icon40x40));

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
			void RegisterNodeInputClasses()
			{
				TSubclassOf<UMetasoundEditorGraphInputNode> NodeClass;
				for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
				{
					UClass* Class = *ClassIt;
					if (!Class->IsNative())
					{
						continue;
					}

					if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
					{
						continue;
					}

					if (!ClassIt->IsChildOf(UMetasoundEditorGraphInputNode::StaticClass()))
					{
						continue;
					}

					if (const UMetasoundEditorGraphInputNode* InputNode = Class->GetDefaultObject<UMetasoundEditorGraphInputNode>())
					{
						NodeInputClassRegistry.Add(InputNode->GetLiteralType(), InputNode->GetClass());
					}
				}
			}

			const TSubclassOf<UMetasoundEditorGraphInputNode> FindNodeInputClass(EMetasoundFrontendLiteralType InLiteralType) const override
			{
				return NodeInputClassRegistry.FindRef(InLiteralType);
			}

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
				FEdGraphPinType PinType(InPinCategoryName, InPinSubCategoryName, nullptr, EPinContainerType::None, false, FEdGraphTerminalType());
				DataTypeInfo.Emplace(InRegistryInfo.DataTypeName, Editor::FEditorDataType(MoveTemp(PinType), InRegistryInfo));
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
						PinCategory = FGraphBuilder::PinCategoryTrigger;
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
									PinCategory = FGraphBuilder::PinCategoryDouble;
								}

								// Differentiate stronger numeric types associated with audio
								if (DataTypeName == "Frequency"
									|| DataTypeName == "Time"
									|| DataTypeName == "Time:HighResolution"
									|| DataTypeName == "Time:SampleResolution"
								)
								{
									PinSubCategory = FGraphBuilder::PinSubCategoryTime;
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
								// TODO: Implement, or will be nuked in favor of general array support for all types
							}
							break;

							
							case ELiteralType::None:
							case ELiteralType::Invalid:
							// TODO: handle array types.
							case ELiteralType::NoneArray:
							case ELiteralType::BooleanArray:
							case ELiteralType::IntegerArray:
							case ELiteralType::StringArray:
							case ELiteralType::FloatArray:
							default:
							{
								// Audio types are ubiquitous, so added as subcategory
								// to be able to stylize connections (i.e. wire color & wire animation)
								if (DataTypeName == "Audio")
								{
									PinCategory = FGraphBuilder::PinCategoryAudio;
								}
								static_assert(static_cast<int32>(ELiteralType::Invalid) == 12, "Possible missing binding of pin category to primitive type");
							}
							break;
						}
					}

					FEdGraphPinType PinType(PinCategory, PinSubCategory, nullptr, EPinContainerType::None, false, FEdGraphTerminalType());
 					UClass* ClassToUse = FMetasoundFrontendRegistryContainer::Get()->GetLiteralUClassForDataType(DataTypeName);
 					PinType.PinSubCategoryObject = Cast<UObject>(ClassToUse);

					DataTypeInfo.Emplace(DataTypeName, FEditorDataType(MoveTemp(PinType), RegistryInfo));
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

				PropertyModule.RegisterCustomClassLayout(
					UMetasoundEditorGraphInputNode::StaticClass()->GetFName(),
					FOnGetDetailCustomizationInstance::CreateLambda([]() { return MakeShared<FMetasoundInputNodeDetailCustomization>(); }));

				PropertyModule.RegisterCustomPropertyTypeLayout(
					"MetasoundEditorGraphInputInt",
					FOnGetPropertyTypeCustomizationInstance::CreateLambda([]() { return MakeShared<FMetasoundInputNodeIntDetailCustomization>(); }));

				PropertyModule.RegisterCustomPropertyTypeLayout(
					"MetasoundEditorGraphInputObject",
					FOnGetPropertyTypeCustomizationInstance::CreateLambda([]() { return MakeShared<FMetasoundInputNodeObjectDetailCustomization>(); }));

				StyleSet = MakeShared<FSlateStyle>();

				RegisterCoreDataTypes();
				RegisterNodeInputClasses();

				GraphConnectionFactory = MakeShared<FGraphConnectionDrawingPolicyFactory>();
				FEdGraphUtilities::RegisterVisualPinConnectionFactory(GraphConnectionFactory);

				GraphNodeFactory = MakeShared<FMetasoundGraphNodeFactory>();
				FEdGraphUtilities::RegisterVisualNodeFactory(GraphNodeFactory);

				GraphPanelPinFactory = MakeShared<FMetasoundGraphPanelPinFactory>();
				FEdGraphUtilities::RegisterVisualPinFactory(GraphPanelPinFactory);

				ISettingsModule& SettingsModule = FModuleManager::LoadModuleChecked<ISettingsModule>("Settings");

				SettingsModule.RegisterSettings("Editor", "Audio", "Metasound Editor",
					NSLOCTEXT("MetasoundsEditor", "MetasoundEditorSettingsName", "Metasound Editor"),
					NSLOCTEXT("MetasoundsEditor", "MetasoundEditorSettingsDescription", "Customize Metasound Editor."),
					GetMutableDefault<UMetasoundEditorSettings>()
				);
			}

			virtual void ShutdownModule() override
			{
				if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
				{
					SettingsModule->UnregisterSettings("Editor", "Audio", "Metasound Editor");
				}

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
			TMap<EMetasoundFrontendLiteralType, const TSubclassOf<UMetasoundEditorGraphInputNode>> NodeInputClassRegistry;

			TSharedPtr<FMetasoundGraphNodeFactory> GraphNodeFactory;
			TSharedPtr<FGraphPanelPinConnectionFactory> GraphConnectionFactory;
			TSharedPtr<FMetasoundGraphPanelPinFactory> GraphPanelPinFactory;
			TSharedPtr<FSlateStyleSet> StyleSet;
		};
	} // namespace Editor
} // namespace Metasound

IMPLEMENT_MODULE(Metasound::Editor::FModule, MetasoundEditor);
