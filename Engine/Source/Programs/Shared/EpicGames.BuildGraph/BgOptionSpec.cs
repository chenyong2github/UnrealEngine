// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.RegularExpressions;
using EpicGames.BuildGraph.Expressions;

namespace EpicGames.BuildGraph
{
	/// <summary>
	/// Exception thrown if an option fails validation
	/// </summary>
	sealed class BgOptionValidationException : Exception
	{
		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="message"></param>
		public BgOptionValidationException(string message)
			: base(message)
		{
		}
	}

	/// <summary>
	/// Base class for option configuration
	/// </summary>
	public interface IBgOption : IBgExpr
	{
		/// <summary>
		/// Name of the option
		/// </summary>
		string Name { get; }

		/// <summary>
		/// Label to show against the option in the UI
		/// </summary>
		BgString? Label { get; set; }

		/// <summary>
		/// Description for the option
		/// </summary>
		BgString Description { get; set; }
	}

	/// <summary>
	/// A boolean option expression
	/// </summary>
	public class BgBoolOption : BgBool, IBgOption
	{
		/// <inheritdoc/>
		public string Name { get; }

		/// <inheritdoc/>
		public BgString? Label { get; set; }

		/// <inheritdoc/>
		public BgString Description { get; set; }

		/// <summary>
		/// Default value for the option
		/// </summary>
		public BgBool DefaultValue { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		internal BgBoolOption(string name, BgString description, BgBool defaultValue)
		{
			Name = name;
			Description = description;
			DefaultValue = defaultValue;
		}

		/// <inheritdoc/>
		public override bool Compute(BgExprContext context)
		{
			string? value;
			if (context.Options.TryGetValue(Name, out value))
			{
				bool boolValue;
				if (!Boolean.TryParse(value, out boolValue))
				{
					throw new BgOptionValidationException($"Argument for {Name} is not a valid bool ({value})");
				}
				return boolValue;
			}
			return DefaultValue.Compute(context);
		}
	}

	/// <summary>
	/// An integer option expression
	/// </summary>
	public class BgIntOption : BgInt, IBgOption
	{
		/// <inheritdoc/>
		public string Name { get; }

		/// <inheritdoc/>
		public BgString? Label { get; set; }

		/// <inheritdoc/>
		public BgString Description { get; set; }

		/// <summary>
		/// Default value for the option
		/// </summary>
		public BgInt DefaultValue { get; set; }

		/// <summary>
		/// Minimum allowed value
		/// </summary>
		public BgInt? MinValue { get; set; }

		/// <summary>
		/// Maximum allowed value
		/// </summary>
		public BgInt? MaxValue { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		internal BgIntOption(string name, BgString description, BgInt defaultValue)
		{
			Name = name;
			Description = description;
			DefaultValue = defaultValue;
		}

		/// <inheritdoc/>
		public override int Compute(BgExprContext context)
		{
			string? value;
			if (context.Options.TryGetValue(Name, out value))
			{
				int intValue;
				if (!Int32.TryParse(value, out intValue))
				{
					throw new BgOptionValidationException($"Argument for '{Name}' is not a valid integer");
				}
				if (!(MinValue is null))
				{
					int intMinValue = MinValue.Compute(context);
					if (intValue < intMinValue)
					{
						throw new BgOptionValidationException($"Argument for '{Name}' is less than the allowed minimum ({intValue} < {intMinValue})");
					}
				}
				if (!(MaxValue is null))
				{
					int intMaxValue = MaxValue.Compute(context);
					if (intValue > intMaxValue)
					{
						throw new BgOptionValidationException($"Argument for '{Name}' is greater than the allowed maximum ({intValue} > {intMaxValue})");
					}
				}
			}
			return DefaultValue.Compute(context);
		}
	}

	/// <summary>
	/// A string option expression
	/// </summary>
	public class BgEnumOption<TEnum> : BgEnum<TEnum>, IBgOption where TEnum : struct
	{
		/// <inheritdoc/>
		public string Name { get; }

