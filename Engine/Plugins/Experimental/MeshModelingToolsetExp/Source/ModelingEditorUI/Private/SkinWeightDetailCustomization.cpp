// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkinWeightDetailCustomization.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "SkeletalMesh/SkinWeightsPaintTool.h"
#include "SSkinWeightProfileImportOptions.h"
#include "UObject/UnrealTypePrivate.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SButton.h"

#define LOCTEXT_NAMESPACE "SkinWeightToolSettingsEditor"

void FSkinWeightDetailCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> DetailObjects;
	DetailBuilder.GetObjectsBeingCustomized(DetailObjects);

	// should be impossible to get multiple settings objects for a single tool
	ensure(DetailObjects.Num()==1);
	SkinToolSettings = Cast<USkinWeightsPaintToolProperties>(DetailObjects[0]);

	// layout constants
	constexpr float WeightSliderWidths = 150.0f;
	constexpr float WeightEditingLabelsPercent = 0.40f;
	constexpr float WeightEditVerticalPadding = 4.0f;

	// custom display of falloff mode as segmented toggle buttons
	IDetailCategoryBuilder& EditModeCategory = DetailBuilder.EditCategory("Weight Editing Mode", FText::GetEmpty(), ECategoryPriority::Important);

	// add segmented control toggle for brush falloff modes ("Surface" or "Volume")
	EditModeCategory.AddCustomRow(LOCTEXT("EditModeCategory", "Weight Editing Mode"), false)
	.WholeRowContent()
	[
		SNew(SBox)
		.Padding(2.0f)
		[
			SNew(SSegmentedControl<EWeightEditMode>)
			.ToolTipText(LOCTEXT("EditingModeTooltip",
					"Brush: the interactive viewport brush interface for editing weights.\n"
					"Selection: the 1-off editing operations for weights on selected elements (bones and vertices).\n"))
			.Value_Lambda([this]()
			{
				return SkinToolSettings->EditingMode;
			})
			.OnValueChanged_Lambda([this](EWeightEditMode Mode)
			{
				SkinToolSettings->EditingMode = Mode;
				SkinToolSettings->WeightTool->ToggleEditingMode();
			})
			+SSegmentedControl<EWeightEditMode>::Slot(EWeightEditMode::Brush)
			.Text(LOCTEXT("BrushEditMode", "Brush"))
			+ SSegmentedControl<EWeightEditMode>::Slot(EWeightEditMode::Selection)
			.Text(LOCTEXT("SelectionEditMode", "Selection"))
		]
	];
	
	// custom display of falloff mode as segmented toggle buttons
	IDetailCategoryBuilder& BrushCategory = DetailBuilder.EditCategory("Brush", FText::GetEmpty(), ECategoryPriority::Important);

	// add segmented control toggle for brush behavior modes ("Add", "Replace", etc..)
	BrushCategory.AddCustomRow(LOCTEXT("BrushModeCategory", "Brush Mode"), false)
	.WholeRowContent()
	[
		SNew(SBox)
		.Padding(2.0f)
		[
			SNew(SSegmentedControl<EWeightEditOperation>)
			.ToolTipText(LOCTEXT("WeightEditModeTooltip",
					"Add: applies the current weight PLUS the strength value to the new weight.\n"
					"Remove: applies the current weight MINUS the strength value to the new weight.\n"
					"Multiply: applies the current weight MULTIPLIED by the strength value to the new weight.\n"
					"Relax: applies the average of the connected (by edge) vertex weights to the new vertex weight, blended by the strength.\n"))
			.IsEnabled_Lambda([this]()
			{
				return SkinToolSettings->EditingMode == EWeightEditMode::Brush;
			})
			.Value_Lambda([this]()
			{
				return SkinToolSettings->BrushMode;
			})
			.OnValueChanged_Lambda([this](EWeightEditOperation Mode)
			{
				SkinToolSettings->BrushMode = Mode;
			})
			+SSegmentedControl<EWeightEditOperation>::Slot(EWeightEditOperation::Add)
			.Text(LOCTEXT("BrushAddMode", "Add"))
			+ SSegmentedControl<EWeightEditOperation>::Slot(EWeightEditOperation::Replace)
			.Text(LOCTEXT("BrushReplaceMode", "Replace"))
			+ SSegmentedControl<EWeightEditOperation>::Slot(EWeightEditOperation::Multiply)
			.Text(LOCTEXT("BrushMultiplyMode", "Multiply"))
			+ SSegmentedControl<EWeightEditOperation>::Slot(EWeightEditOperation::Relax)
			.Text(LOCTEXT("BrushRelaxMode", "Relax"))
		]
	];

	// add segmented control toggle for brush falloff modes ("Surface" or "Volume")
	BrushCategory.AddCustomRow(LOCTEXT("BrushFalloffModeCategory", "Brush Falloff Mode"), false)
	.WholeRowContent()
	[
		SNew(SBox)
		.Padding(2.0f)
		[
			SNew(SSegmentedControl<EWeightBrushFalloffMode>)
			.ToolTipText(LOCTEXT("BrushFalloffModeTooltip",
					"Surface: falloff is based on the distance along the surface from the brush center to nearby connected vertices.\n"
					"Volume: falloff is based on the straight-line distance from the brush center to surrounding vertices.\n"))
			.IsEnabled_Lambda([this]()
			{
				return SkinToolSettings->EditingMode == EWeightEditMode::Brush;
			})
			.Value_Lambda([this]()
			{
				return SkinToolSettings->FalloffMode;
			})
			.OnValueChanged_Lambda([this](EWeightBrushFalloffMode Mode)
			{
				SkinToolSettings->FalloffMode = Mode;
				SkinToolSettings->bColorModeChanged = true;
			})
			+SSegmentedControl<EWeightBrushFalloffMode>::Slot(EWeightBrushFalloffMode::Surface)
			.Text(LOCTEXT("SurfaceMode", "Surface"))
			+ SSegmentedControl<EWeightBrushFalloffMode>::Slot(EWeightBrushFalloffMode::Volume)
			.Text(LOCTEXT("VolumeMode", "Volume"))
		]
	];

	BrushCategory.AddCustomRow(LOCTEXT("BrushSizeCategory", "Brush Radius"), false)
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("BrushRadiusLabel", "Radius"))
		.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
		.ToolTipText(LOCTEXT("BrushRadiusTooltip", "The radius of the brush in scene units."))
	]
	.ValueContent()
	[
		SNew(SSpinBox<float>)
		.MinValue(0.01f)
		.MaxSliderValue(20.f)
		.Value(10.0f)
		.SupportDynamicSliderMaxValue(true)
		.IsEnabled_Lambda([this]()
		{
			return SkinToolSettings->EditingMode == EWeightEditMode::Brush;
		})
		.Value_Lambda([this]()
		{
			return SkinToolSettings->BrushRadius;
		})
		.OnValueChanged_Lambda([this](float NewValue)
		{
			SkinToolSettings->BrushRadius = NewValue;
			FPropertyChangedEvent PropertyChangedEvent(UBrushBaseProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UBrushBaseProperties, BrushRadius)));
			SkinToolSettings->PostEditChangeProperty(PropertyChangedEvent);
		})
	];

	BrushCategory.AddCustomRow(LOCTEXT("BrushStrengthCategory", "Brush Strength"), false)
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("BrushStrengthLabel", "Strength"))
		.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
		.ToolTipText(LOCTEXT("BrushStrengthTooltip", "The strength of the effect on the weights. Exact effect depends on the Brush mode."))
	]
	.ValueContent()
	[
		SNew(SSpinBox<float>)
		.MinValue(0.f)
		.MaxValue(2.0f)
		.MaxSliderValue(1.f)
		.Value(1.0f)
		.SupportDynamicSliderMaxValue(true)
		.IsEnabled_Lambda([this]()
		{
			return SkinToolSettings->EditingMode == EWeightEditMode::Brush;
		})
		.Value_Lambda([this]()
		{
			return SkinToolSettings->BrushStrength;
		})
		.OnValueChanged_Lambda([this](float NewValue)
		{
			SkinToolSettings->BrushStrength = NewValue;
			FPropertyChangedEvent PropertyChangedEvent(UBrushBaseProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UBrushBaseProperties, BrushStrength)));
			SkinToolSettings->PostEditChangeProperty(PropertyChangedEvent);
		})
	];

	BrushCategory.AddCustomRow(LOCTEXT("BrushFalloffCategory", "Brush Falloff"), false)
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("BrushFalloffLabel", "Falloff"))
		.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
		.ToolTipText(LOCTEXT("BrushFalloffTooltip", "At 0, the brush has no falloff. At 1 it has exponential falloff."))
	]
	.ValueContent()
	[
		SNew(SSpinBox<float>)
		.MinValue(0.f)
		.MaxValue(1.f)
		.IsEnabled_Lambda([this]()
		{
			return SkinToolSettings->EditingMode == EWeightEditMode::Brush;
		})
		.Value_Lambda([this]()
		{
			return SkinToolSettings->BrushFalloffAmount;
		})
		.OnValueChanged_Lambda([this](float NewValue)
		{
			SkinToolSettings->BrushFalloffAmount = NewValue;
			FPropertyChangedEvent PropertyChangedEvent(UBrushBaseProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UBrushBaseProperties, BrushFalloffAmount)));
			SkinToolSettings->PostEditChangeProperty(PropertyChangedEvent);
		})
	];

	// custom display of weight editing tools
	IDetailCategoryBuilder& EditWeightsCategory = DetailBuilder.EditCategory("EditWeights", FText::GetEmpty(), ECategoryPriority::Important);
	EditWeightsCategory.InitiallyCollapsed(true);

	// AVERAGE/RELAX/NORMALIZE WEIGHTS category
	EditWeightsCategory.AddCustomRow(LOCTEXT("NormalizeWeightsRow", "Normalize"), false)
	.WholeRowContent()
	[
		SNew(SHorizontalBox)

		+SHorizontalBox::Slot()
		.Padding(2.f, WeightEditVerticalPadding)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Text(LOCTEXT("AverageWeightsButtonLabel", "Average"))
			.ToolTipText(LOCTEXT("AverageWeightsTooltip",
					"Takes the average of vertex weights and applies it to the selected vertices.\n"
					"This command operates on the selected bone(s) and selected vertices.\n "
					"If no bones are selected, ALL bone weights are considered.\n "
					"If no vertices are selected, ALL vertices are considered."))
			.IsEnabled_Lambda([this]()
			{
				return SkinToolSettings->EditingMode == EWeightEditMode::Selection;
			})
			.OnClicked_Lambda([this]()
			{
				SkinToolSettings->WeightTool->AverageWeights();
				return FReply::Handled();
			})
		]

		+SHorizontalBox::Slot()
		.Padding(2.f, WeightEditVerticalPadding)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Text(LOCTEXT("NormalizeWeightsButtonLabel", "Normalize"))
			.ToolTipText(LOCTEXT("NormalizeWeightsTooltip",
					"Forces the weights on the selected vertices to sum to 1.\n"
					"This command operates on the selected vertices.\n "
					"If no vertices are selected, ALL vertices are considered."))
			.IsEnabled_Lambda([this]()
			{
				return SkinToolSettings->EditingMode == EWeightEditMode::Selection;
			})
			.OnClicked_Lambda([this]()
			{
				SkinToolSettings->WeightTool->NormalizeWeights();
				return FReply::Handled();
			})
		]
	];

	// MIRROR WEIGHTS category
	EditWeightsCategory.AddCustomRow(LOCTEXT("MirrorWeightsRow", "Mirror"), false)
	.WholeRowContent()
	[
		SNew(SVerticalBox)
		
		+ SVerticalBox::Slot()
		.Padding(0.f, WeightEditVerticalPadding)
		[
			SNew(SHorizontalBox)
			
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.FillWidth(WeightEditingLabelsPercent)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("MirrorPlaneLabel", "Mirror Plane"))
				.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				.ToolTipText(LOCTEXT("MirrorPlaneTooltip", "The plane to copy weights across."))
			]

			+SHorizontalBox::Slot()
			.FillWidth(1.f)
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				[
					SNew(SSegmentedControl<EAxis::Type>)
					.ToolTipText(LOCTEXT("MirrorAxisTooltip",
						"X: copies weights across the YZ plane.\n"
						"Y: copies weights across the XZ plane.\n"
						"Z: copies weights across the XY plane."))
					.IsEnabled_Lambda([this]()
					{
						return SkinToolSettings->EditingMode == EWeightEditMode::Selection;
					})
					.Value_Lambda([this]()
					{
						return SkinToolSettings->MirrorAxis;
					})
					.OnValueChanged_Lambda([this](EAxis::Type Mode)
					{
						SkinToolSettings->MirrorAxis = Mode;
					})
					+ SSegmentedControl<EAxis::Type>::Slot(EAxis::X)
					.Text(LOCTEXT("MirrorXLabel", "X"))
					+ SSegmentedControl<EAxis::Type>::Slot(EAxis::Y)
					.Text(LOCTEXT("MirrorYLabel", "Y"))
					+ SSegmentedControl<EAxis::Type>::Slot(EAxis::Z)
					.Text(LOCTEXT("MirrorZLabel", "Z"))
				]
					
				+SHorizontalBox::Slot()
				[
					SNew(SSegmentedControl<EMirrorDirection>)
					.ToolTipText(LOCTEXT("MirrorDirectionTooltip", "The direction that determines what side of the plane to copy weights from."))
					.IsEnabled_Lambda([this]()
					{
						return SkinToolSettings->EditingMode == EWeightEditMode::Selection;
					})
					.Value_Lambda([this]()
					{
						return SkinToolSettings->MirrorDirection;
					})
					.OnValueChanged_Lambda([this](EMirrorDirection Mode)
					{
						SkinToolSettings->MirrorDirection = Mode;
					})
					+ SSegmentedControl<EMirrorDirection>::Slot(EMirrorDirection::PositiveToNegative)
					.Text(LOCTEXT("MirrorPosToNegLabel", "+ to -"))
					+ SSegmentedControl<EMirrorDirection>::Slot(EMirrorDirection::NegativeToPositive)
					.Text(LOCTEXT("MirrorNegToPosLabel", "- to +"))
				]
			]
		]

		+SVerticalBox::Slot()
		.Padding(0.f, WeightEditVerticalPadding)
		[
			SNew(SBox)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.Text(LOCTEXT("MirrorWeightsButtonLabel", "Mirror"))
				.ToolTipText(LOCTEXT("MirrorButtonTooltip",
					"Weights are copied across the given plane in the given direction.\n"
					"This command operates on the selected bone(s) and selected vertices.\n "
					"If no bones are selected, ALL bone weights are considered.\n "
					"If no vertices are selected, ALL vertices are considered."))
				.IsEnabled_Lambda([this]()
				{
					return SkinToolSettings->EditingMode == EWeightEditMode::Selection;
				})
				.OnClicked_Lambda([this]()
				{
					SkinToolSettings->WeightTool->MirrorWeights(SkinToolSettings->MirrorAxis, SkinToolSettings->MirrorDirection);
					return FReply::Handled();
				})
			]
		]
	];

	// FLOOD WEIGHTS category
	EditWeightsCategory.AddCustomRow(LOCTEXT("FloodWeightsRow", "Flood"), false)
	.WholeRowContent()
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.Padding(0.f, WeightEditVerticalPadding)
		[
			SNew(SHorizontalBox)
				
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.FillWidth(WeightEditingLabelsPercent)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("FloodAmountLabel", "Flood Amount"))
				.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				.ToolTipText(LOCTEXT("FloodAmountTooltip", "The amount of weight to apply in Flood operation."))
			]

			+SHorizontalBox::Slot()
			.MaxWidth(WeightSliderWidths)
			[
				SNew(SSpinBox<float>)
				.MinValue(0.f)
				.MaxValue(2.0f)
				.MaxSliderValue(1.f)
				.Value(1.0f)
				.SupportDynamicSliderMaxValue(true)
				.IsEnabled_Lambda([this]()
				{
					return SkinToolSettings->EditingMode == EWeightEditMode::Selection;
				})
				.Value_Lambda([this]()
				{
					return SkinToolSettings->FloodValue;
				})
				.OnValueChanged_Lambda([this](float NewValue)
				{
					SkinToolSettings->FloodValue = NewValue;
				})
			]
		]

		+ SVerticalBox::Slot()
		.Padding(0.f, WeightEditVerticalPadding)
		[
			SNew(SHorizontalBox)
				
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.FillWidth(WeightEditingLabelsPercent)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("FloodOperationLabel", "Flood Operation"))
				.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				.ToolTipText(LOCTEXT("FloodOperationTooltip", "The operation to perform when Flood button is pressed."))
			]

			+SHorizontalBox::Slot()
			.FillWidth(1.f)
			[
				SNew(SSegmentedControl<EWeightEditOperation>)
				.ToolTipText(LOCTEXT("FloodOperationButtonTooltip",
					"Add: applies the current weight PLUS the flood value to the new weight.\n"
					"Remove: applies the current weight MINUS the flood value to the new weight.\n"
					"Multiply: applies the current weight MULTIPLIED by the flood value to the new weight.\n"
					"Relax: applies the average of the connected (by edge) vertex weights to the new vertex weight.\n"))
				.IsEnabled_Lambda([this]()
				{
					return SkinToolSettings->EditingMode == EWeightEditMode::Selection;
				})
				.Value_Lambda([this]()
				{
					return SkinToolSettings->FloodMode;
				})
				.OnValueChanged_Lambda([this](EWeightEditOperation Mode)
				{
					SkinToolSettings->FloodMode = Mode;
				})
				+SSegmentedControl<EWeightEditOperation>::Slot(EWeightEditOperation::Add)
				.Text(LOCTEXT("FloodAddMode", "Add"))
				+ SSegmentedControl<EWeightEditOperation>::Slot(EWeightEditOperation::Replace)
				.Text(LOCTEXT("FloodReplaceMode", "Replace"))
				+ SSegmentedControl<EWeightEditOperation>::Slot(EWeightEditOperation::Multiply)
				.Text(LOCTEXT("FloodMultiplyMode", "Multiply"))
				+ SSegmentedControl<EWeightEditOperation>::Slot(EWeightEditOperation::Relax)
				.Text(LOCTEXT("FloodRelaxMode", "Relax"))
			]
		]
		
		+SVerticalBox::Slot()
		.Padding(0.f, WeightEditVerticalPadding)
		[
			SNew(SBox)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.Text(LOCTEXT("FloodWeightsButtonLabel", "Flood"))
				.ToolTipText(LOCTEXT("FloodButtonTooltip",
					"Modifies the vertex weights according to the chosen operation. See the operation tooltip for details.\n"
					"This command operates on the selected bone(s) and selected vertices.\n"
					"If no bones are selected, ALL bones are considered.\n"
					"If no vertices are selected, ALL vertices are considered."))
				.IsEnabled_Lambda([this]()
				{
					return SkinToolSettings->EditingMode == EWeightEditMode::Selection;
				})
				.OnClicked_Lambda([this]()
				{
					SkinToolSettings->WeightTool->FloodWeights(SkinToolSettings->FloodValue, SkinToolSettings->FloodMode);
					return FReply::Handled();
				})
			]
		]
	];

	// PRUNE WEIGHTS category
	EditWeightsCategory.AddCustomRow(LOCTEXT("PruneWeightsRow", "Prune"), false)
	.WholeRowContent()
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.Padding(0.f, WeightEditVerticalPadding)
		[
			SNew(SHorizontalBox)
					
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.FillWidth(WeightEditingLabelsPercent)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("PruneThresholdLabel", "Prune Threshold"))
				.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				.ToolTipText(LOCTEXT("PruneThresholdTooltip", "The threshold weight value to use with Prune operation."))
			]

			+SHorizontalBox::Slot()
			.MaxWidth(WeightSliderWidths)
			[
				SNew(SSpinBox<float>)
				.MinValue(0.f)
				.MaxValue(1.f)
				.IsEnabled_Lambda([this]()
				{
					return SkinToolSettings->EditingMode == EWeightEditMode::Selection;
				})
				.Value_Lambda([this]()
				{
					return SkinToolSettings->PruneValue;
				})
				.OnValueChanged_Lambda([this](float NewValue)
				{
					SkinToolSettings->PruneValue = NewValue;
				})
			]
		]
		
		+SVerticalBox::Slot()
		.Padding(0.f, WeightEditVerticalPadding)
		[
			SNew(SBox)
			.MinDesiredWidth(WeightSliderWidths)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.Text(LOCTEXT("PruneWeightsButtonLabel", "Prune"))
				.ToolTipText(LOCTEXT("PruneButtonTooltip",
					"Weights below the given threshold value are removed.\n"
					"This command operates on the selected bone(s) and selected vertices.\n "
					"If no bones are selected, ALL bone weights are considered.\n "
					"If no vertices are selected, ALL vertices are considered."))
				.IsEnabled_Lambda([this]()
				{
					return SkinToolSettings->EditingMode == EWeightEditMode::Selection;
				})
				.OnClicked_Lambda([this]()
				{
					SkinToolSettings->WeightTool->PruneWeights(SkinToolSettings->PruneValue);
					return FReply::Handled();
				})
			]
		]
	];
	
	// COLOR MODE category
	IDetailCategoryBuilder& WeightColorsCategory = DetailBuilder.EditCategory("WeightColors", FText::GetEmpty(), ECategoryPriority::Important);
	WeightColorsCategory.InitiallyCollapsed(true);
	WeightColorsCategory.AddCustomRow(LOCTEXT("ColorModeCategory", "Color Mode"), false)
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("ColorModeLabel", "Color Mode"))
		.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
		.ToolTipText(LOCTEXT("ColorModeTooltip", "Determines how the weight colors are displayed."))
	]
	.ValueContent()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		[
			SNew(SBox)
			.Padding(2.0f)
			.HAlign(HAlign_Center)
			[
				SNew(SSegmentedControl<EWeightColorMode>)
				.Value_Lambda([this]()
				{
					return SkinToolSettings->ColorMode;
				})
				.OnValueChanged_Lambda([this](EWeightColorMode Mode)
				{
					SkinToolSettings->ColorMode = Mode;
					SkinToolSettings->bColorModeChanged = true;
				})
				+ SSegmentedControl<EWeightColorMode>::Slot(EWeightColorMode::Greyscale)
				.Text(LOCTEXT("GreyscaleMode", "Greyscale"))
				+SSegmentedControl<EWeightColorMode>::Slot(EWeightColorMode::ColorRamp)
				.Text(LOCTEXT("RampMode", "Color Ramp"))
			]
		]
	];

	// hide all base brush properties that have been customized
	const TSharedRef<IPropertyHandle> BrushModeHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(USkinWeightsPaintToolProperties, BrushMode));
	DetailBuilder.HideProperty(BrushModeHandle);
	const TSharedRef<IPropertyHandle> BrushSizeHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UBrushBaseProperties, BrushSize), UBrushBaseProperties::StaticClass());
	DetailBuilder.HideProperty(BrushSizeHandle);
	const TSharedRef<IPropertyHandle> BrushStrengthHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UBrushBaseProperties, BrushStrength), UBrushBaseProperties::StaticClass());
	DetailBuilder.HideProperty(BrushStrengthHandle);
	const TSharedRef<IPropertyHandle> BrushFalloffHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UBrushBaseProperties, BrushFalloffAmount), UBrushBaseProperties::StaticClass());
	DetailBuilder.HideProperty(BrushFalloffHandle);
	const TSharedRef<IPropertyHandle> BrushRadiusHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UBrushBaseProperties, BrushRadius), UBrushBaseProperties::StaticClass());
	DetailBuilder.HideProperty(BrushRadiusHandle);
	const TSharedRef<IPropertyHandle> SpecifyRadiusHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UBrushBaseProperties, bSpecifyRadius), UBrushBaseProperties::StaticClass());
	DetailBuilder.HideProperty(SpecifyRadiusHandle);
	const TSharedRef<IPropertyHandle> FalloffPropHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(USkinWeightsPaintToolProperties, FalloffMode));
	DetailBuilder.HideProperty(FalloffPropHandle);
	const TSharedRef<IPropertyHandle> EditModePropHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(USkinWeightsPaintToolProperties, EditingMode));
	DetailBuilder.HideProperty(EditModePropHandle);
	const TSharedRef<IPropertyHandle> ColorModePropHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(USkinWeightsPaintToolProperties, ColorMode));
	DetailBuilder.HideProperty(ColorModePropHandle);
}

#undef LOCTEXT_NAMESPACE