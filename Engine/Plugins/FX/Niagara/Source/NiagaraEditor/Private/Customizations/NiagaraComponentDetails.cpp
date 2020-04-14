// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraComponentDetails.h"
#include "PropertyHandle.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterface.h"
#include "DetailLayoutBuilder.h"
#include "IDetailPropertyRow.h"
#include "DetailWidgetRow.h"
#include "Materials/Material.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/SToolTip.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "EditorStyleSet.h"
#include "NiagaraSystemInstance.h"
#include "ViewModels/NiagaraParameterViewModel.h"
#include "NiagaraEditorUtilities.h"
#include "INiagaraEditorTypeUtilities.h"
#include "NiagaraTypes.h"
#include "ScopedTransaction.h"
#include "SNiagaraParameterEditor.h"
#include "NiagaraEditorModule.h"
#include "PropertyEditorModule.h"
#include "Modules/ModuleManager.h"
#include "IStructureDetailsView.h"
#include "IDetailsView.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableText.h"
#include "Editor.h"
#include "NiagaraParameterCollectionViewModel.h"
#include "NiagaraScriptSource.h"
#include "NiagaraNodeInput.h"
#include "NiagaraGraph.h"
#include "GameDelegates.h"
#include "NiagaraEditorStyle.h"
#include "IDetailChildrenBuilder.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Images/SImage.h"
#include "NiagaraEditorModule.h"
#include "Modules/ModuleManager.h"
#include "INiagaraEditorTypeUtilities.h"
#include "Widgets/Layout/SBox.h"
#include "UObject/WeakObjectPtr.h"
#include "NiagaraUserRedirectionParameterStore.h"

#define LOCTEXT_NAMESPACE "NiagaraComponentDetails"

static FNiagaraVariant GetParameterValueFromAsset(const FNiagaraVariableBase& Parameter, const UNiagaraComponent* Component)
{
	FNiagaraUserRedirectionParameterStore& UserParameterStore = Component->GetAsset()->GetExposedParameters();
	if (Parameter.IsDataInterface())
	{
		int32 Index = UserParameterStore.IndexOf(Parameter);
		if (Index != INDEX_NONE)
		{
			return FNiagaraVariant(UserParameterStore.GetDataInterfaces()[Index]);
		}
	}
	
	if (Parameter.IsUObject())
	{
		int32 Index = UserParameterStore.IndexOf(Parameter);
		if (Index != INDEX_NONE)
		{
			return FNiagaraVariant(UserParameterStore.GetUObjects()[Index]);
		}
	}

	const uint8* ParameterData = UserParameterStore.GetParameterData(Parameter);
	if (ParameterData == nullptr)
	{
		return FNiagaraVariant();
	}
	
	return FNiagaraVariant(ParameterData, Parameter.GetSizeInBytes());
}

static FNiagaraVariant GetCurrentParameterValue(const FNiagaraVariableBase& Parameter, const UNiagaraComponent* Component)
{
	FNiagaraVariant OverriddenValue = Component->FindParameterOverride(Parameter);
	if (OverriddenValue.IsValid())
	{
		return OverriddenValue;
	}
	
	return GetParameterValueFromAsset(Parameter, Component);
}

// Proxy class to allow us to override values on the component that are not yet overridden.
class FNiagaraParameterProxy
{
public:
	FNiagaraParameterProxy(TWeakObjectPtr<UNiagaraComponent> InComponent, const FNiagaraVariableBase& InKey, const FNiagaraVariant& InValue, const FSimpleDelegate& InOnRebuild, TArray<TSharedPtr<IPropertyHandle>> InPropertyHandles)
	{
		bResettingToDefault = false;
		Component = InComponent;
		ParameterKey = InKey;
		ParameterValue = InValue;
		OnRebuild = InOnRebuild;
		PropertyHandles = InPropertyHandles;
	}

	FReply OnResetToDefaultClicked()
	{
		OnResetToDefault();
		return FReply::Handled();
	}

