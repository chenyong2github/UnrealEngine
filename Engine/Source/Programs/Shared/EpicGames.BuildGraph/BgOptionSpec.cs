// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.RegularExpressions;
using EpicGames.BuildGraph.Expressions;

namespace EpicGames.BuildGraph
{
	/// <summary>
	/// A boolean option expression
	/// </summary>
	public class BgBoolOption : BgBool
	{
		/// <summary>
		/// Name of the option
		/// </summary>
		public BgString Name { get; }

		/// <summary>
		/// Label to display next to the option
		/// </summary>
		public BgString? Label { get; }

		/// <summary>
		/// Help text to display for the user
		/// </summary>
		public BgString? Description { get; }

		/// <summary>
		/// Default value for the option
		/// </summary>
		public BgBool? DefaultValue { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public BgBoolOption(BgString name, BgString? description = null, BgBool? defaultValue = null)
			: this(name, null, description, defaultValue)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		public BgBoolOption(BgString name, BgString? label, BgString? description, BgBool? defaultValue)
			: base(BgExprFlags.None)
		{
			Name = name;
			Label = label;
			Description = description;
			DefaultValue = defaultValue;
		}

		/// <inheritdoc/>
		public override void Write(BgBytecodeWriter writer)
		{
			writer.WriteOpcode(BgOpcode.BoolOption);
			writer.WriteExpr(Name);
			writer.WriteExpr(Label ?? BgString.Empty);
			writer.WriteExpr(Description ?? BgString.Empty);
			writer.WriteExpr(DefaultValue ?? BgBool.False);
		}
	}

	/// <summary>
	/// An integer option expression
	/// </summary>
	public class BgIntOption : BgInt
	{
		/// <summary>
		/// Name of the option
		/// </summary>
		public BgString Name { get; }

		/// <summary>
		/// Label to display next to the option
		/// </summary>
		public BgString? Label { get; }

		/// <summary>
		/// Help text to display for the user
		/// </summary>
		public BgString? Description { get; }

		/// <summary>
		/// Default value for the option
		/// </summary>
		public BgInt? DefaultValue { get; }

		/// <summary>
		/// Minimum allowed value
		/// </summary>
		public BgInt? MinValue { get; }

		/// <summary>
		/// Maximum allowed value
		/// </summary>
		public BgInt? MaxValue { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public BgIntOption(string name, BgString? description = null, BgInt? defaultValue = null, BgInt? minValue = null, BgInt? maxValue = null, BgString? label = null)
			: base(BgExprFlags.None)
		{
			Name = name;
			Label = label;
			Description = description;
			DefaultValue = defaultValue;
			MinValue = minValue;
			MaxValue = maxValue;
		}

		/// <inheritdoc/>
		public override void Write(BgBytecodeWriter writer)
		{
			writer.WriteOpcode(BgOpcode.IntOption);
			writer.WriteExpr(Name);
			writer.WriteExpr(Label ?? BgString.Empty);
			writer.WriteExpr(Description ?? BgString.Empty);
			writer.WriteExpr(DefaultValue ?? (BgInt)0);
			writer.WriteExpr(MinValue ?? (BgInt)(-1));
			writer.WriteExpr(MaxValue ?? (BgInt)(-1));
		}
	}

	/// <summary>
	/// Style for a string option
	/// </summary>
	public enum BgStrOptionStyle
	{
		/// <summary>
		/// Free-form text entry
		/// </summary>
		Text,

		/// <summary>
		/// List of options
		/// </summary>
		DropList,
	}

	/// <summary>
	/// A string option expression
	/// </summary>
	public class BgStrOption : BgString
	{
		/// <summary>
		/// Name of the option
		/// </summary>
		public BgString Name { get; }

		/// <summary>
		/// Label to display next to the option
		/// </summary>
		public BgString? Label { get; }

		/// <summary>
		/// Help text to display for the user
		/// </summary>
		public BgString? Description { get; }

		/// <summary>
		/// Default value for the option
		/// </summary>
		public BgString? DefaultValue { get; set; }

		/// <summary>
		/// Style for this option
		/// </summary>
		public BgStrOptionStyle Style { get; }

		/// <summary>
		/// Regex for validating values for the option
		/// </summary>
		public BgString? Pattern { get; set; }

		/// <summary>
		/// Message to display if validation fails
		/// </summary>
		public BgString? PatternFailed { get; set; }

