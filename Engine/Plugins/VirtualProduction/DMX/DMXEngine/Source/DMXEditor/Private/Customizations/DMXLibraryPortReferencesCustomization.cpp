// Copyright Epic Games, Inc. All Rights Reserved.

#include "Customizations/DMXLibraryPortReferencesCustomization.h"

#include "IO/DMXPortManager.h"
#include "IO/DMXInputPort.h"
#include "IO/DMXInputPortReference.h"
#include "IO/DMXOutputPort.h"
#include "IO/DMXOutputPortReference.h"
#include "IO/DMXPortManager.h"
#include "Library/DMXLibrary.h"

#include "DetailWidgetRow.h"
#include "EditorStyleSet.h"
#include "DetailCategoryBuilder.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h" 
#include "ScopedTransaction.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/STextBlock.h" 


#define LOCTEXT_NAMESPACE "DMXLibraryPortReferencesCustomization"

namespace
{
	/** Widget that displays a single port reference */
	template <typename PortReferenceType>
	class SDMXLibraryPortReferenceCustomization
		: public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SDMXLibraryPortReferenceCustomization<PortReferenceType>)
		{}
		
			SLATE_ARGUMENT(TSharedPtr<IPropertyHandle>, PortReferenceHandle)

		SLATE_END_ARGS()

		/** Constructs the widget */
		void Construct(const FArguments& InArgs)
		{
			check(InArgs._PortReferenceHandle.IsValid() && InArgs._PortReferenceHandle->IsValidHandle());

			PortReferenceHandle = InArgs._PortReferenceHandle;

			PortReferenceType PortReference = GetPortReferenceChecked();

			const FDMXPortSharedRef& Port = FDMXPortManager::Get().FindPortByGuidChecked(PortReference.GetPortGuid());
			bool bEnabledFlagSet = PortReference.IsEnabledFlagSet();

			ChildSlot
			[
				SNew(SVerticalBox)

				// Port name
				+ SVerticalBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Top)
				.Padding(FMargin(4.f, 4.f, 4.f, 4.f))
				[
					SNew(STextBlock)
					.ToolTipText(LOCTEXT("PortReferenceEnabledTextTooltip", "Enables or disables the port for the Library"))
					.Font(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
					.Text_Lambda([Port]() -> FText
					{
						return FText::FromString(Port->GetPortName());
					})
				]

				// Enabled checkbox
				+ SVerticalBox::Slot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Top)
				.Padding(FMargin(4.f, 4.f, 4.f, 4.f))
				.AutoHeight()
				[
					SNew(SHorizontalBox)

					// Enabled checkbox
					+ SHorizontalBox::Slot()
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Top)
					.AutoWidth()
					.Padding(FMargin(16.f, 0.f, 4.f, 0.f))
					[
						SNew(STextBlock)
						.Font(FEditorStyle::GetFontStyle("PropertyWindow.NormalFont"))
						.Text(LOCTEXT("PortReferenceEnabledLabel", "Enabled"))
					]

					+ SHorizontalBox::Slot()
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Top)
					[
						SNew(SCheckBox)
						.ToolTipText(LOCTEXT("PortReferenceEnabledCheckboxTooltip", "Enables or disables the port for the Library"))
						.IsChecked(bEnabledFlagSet)
						.OnCheckStateChanged(this, &SDMXLibraryPortReferenceCustomization<PortReferenceType>::OnPortReferenceChangedEnabledState)
					]
				]
			];
		}
		

	private:
		/** Called when the checkbox state changed */
		void OnPortReferenceChangedEnabledState(ECheckBoxState CheckBoxState)
		{
			check(PortReferenceHandle.IsValid());

			TSharedPtr<IPropertyHandle> PortReferenceEnabledFlagHandle = PortReferenceHandle->GetChildHandle(PortReferenceType::GetEnabledFlagPropertyName());
			check(PortReferenceEnabledFlagHandle.IsValid());

			if (CheckBoxState == ECheckBoxState::Checked)
			{
				PortReferenceEnabledFlagHandle->SetValue(true);
			}
			else
			{
				PortReferenceEnabledFlagHandle->SetValue(false);
			}
		}

		/** Returns the port reference from the handle */
		const PortReferenceType& GetPortReferenceChecked()
		{
			check(PortReferenceHandle.IsValid());

			TArray<void*> RawData;
			PortReferenceHandle->AccessRawData(RawData);

			// Multi editing isn't possible in arrays, so there should be always exactly one element
			check(RawData.Num() == 1);

			return *static_cast<PortReferenceType*>(RawData[0]);
		}

		/** Handle to the port reference this widget draws */
		TSharedPtr<IPropertyHandle> PortReferenceHandle;
	};
}


void FDMXLibraryPortReferencesCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	const bool bDisplayResetToDefault = false;
	const FText DisplayNameOverride = LOCTEXT("HeaderDisplayName", "Ports");
	const FText DisplayToolTipOverride = LOCTEXT("HeaderToolTip", "The ports to be used with this Library.");

	HeaderRow
		.NameContent()
		[
			StructPropertyHandle->CreatePropertyNameWidget(DisplayNameOverride, DisplayToolTipOverride, bDisplayResetToDefault)
		];
}

void FDMXLibraryPortReferencesCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	LibraryPortReferencesHandle = StructPropertyHandle;

	// Expand the category
	ChildBuilder.GetParentCategory().InitiallyCollapsed(false);

	// Hide the 'reset to default' option
	LibraryPortReferencesHandle->MarkResetToDefaultCustomized();

	// Customize input port refrences view in the ports struct
	ChildBuilder		
		.AddCustomRow(LOCTEXT("InputPortReferenceTitleRowSearchString", "InputPorts"))
		.NameContent()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		[
			SNew(STextBlock)
			.Font(FEditorStyle::GetFontStyle("PropertyWindow.NormalFont"))
			.Text(LOCTEXT("InputPortsLabel", "Input Ports"))
		]
		.ValueContent()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Top)
		[
			SAssignNew(InputPortReferenceContentBorder, SBorder)
			.BorderImage(FEditorStyle::GetBrush("NoBorder"))
		];

	ChildBuilder
		.AddCustomRow(LOCTEXT("SeparatorSearchString", "Separator"))
		.WholeRowContent()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Top)
		[
			SNew(SSeparator)
			.Orientation(EOrientation::Orient_Horizontal)
		];

	
	// Customize output port refrences view in the ports struct
	ChildBuilder
		.AddCustomRow(LOCTEXT("OutputPortReferenceTitleRowSearchString", "OutputPorts"))
		.NameContent()
		.HAlign(HAlign_Left)
		[
			SNew(STextBlock)
			.Font(FEditorStyle::GetFontStyle("PropertyWindow.NormalFont"))
			.Text(LOCTEXT("OutputPortsLabel", "Output Ports"))
		]
		.ValueContent()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Top)
		[
			SAssignNew(OutputPortReferenceContentBorder, SBorder)
			.BorderImage(FEditorStyle::GetBrush("NoBorder"))
		];

	// Bind to port reference array changes
	TSharedPtr<IPropertyHandle> InputPortReferencesHandle = LibraryPortReferencesHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXLibraryPortReferences, InputPortReferences));
	TSharedPtr<IPropertyHandleArray> InputPortReferencesHandleArray = InputPortReferencesHandle->AsArray();
	check(InputPortReferencesHandleArray.IsValid());

	FSimpleDelegate OnInputPortArrayChangedDelegate = FSimpleDelegate::CreateSP(this, &FDMXLibraryPortReferencesCustomization::RefreshPortReferenceWidgets);
	InputPortReferencesHandleArray->SetOnNumElementsChanged(OnInputPortArrayChangedDelegate);

	TSharedPtr<IPropertyHandle> OutputPortReferencesHandle = LibraryPortReferencesHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXLibraryPortReferences, OutputPortReferences));
	TSharedPtr<IPropertyHandleArray> OutputPortReferencesHandleArray = OutputPortReferencesHandle->AsArray();
	check(OutputPortReferencesHandleArray.IsValid());

	FSimpleDelegate OnOutputPortArrayChangedDelegate = FSimpleDelegate::CreateSP(this, &FDMXLibraryPortReferencesCustomization::RefreshPortReferenceWidgets);
	OutputPortReferencesHandleArray->SetOnNumElementsChanged(OnOutputPortArrayChangedDelegate);

	// Create content widgets
	RefreshPortReferenceWidgets();
}

void FDMXLibraryPortReferencesCustomization::RefreshPortReferenceWidgets()
{
	check(InputPortReferenceContentBorder.IsValid());
	check(OutputPortReferenceContentBorder.IsValid());

	// Add the Input Port Reference widgets
	TSharedRef<SVerticalBox> InputPortReferencesWidget = SNew(SVerticalBox);
	
	TSharedPtr<IPropertyHandle> InputPortReferencesHandle = LibraryPortReferencesHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXLibraryPortReferences, InputPortReferences));
	TSharedPtr<IPropertyHandleArray> InputPortReferencesHandleArray = InputPortReferencesHandle->AsArray();
	check(InputPortReferencesHandleArray.IsValid());

	uint32 NumInputPortRefElements = 0;
	InputPortReferencesHandleArray->GetNumElements(NumInputPortRefElements);
	for (uint32 IndexElement = 0; IndexElement < NumInputPortRefElements; IndexElement++)
	{
		TSharedRef<IPropertyHandle> InputPortReferenceHandle = InputPortReferencesHandleArray->GetElement(IndexElement);

		
		InputPortReferencesWidget->AddSlot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		.AutoHeight()
		[
			SNew(SDMXLibraryPortReferenceCustomization<FDMXInputPortReference>)
			.PortReferenceHandle(InputPortReferenceHandle)
		];
	}

	InputPortReferenceContentBorder->SetContent(InputPortReferencesWidget);

	// Add the Output Port Reference widgets
	TSharedRef<SVerticalBox> OutputPortReferencesWidget = SNew(SVerticalBox);

	TSharedPtr<IPropertyHandle> OutputPortReferencesHandle = LibraryPortReferencesHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXLibraryPortReferences, OutputPortReferences));
	TSharedPtr<IPropertyHandleArray> OutputPortReferencesHandleArray = OutputPortReferencesHandle->AsArray();
	check(OutputPortReferencesHandleArray.IsValid());

	uint32 NumOutputPortRefElements = 0;
	OutputPortReferencesHandleArray->GetNumElements(NumOutputPortRefElements);
	for (uint32 IndexElement = 0; IndexElement < NumOutputPortRefElements; IndexElement++)
	{
		TSharedRef<IPropertyHandle> OutputPortReferenceHandle = OutputPortReferencesHandleArray->GetElement(IndexElement);

		OutputPortReferencesWidget->AddSlot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			.AutoHeight()
			[
				SNew(SDMXLibraryPortReferenceCustomization<FDMXOutputPortReference>)
				.PortReferenceHandle(OutputPortReferenceHandle)
			];
	}

	OutputPortReferenceContentBorder->SetContent(OutputPortReferencesWidget);

}

#undef LOCTEXT_NAMESPACE