	void OnResetToDefault()
	{
		UNiagaraComponent* RawComponent = Component.Get();
		if (RawComponent != nullptr)
		{
			FScopedTransaction ScopedTransaction(NSLOCTEXT("UnrealEd", "PropertyWindowResetToDefault", "Reset to Default"));
			RawComponent->Modify();

			bResettingToDefault = true;

			for (TSharedPtr<IPropertyHandle> PropertyHandle : PropertyHandles)
			{
				PropertyHandle->NotifyPreChange();
			}

			RawComponent->RemoveParameterOverride(ParameterKey);

			for (TSharedPtr<IPropertyHandle> PropertyHandle : PropertyHandles)
			{
				PropertyHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
			}

			OnRebuild.ExecuteIfBound();
			bResettingToDefault = false;
		}
	}

	EVisibility GetResetToDefaultVisibility() const 
	{
		UNiagaraComponent* RawComponent = Component.Get();
		if (RawComponent != nullptr)
		{
			return RawComponent->HasParameterOverride(ParameterKey) ? EVisibility::Visible : EVisibility::Hidden;
		}
		return EVisibility::Hidden;
	}

	FNiagaraVariant FindExistingOverride() const
	{
		UNiagaraComponent* RawComponent = Component.Get();
		if (RawComponent != nullptr)
		{
			return RawComponent->FindParameterOverride(ParameterKey);
		}
		return FNiagaraVariant();
	}

	void OnParameterPreChange()
	{
		UNiagaraComponent* RawComponent = Component.Get();
		if (RawComponent != nullptr)
		{
			RawComponent->Modify();

			for (TSharedPtr<IPropertyHandle> PropertyHandle : PropertyHandles)
			{
				PropertyHandle->NotifyPreChange();
			}
		}
	}

	void OnParameterChanged()
	{
		if (bResettingToDefault)
		{
			return;
		}

		UNiagaraComponent* RawComponent = Component.Get();
		if (RawComponent != nullptr)
		{
			RawComponent->SetParameterOverride(ParameterKey, ParameterValue);

			for (TSharedPtr<IPropertyHandle> PropertyHandle : PropertyHandles)
			{
				PropertyHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
			}
		}
	}

	void OnAssetSelectedFromPicker(const FAssetData& InAssetData)
	{
		UNiagaraComponent* RawComponent = Component.Get();
		if (RawComponent != nullptr)
		{
			UObject* Asset = InAssetData.GetAsset();
			if (Asset == nullptr || Asset->GetClass()->IsChildOf(ParameterKey.GetType().GetClass()))
			{
				FScopedTransaction ScopedTransaction(LOCTEXT("ChangeAsset", "Change asset"));
				RawComponent->Modify();
				RawComponent->SetParameterOverride(ParameterKey, FNiagaraVariant(Asset));

				for (TSharedPtr<IPropertyHandle> PropertyHandle : PropertyHandles)
				{
					PropertyHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
				}
			}
		}
	}

	FString GetCurrentAssetPath() const
	{
		UObject* CurrentObject = nullptr;

		UNiagaraComponent* RawComponent = Component.Get();
		if (RawComponent != nullptr)
		{
			FNiagaraVariant CurrentValue = FindExistingOverride();
			if (CurrentValue.IsValid())
			{
				CurrentObject = CurrentValue.GetUObject();
			}
			else
			{
				// fetch from asset
				UNiagaraSystem* System = RawComponent->GetAsset();
				if (System != nullptr)
				{
					FNiagaraUserRedirectionParameterStore& AssetParamStore = System->GetExposedParameters();
					CurrentObject = AssetParamStore.GetUObject(ParameterKey);
				}
			}
		}

		return CurrentObject != nullptr ? CurrentObject->GetPathName() : FString();
	}


	const FNiagaraVariableBase& Key() const { return ParameterKey; }
	FNiagaraVariant& Value() { return ParameterValue; }


private:
	TWeakObjectPtr<UNiagaraComponent> Component;
	TArray<TSharedPtr<IPropertyHandle>> PropertyHandles;
	FNiagaraVariableBase ParameterKey;
	FNiagaraVariant ParameterValue;
	FSimpleDelegate OnRebuild;
	bool bResettingToDefault;
};

class FNiagaraComponentNodeBuilder : public IDetailCustomNodeBuilder
{
public:
	FNiagaraComponentNodeBuilder(UNiagaraComponent* InComponent, TArray<TSharedPtr<IPropertyHandle>> InOverridePropertyHandles) 
	{
		OverridePropertyHandles = InOverridePropertyHandles;

		Component = InComponent;
		Component->OnSynchronizedWithAssetParameters().AddRaw(this, &FNiagaraComponentNodeBuilder::ComponentSynchronizedWithAssetParameters);

		//UE_LOG(LogNiagaraEditor, Log, TEXT("FNiagaraComponentNodeBuilder %p Component %p"), this, Component.Get());
	}

