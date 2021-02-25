// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Components/Widget.h"
#include "CoreMinimal.h"
#include "DetailLayoutBuilder.h"
#include "IDetailCustomization.h"
#include "IPropertyTypeCustomization.h"
#include "Layout/Visibility.h"
#include "MetasoundAssetBase.h"
#include "MetasoundEditorGraphNode.h"
#include "MetasoundEditorGraphInputNodes.h"
#include "MetasoundUObjectRegistry.h"
#include "Types/SlateEnums.h"
#include "UObject/NameTypes.h"
#include "Widgets/Input/SEditableTextBox.h"

// Forward Declarations
class FDetailWidgetRow;
class FPropertyRestriction;
class IDetailLayoutBuilder;
class IPropertyHandle;

namespace Metasound
{
	namespace Editor
	{
		class FMetasoundInputNodeArrayDetailCustomizationBase : public IPropertyTypeCustomization
		{
		public:
			//~ Begin IPropertyTypeCustomization
			virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
			virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
			//~ End IPropertyTypeCustomization

		protected:
			virtual TSharedRef<SWidget> CreateStructureWidget(TSharedPtr<IPropertyHandle>& PropertyHandle) const = 0;
			virtual void CacheProxyData(TSharedPtr<IPropertyHandle> ProxyHandle) { }

		private:
			TSharedRef<SWidget> CreateValueWidget(TSharedPtr<IPropertyHandleArray> ParentArrayProperty, TSharedPtr<IPropertyHandle> StructPropertyHandle, bool bIsInArray) const;
		};

		class FMetasoundInputNodeIntDetailCustomization : public FMetasoundInputNodeArrayDetailCustomizationBase
		{
		protected:
			virtual TSharedRef<SWidget> CreateStructureWidget(TSharedPtr<IPropertyHandle>& StructPropertyHandle) const override;
			virtual void CacheProxyData(TSharedPtr<IPropertyHandle> ProxyHandle) override;

		private:
			FName DataTypeName;
		};

		class FMetasoundInputNodeObjectDetailCustomization : public FMetasoundInputNodeArrayDetailCustomizationBase
		{
		protected:
			virtual TSharedRef<SWidget> CreateStructureWidget(TSharedPtr<IPropertyHandle>& StructPropertyHandle) const override;
			virtual void CacheProxyData(TSharedPtr<IPropertyHandle> ProxyHandle) override;

		private:
			TWeakObjectPtr<UClass> ProxyGenClass;
		};

		// TODO: This is templated to easily make a customization for outputs once
		// widget support is added.
		template <typename NodeType>
		class TMetasoundNodeVariableDetailCustomization : public IDetailCustomization
		{
		public:
			TMetasoundNodeVariableDetailCustomization()
				: IDetailCustomization()
			{
			}

		protected:
			TWeakObjectPtr<NodeType> GraphNode;
			TSharedPtr<SEditableTextBox> DisplayNameEditableTextBox;
			bool bIsNameInvalid = false;

			// IDetailCustomization interface
			virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override
			{
				TArray<TWeakObjectPtr<UObject>> Objects;
				DetailLayout.GetObjectsBeingCustomized(Objects);
				if (Objects.IsEmpty())
				{
					return;
				}

				GraphNode = Cast<NodeType>(Objects[0].Get());
			}
			// End of IDetailCustomization interface


			virtual bool IsRequired() const = 0;

			virtual FText GetDisplayName() const = 0;
			virtual void OnDisplayNameChanged(const FText& InNewText) = 0;
			virtual void OnDisplayNameCommitted(const FText& InNewName, ETextCommit::Type InTextCommit) = 0;

			virtual FText GetTooltip() const = 0;
			virtual void OnTooltipCommitted(const FText& InNewName, ETextCommit::Type InTextCommit) = 0;

			ECheckBoxState OnGetPrivateCheckboxState() const
			{
				if (GraphNode.IsValid())
				{
					return GraphNode->GetNodeHandle()->GetNodeStyle().bIsPrivate ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				}

				return ECheckBoxState::Unchecked;
			}

			void OnPrivateChanged(ECheckBoxState InNewState)
			{
				if (GraphNode.IsValid())
				{
					const bool bIsChecked = InNewState == ECheckBoxState::Checked;
					Frontend::FNodeHandle NodeHandle = GraphNode->GetNodeHandle();
					FMetasoundFrontendNodeStyle NodeStyle = NodeHandle->GetNodeStyle();
					NodeStyle.bIsPrivate = bIsChecked;
					NodeHandle->SetNodeStyle(NodeStyle);
				}
			}

			EVisibility ExposePrivateVisibility() const
			{
				if (IsRequired())
				{
					return EVisibility::Collapsed;
				}

				return EVisibility::Visible;
			}

			FMetasoundAssetBase* GetMetasoundAsset() const
			{
				if (!GraphNode.IsValid())
				{
					return nullptr;
				}

				UObject& MetasoundObj = GraphNode->GetMetasoundChecked();
				if (FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&MetasoundObj))
				{
					return MetasoundAsset;
				}

				return nullptr;
			}
		};

		class FMetasoundInputNodeDetailCustomization : public TMetasoundNodeVariableDetailCustomization<UMetasoundEditorGraphInputNode>
		{
		public:
			FMetasoundInputNodeDetailCustomization()
				: TMetasoundNodeVariableDetailCustomization<UMetasoundEditorGraphInputNode>()
			{
			}

			// IDetailCustomization interface
			virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;
			// End of IDetailCustomization interface

		protected:
			virtual bool IsRequired() const override;

			virtual FText GetDisplayName() const override;
			virtual void OnDisplayNameChanged(const FText& InNewText) override;
			virtual void OnDisplayNameCommitted(const FText& InNewName, ETextCommit::Type InTextCommit) override;

			virtual FText GetTooltip() const override;
			virtual void OnTooltipCommitted(const FText& InNewName, ETextCommit::Type InTextCommit) override;

		private:
			void SetDefaultPropertyMetaData(TSharedRef<IPropertyHandle> InDefaultPropertyHandle) const;

			FName GetLiteralDataType() const;
		};
	} // namespace Editor
} // namespace Metasound