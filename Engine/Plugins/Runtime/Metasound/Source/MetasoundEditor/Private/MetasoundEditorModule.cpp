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

				SetContentRoot(FPaths::EngineContentDir() / TEXT("Editor/Slate"));
				SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));

				static const FVector2D Icon20x20(20.0f, 20.0f);
				static const FVector2D Icon40x40(40.0f, 40.0f);

				// Metasound Editor
				{
					Set("MetasoundEditor.Play", new FSlateImageBrush(RootToContentDir(TEXT("Icons/icon_SCueEd_PlayCue_40x.png")), Icon40x40));
					Set("MetasoundEditor.Play.Small", new FSlateImageBrush(RootToContentDir(TEXT("Icons/icon_SCueEd_PlayCue_40x.png")), Icon20x20));
					Set("MetasoundEditor.Stop", new FSlateImageBrush(RootToContentDir(TEXT("Icons/icon_SCueEd_Stop_40x.png")), Icon40x40));
					Set("MetasoundEditor.Stop.Small", new FSlateImageBrush(RootToContentDir(TEXT("Icons/icon_SCueEd_Stop_40x.png")), Icon20x20));
					Set("MetasoundEditor.Import", new FSlateImageBrush(RootToContentDir(TEXT("/Old/Kismet2/compile_40px.png")), Icon40x40));
					Set("MetasoundEditor.Import.Small", new FSlateImageBrush(RootToContentDir(TEXT("/Old/Kismet2/compile_40px.png")), Icon20x20));
					Set("MetasoundEditor.Export", new FSlateImageBrush(RootToContentDir(TEXT("/Old/Kismet2/compile_40px.png")), Icon40x40));
					Set("MetasoundEditor.Export.Small", new FSlateImageBrush(RootToContentDir(TEXT("/Old/Kismet2/compile_40px.png")), Icon20x20));
					Set("MetasoundEditor.ExportError", new FSlateImageBrush(RootToContentDir(TEXT("/Old/Kismet2/CompileStatus_Fail.png")), Icon40x40));
					Set("MetasoundEditor.ExportError.Small", new FSlateImageBrush(RootToContentDir(TEXT("/Old/Kismet2/CompileStatus_Broken_Small.png")), Icon20x20));
				}

				FSlateStyleRegistry::RegisterSlateStyle(*this);
			}
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

					FName PinType = NAME_None;
					switch (RegistryInfo.PreferredLiteralType)
					{
						case ELiteralArgType::Boolean:
						{
							PinType = FGraphBuilder::PinPrimitiveBoolean;
						}
						break;

						case ELiteralArgType::Float:
						{
							PinType = FGraphBuilder::PinPrimitiveFloat;
						}
						break;

						case ELiteralArgType::Integer:
						{
							PinType = FGraphBuilder::PinPrimitiveInteger;
						}
						break;

						case ELiteralArgType::String:
						{
							PinType = FGraphBuilder::PinPrimitiveString;
						}
						break;

						case ELiteralArgType::UObjectProxy:
						{
							PinType = FGraphBuilder::PinPrimitiveUObject;
						}
						break;

						case ELiteralArgType::UObjectProxyArray:
						{
							PinType = FGraphBuilder::PinPrimitiveUObjectArray;
						}
						break;

						// Register atypical primitives
						default:
						case ELiteralArgType::None:
						case ELiteralArgType::Invalid:
						{
							static_assert(static_cast<int32>(ELiteralArgType::Invalid) == 7, "Possible missing binding of pin category to primitive type");
						}
						break;
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
					"MetasoundLiteralDescription",
					FOnGetPropertyTypeCustomizationInstance::CreateLambda([]() { return MakeShared<FMetasoundLiteralDescriptionDetailCustomization>(); }));

				StyleSet = MakeShared<FSlateStyle>();

				RegisterCoreDataTypes();

				GraphConnectionFactory = MakeShared<FGraphConnectionDrawingPolicyFactory>();
				FEdGraphUtilities::RegisterVisualPinConnectionFactory(GraphConnectionFactory);
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

				AssetActions.Reset();
				DataTypeInfo.Reset();
			}

			TArray<TSharedPtr<FAssetTypeActions_Base>> AssetActions;
			TMap<FName, FEditorDataType> DataTypeInfo;

			TSharedPtr<FGraphPanelPinConnectionFactory> GraphConnectionFactory;
			TSharedPtr<FSlateStyleSet> StyleSet;
		};
	} // namespace Editor
} // namespace Metasound

IMPLEMENT_MODULE(Metasound::Editor::FModule, MetasoundEditor);