		/// <inheritdoc/>
		public BgString? Label { get; set; }

		/// <inheritdoc/>
		public BgString Description { get; set; }

		/// <summary>
		/// Default value for the option
		/// </summary>
		public BgEnum<TEnum> DefaultValue { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		internal BgEnumOption(string name, BgString description, BgEnum<TEnum> defaultValue)
		{
			Name = name;
			Description = description;
			DefaultValue = defaultValue;
		}

		/// <inheritdoc/>
		public override TEnum Compute(BgExprContext context)
		{
			string? value;
			if (context.Options.TryGetValue(Name, out value))
			{
				TEnum enumValue;
				if (!Enum.TryParse<TEnum>(value, true, out enumValue))
				{
					throw new BgOptionValidationException($"Argument '{Name}' is not a valid value for {typeof(TEnum).Name}");
				}
				return enumValue;
			}
			return DefaultValue.Compute(context);
		}
	}

	/// <summary>
	/// A string option expression
	/// </summary>
	public class BgStringOption : BgString, IBgOption
	{
		/// <inheritdoc/>
		public string Name { get; }

		/// <inheritdoc/>
		public BgString? Label { get; set; }

		/// <inheritdoc/>
		public BgString Description { get; set; }

		/// <summary>
		/// Default value for the option
		/// </summary>
		public BgString DefaultValue { get; set; }

		/// <summary>
		/// Regex for validating values for the option
		/// </summary>
		public BgString? Pattern { get; set; }

		/// <summary>
		/// Message to display if validation fails
		/// </summary>
		public BgString? PatternFailed { get; set; }

		/// <summary>
		/// Allowed values of the option
		/// </summary>
		public BgList<BgString>? Enum { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		internal BgStringOption(string name, BgString description, BgString defaultValue)
		{
			Name = name;
			Description = description;
			DefaultValue = defaultValue;
		}

		/// <inheritdoc/>
		public override string Compute(BgExprContext context)
		{
			string? value;
			if (context.Options.TryGetValue(Name, out value))
			{
				if (!(Pattern is null))
				{
					string patternValue = Pattern.Compute(context);
					if (!Regex.IsMatch(value, patternValue))
					{
						string patternFailedValue = PatternFailed?.Compute(context) ?? $"Argument '{Name}' does not match the required pattern: '{patternValue}'";
						throw new BgOptionValidationException(patternFailedValue);
					}
				}
				if (!(Enum is null))
				{
					List<string> enumValues = Enum.Compute(context);
					if (!enumValues.Any(x => x.Equals(value, StringComparison.OrdinalIgnoreCase)))
					{
						throw new BgOptionValidationException($"Argument '{Name}' is invalid");
					}
				}
				return value;
			}
			return DefaultValue.Compute(context);
		}
	}

	/// <summary>
	/// A list option expression
	/// </summary>
	public class BgEnumListOption<TEnum> : BgList<BgEnum<TEnum>>, IBgOption where TEnum : struct
	{
		/// <inheritdoc/>
		public string Name { get; }

		/// <inheritdoc/>
		public BgString? Label { get; set; }

		/// <inheritdoc/>
		public BgString Description { get; set; }

		/// <summary>
		/// Default value for the option
		/// </summary>
		public BgList<BgEnum<TEnum>> DefaultValue { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		internal BgEnumListOption(string name, BgString description, BgList<BgEnum<TEnum>> defaultValue)
		{
			Name = name;
			Description = description;
			DefaultValue = defaultValue;
		}

		/// <inheritdoc/>
		public override IEnumerable<BgEnum<TEnum>> GetEnumerable(BgExprContext context)
		{
			BgList<BgEnum<TEnum>> value = DefaultValue;
			if (context.Options.TryGetValue(Name, out string? valueText))
			{
				value = BgType.Get<BgList<BgEnum<TEnum>>>().DeserializeArgument(valueText);
			}
			return value.GetEnumerable(context);
		}
	}
}