	~FNiagaraComponentNodeBuilder()
	{
		if (Component.IsValid())
		{
			Component->OnSynchronizedWithAssetParameters().RemoveAll(this);
		}

		//UE_LOG(LogNiagaraEditor, Log, TEXT("~FNiagaraComponentNodeBuilder %p Component %p"), this, Component.Get());
	}

	virtual void SetOnRebuildChildren(FSimpleDelegate InOnRegenerateChildren) override
	{
		OnRebuildChildren = InOnRegenerateChildren;
	}

	virtual void GenerateHeaderRowContent(FDetailWidgetRow& NodeRow) {}
	virtual void Tick(float DeltaTime) override {}
	virtual bool RequiresTick() const override { return false; }
	virtual bool InitiallyCollapsed() const { return false; }
	virtual FName GetName() const  override
	{
		static const FName NiagaraComponentNodeBuilder("FNiagaraComponentNodeBuilder");
		return NiagaraComponentNodeBuilder;
	}

	virtual void GenerateChildContent(IDetailChildrenBuilder& ChildrenBuilder) override
	{
		check(Component.IsValid());

		ParameterProxies.Reset();

		FNiagaraEditorModule& NiagaraEditorModule = FModuleManager::GetModuleChecked<FNiagaraEditorModule>("NiagaraEditor");

		UNiagaraSystem* SystemAsset = Component->GetAsset();
		if (SystemAsset == nullptr)
		{
			return;
		}

		TArray<FNiagaraVariable> UserParameters;
		SystemAsset->GetExposedParameters().GetUserParameters(UserParameters);

		ParameterProxies.Reserve(UserParameters.Num());

		for (const FNiagaraVariable& Parameter : UserParameters)
		{
			TSharedPtr<SWidget> NameWidget;

			NameWidget =
				SNew(STextBlock)
				.TextStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.ParameterText")
				.Text(FText::FromName(Parameter.GetName()));

			IDetailPropertyRow* Row = nullptr;

			TSharedPtr<SWidget> CustomValueWidget;
			
			FNiagaraVariant ParameterValue = GetCurrentParameterValue(Parameter, Component.Get());
			if (!ParameterValue.IsValid())
			{
				continue;
			}

			if (Parameter.IsDataInterface())
			{
				ParameterValue = FNiagaraVariant(DuplicateObject(ParameterValue.GetDataInterface(), Component.Get()));
			}

			TSharedPtr<FNiagaraParameterProxy> ParameterProxy = ParameterProxies.Add_GetRef(MakeShareable(new FNiagaraParameterProxy(Component, Parameter, ParameterValue, OnRebuildChildren, OverridePropertyHandles)));
				
			if (Parameter.IsDataInterface())
			{
				// duplicate the DI here so that if it is changed, the component's SetParameterOverride will override the value
				// if no changes are made, then it'll just be the same as the asset
				TArray<UObject*> Objects { ParameterProxy->Value().GetDataInterface() };

				FAddPropertyParams Params = FAddPropertyParams()
					.UniqueId(Parameter.GetName())
					.AllowChildren(true)
					.CreateCategoryNodes(false);

				Row = ChildrenBuilder.AddExternalObjectProperty(Objects, NAME_None, Params); 

				CustomValueWidget =
					SNew(STextBlock)
					.TextStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.ParameterText")
					.Text(FText::FromString(FName::NameToDisplayString(Parameter.GetType().GetClass()->GetName(), false)));
			}
			else if (Parameter.IsUObject())
			{
				TArray<UObject*> Objects { ParameterProxy->Value().GetUObject() };

				// How do I set this up so I can have this pick actors from the level?

				FAddPropertyParams Params = FAddPropertyParams()
					.UniqueId(Parameter.GetName())
					.AllowChildren(false) // Don't show the material's properties
					.CreateCategoryNodes(false);

				Row = ChildrenBuilder.AddExternalObjectProperty(Objects, NAME_None, Params);

				CustomValueWidget = SNew(SObjectPropertyEntryBox)
					.ObjectPath(ParameterProxy.ToSharedRef(), &FNiagaraParameterProxy::GetCurrentAssetPath)
					.AllowedClass(Parameter.GetType().GetClass())
					.OnObjectChanged(ParameterProxy.ToSharedRef(), &FNiagaraParameterProxy::OnAssetSelectedFromPicker)
					.AllowClear(false)
					.DisplayUseSelected(true)
					.DisplayBrowse(true)
					.DisplayThumbnail(true)
					.NewAssetFactories(TArray<UFactory*>());
			}
			else
			{
				TSharedPtr<FStructOnScope> StructOnScope = MakeShareable(new FStructOnScope(Parameter.GetType().GetStruct(), ParameterProxy->Value().GetBytes()));

				FAddPropertyParams Params = FAddPropertyParams()
					.UniqueId(Parameter.GetName());

				Row = ChildrenBuilder.AddExternalStructureProperty(StructOnScope.ToSharedRef(), NAME_None, Params);
			}

			check(Row && ParameterProxy.IsValid() && ParameterProxy->Value().IsValid());

			TSharedPtr<SWidget> DefaultNameWidget;
			TSharedPtr<SWidget> DefaultValueWidget;

			Row->DisplayName(FText::FromName(Parameter.GetName()));

			FDetailWidgetRow& CustomWidget = Row->CustomWidget(true);

			Row->GetDefaultWidgets(DefaultNameWidget, DefaultValueWidget, CustomWidget);

			Row->GetPropertyHandle()->SetOnPropertyValuePreChange(FSimpleDelegate::CreateSP(ParameterProxy.ToSharedRef(), &FNiagaraParameterProxy::OnParameterPreChange));
			Row->GetPropertyHandle()->SetOnChildPropertyValuePreChange(FSimpleDelegate::CreateSP(ParameterProxy.ToSharedRef(), &FNiagaraParameterProxy::OnParameterPreChange));
			Row->GetPropertyHandle()->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(ParameterProxy.ToSharedRef(), &FNiagaraParameterProxy::OnParameterChanged));
			Row->GetPropertyHandle()->SetOnChildPropertyValueChanged(FSimpleDelegate::CreateSP(ParameterProxy.ToSharedRef(), &FNiagaraParameterProxy::OnParameterChanged));
			Row->GetPropertyHandle()->SetOnPropertyResetToDefault(FSimpleDelegate::CreateSP(ParameterProxy.ToSharedRef(), &FNiagaraParameterProxy::OnResetToDefault));

			CustomWidget
				.NameContent()
				[
					SNew(SBox)
					.Padding(FMargin(0.0f, 2.0f))
					[
						NameWidget.ToSharedRef()
					]
				];

			TSharedPtr<SWidget> ValueWidget = DefaultValueWidget;
			if (CustomValueWidget.IsValid())
			{
				ValueWidget = CustomValueWidget;
			}

			CustomWidget
				.ValueContent()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.HAlign(HAlign_Fill)
					.Padding(4.0f)
					[
						// Add in the parameter editor factoried above.
						ValueWidget.ToSharedRef()
					]
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.AutoWidth()
					[
						// Add in the "reset to default" buttons
						SNew(SButton)
						.OnClicked(ParameterProxy.ToSharedRef(), &FNiagaraParameterProxy::OnResetToDefaultClicked)
						.Visibility(ParameterProxy.ToSharedRef(), &FNiagaraParameterProxy::GetResetToDefaultVisibility)
						.ContentPadding(FMargin(5.f, 0.f))
						.ToolTipText(LOCTEXT("ResetToDefaultToolTip", "Reset to Default"))
						.ButtonStyle(FEditorStyle::Get(), "NoBorder")
						.Content()
						[
							SNew(SImage)
							.Image(FEditorStyle::GetBrush("PropertyWindow.DiffersFromDefault"))
						]
					]
				];
		}
	}