		/// <summary>
		/// List of values to choose from
		/// </summary>
		public BgList<BgString>? Values { get; set; }

		/// <summary>
		/// Matching list of descriptions for each value
		/// </summary>
		public BgList<BgString>? ValueDescriptions { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		public BgStrOption(string name, BgString? label = null, BgString? description = null, BgString? defaultValue = null, BgStrOptionStyle style = BgStrOptionStyle.Text, BgString? pattern = null, BgString? patternFailed = null, BgList<BgString>? values = null, BgList<BgString>? valueDescriptions = null)
			: base(BgExprFlags.None)
		{
			Name = name;
			Label = label;
			Description = description;
			Style = style;
			DefaultValue = defaultValue;
			Pattern = pattern;
			PatternFailed = patternFailed;
			Values = values;
			ValueDescriptions = valueDescriptions;
		}

		/// <inheritdoc/>
		public override void Write(BgBytecodeWriter writer)
		{
			writer.WriteOpcode(BgOpcode.StrOption);
			writer.WriteExpr(Name);
			writer.WriteExpr(Label ?? BgString.Empty);
			writer.WriteExpr(Description ?? BgString.Empty);
			writer.WriteUnsignedInteger((int)Style);
			writer.WriteExpr(DefaultValue ?? BgString.Empty);
			writer.WriteExpr(Pattern ?? BgString.Empty);
			writer.WriteExpr(PatternFailed ?? BgString.Empty);
			writer.WriteExpr(Values ?? BgList<BgString>.Empty);
			writer.WriteExpr(ValueDescriptions ?? BgList<BgString>.Empty);
		}
	}

	/// <summary>
	/// Style for a list option
	/// </summary>
	public enum BgListOptionStyle
	{
		/// <summary>
		/// List of checkboxes
		/// </summary>
		CheckList = 0,

		/// <summary>
		/// Tag picker
		/// </summary>
		TagPicker = 1,
	}

	/// <summary>
	/// A list option expression
	/// </summary>
	public class BgListOptionSpec : BgList<BgString>
	{
		/// <summary>
		/// Name of the option
		/// </summary>
		public BgString Name { get; }

		/// <summary>
		/// Label to display next to the option
		/// </summary>
		public BgString? Label { get; }

		/// <summary>
		/// Help text to display for the user
		/// </summary>
		public BgString? Description { get; }

		/// <summary>
		/// Style for this list box
		/// </summary>
		public BgListOptionStyle Style { get; }

		/// <summary>
		/// Default value for the option
		/// </summary>
		public BgString? DefaultValue { get; set; }

		/// <summary>
		/// Regex for validating values for the option
		/// </summary>
		public BgString? Pattern { get; set; }

		/// <summary>
		/// Message to display if validation fails
		/// </summary>
		public BgString? PatternFailed { get; set; }

		/// <summary>
		/// List of values to choose from
		/// </summary>
		public BgList<BgString>? Values { get; set; }

		/// <summary>
		/// Matching list of descriptions for each value
		/// </summary>
		public BgList<BgString>? ValueDescriptions { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		public BgListOptionSpec(string name, BgString? label = null, BgString? description = null, BgString? defaultValue = null, BgListOptionStyle style = BgListOptionStyle.CheckList, BgString? pattern = null, BgString? patternFailed = null, BgList<BgString>? values = null, BgList<BgString>? valueDescriptions = null)
			: base(BgExprFlags.None)
		{
			Name = name;
			Label = label;
			Description = description;
			DefaultValue = defaultValue;
			Style = style;
			Pattern = pattern;
			PatternFailed = patternFailed;
			Values = values;
			ValueDescriptions = valueDescriptions;
		}

		/// <inheritdoc/>
		public override void Write(BgBytecodeWriter writer)
		{
			writer.WriteOpcode(BgOpcode.ListOption);
			writer.WriteExpr(Name);
			writer.WriteExpr(Label ?? BgString.Empty);
			writer.WriteExpr(Description ?? BgString.Empty);
			writer.WriteUnsignedInteger((int)Style);
			writer.WriteExpr(DefaultValue ?? BgString.Empty);
			writer.WriteExpr(Pattern ?? BgString.Empty);
			writer.WriteExpr(PatternFailed ?? BgString.Empty);
			writer.WriteExpr(Values ?? BgList<BgString>.Empty);
			writer.WriteExpr(ValueDescriptions ?? BgList<BgString>.Empty);
		}
	}
}