private:

	void ComponentSynchronizedWithAssetParameters()
	{
		OnRebuildChildren.ExecuteIfBound();
	}

private:
	TWeakObjectPtr<UNiagaraComponent> Component;
	TArray<TSharedPtr<IPropertyHandle>> OverridePropertyHandles;
	FSimpleDelegate OnRebuildChildren;
	TArray<TSharedPtr<FNiagaraParameterProxy>> ParameterProxies;
};

TSharedRef<IDetailCustomization> FNiagaraComponentDetails::MakeInstance()
{
	return MakeShareable(new FNiagaraComponentDetails);
}

FNiagaraComponentDetails::FNiagaraComponentDetails() : Builder(nullptr)
{
}

FNiagaraComponentDetails::~FNiagaraComponentDetails()
{
	if (GEngine)
	{
		GEngine->OnWorldDestroyed().RemoveAll(this);
	}

	FGameDelegates::Get().GetEndPlayMapDelegate().RemoveAll(this);
}

void FNiagaraComponentDetails::OnPiEEnd()
{
	UE_LOG(LogNiagaraEditor, Log, TEXT("onPieEnd"));
	if (Component.IsValid())
	{
		if (Component->GetOutermost()->HasAnyPackageFlags(PKG_PlayInEditor))
		{
			UE_LOG(LogNiagaraEditor, Log, TEXT("onPieEnd - has package flags"));
			UWorld* TheWorld = UWorld::FindWorldInPackage(Component->GetOutermost());
			if (TheWorld)
			{
				OnWorldDestroyed(TheWorld);
			}
		}
	}
}

void FNiagaraComponentDetails::OnWorldDestroyed(class UWorld* InWorld)
{
	// We have to clear out any temp data interfaces that were bound to the component's package when the world goes away or otherwise
	// we'll report GC leaks..
	if (Component.IsValid())
	{
		if (Component->GetWorld() == InWorld)
		{
			UE_LOG(LogNiagaraEditor, Log, TEXT("OnWorldDestroyed - matched up"));
			Builder = nullptr;
		}
	}
}

void FNiagaraComponentDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	Builder = &DetailBuilder;

	static const FName ParamCategoryName = TEXT("NiagaraComponent_Parameters");
	static const FName ScriptCategoryName = TEXT("Parameters");

	TSharedPtr<IPropertyHandle> LocalOverridesPropertyHandle = DetailBuilder.GetProperty("OverrideParameters");
	if (LocalOverridesPropertyHandle.IsValid())
	{
		LocalOverridesPropertyHandle->MarkHiddenByCustomization();
	}

	TSharedPtr<IPropertyHandle> TemplateParameterOverridesPropertyHandle = DetailBuilder.GetProperty("TemplateParameterOverrides");
	TemplateParameterOverridesPropertyHandle->MarkHiddenByCustomization();

	TSharedPtr<IPropertyHandle> InstanceParameterOverridesPropertyHandle = DetailBuilder.GetProperty("InstanceParameterOverrides");
	InstanceParameterOverridesPropertyHandle->MarkHiddenByCustomization();

	TArray<TSharedPtr<IPropertyHandle>> PropertyHandles { TemplateParameterOverridesPropertyHandle, InstanceParameterOverridesPropertyHandle };

	TArray<TWeakObjectPtr<UObject>> ObjectsCustomized;
	DetailBuilder.GetObjectsBeingCustomized(ObjectsCustomized);

	if (ObjectsCustomized.Num() == 1 && ObjectsCustomized[0]->IsA<UNiagaraComponent>())
	{
		Component = CastChecked<UNiagaraComponent>(ObjectsCustomized[0].Get());

		if (GEngine)
		{
			GEngine->OnWorldDestroyed().AddRaw(this, &FNiagaraComponentDetails::OnWorldDestroyed);
		}

		FGameDelegates::Get().GetEndPlayMapDelegate().AddRaw(this, &FNiagaraComponentDetails::OnPiEEnd);
			
		IDetailCategoryBuilder& InputParamCategory = DetailBuilder.EditCategory(ParamCategoryName, LOCTEXT("ParamCategoryName", "Override Parameters"));
		InputParamCategory.AddCustomBuilder(MakeShared<FNiagaraComponentNodeBuilder>(Component.Get(), PropertyHandles));
	}
	else if (ObjectsCustomized.Num() > 1)
	{
		IDetailCategoryBuilder& InputParamCategory = DetailBuilder.EditCategory(ParamCategoryName, LOCTEXT("ParamCategoryName", "Override Parameters"));
		InputParamCategory.AddCustomRow(LOCTEXT("ParamCategoryName", "Override Parameters"))
			.WholeRowContent()
			[
				SNew(STextBlock)
				.TextStyle(FEditorStyle::Get(), "SmallText")
				.Text(LOCTEXT("OverrideParameterMultiselectionUnsupported", "Multiple override parameter sets cannot be edited simultaneously."))
			];
	}
}

#undef LOCTEXT_NAMESPACE
