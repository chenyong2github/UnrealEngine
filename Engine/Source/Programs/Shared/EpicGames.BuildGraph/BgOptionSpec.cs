// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.BuildGraph.Expressions;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;

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
		/// <param name="Message"></param>
		public BgOptionValidationException(string Message)
			: base(Message)
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
		internal BgBoolOption(string Name, BgString Description, BgBool DefaultValue)
		{
			this.Name = Name;
			this.Description = Description;
			this.DefaultValue = DefaultValue;
		}

		/// <inheritdoc/>
		public override bool Compute(BgExprContext Context)
		{
			string? Value;
			if (Context.Options.TryGetValue(Name, out Value))
			{
				bool BoolValue;
				if (!bool.TryParse(Value, out BoolValue))
				{
					throw new BgOptionValidationException($"Argument for {Name} is not a valid bool ({Value})");
				}
				return BoolValue;
			}
			return DefaultValue.Compute(Context);
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
		internal BgIntOption(string Name, BgString Description, BgInt DefaultValue)
		{
			this.Name = Name;
			this.Description = Description;
			this.DefaultValue = DefaultValue;
		}

		/// <inheritdoc/>
		public override int Compute(BgExprContext Context)
		{
			string? Value;
			if (Context.Options.TryGetValue(Name, out Value))
			{
				int IntValue;
				if (!int.TryParse(Value, out IntValue))
				{
					throw new BgOptionValidationException($"Argument for '{Name}' is not a valid integer");
				}
				if (!ReferenceEquals(MinValue, null))
				{
					int IntMinValue = MinValue.Compute(Context);
					if (IntValue < IntMinValue)
					{
						throw new BgOptionValidationException($"Argument for '{Name}' is less than the allowed minimum ({IntValue} < {IntMinValue})");
					}
				}
				if (!ReferenceEquals(MaxValue, null))
				{
					int IntMaxValue = MaxValue.Compute(Context);
					if (IntValue > IntMaxValue)
					{
						throw new BgOptionValidationException($"Argument for '{Name}' is greater than the allowed maximum ({IntValue} > {IntMaxValue})");
					}
				}
			}
			return DefaultValue.Compute(Context);
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
		internal BgEnumOption(string Name, BgString Description, BgEnum<TEnum> DefaultValue)
		{
			this.Name = Name;
			this.Description = Description;
			this.DefaultValue = DefaultValue;
		}

		/// <inheritdoc/>
		public override TEnum Compute(BgExprContext Context)
		{
			string? Value;
			if (Context.Options.TryGetValue(Name, out Value))
			{
				TEnum EnumValue;
				if (!Enum.TryParse<TEnum>(Value, true, out EnumValue))
				{
					throw new BgOptionValidationException($"Argument '{Name}' is not a valid value for {typeof(TEnum).Name}");
				}
				return EnumValue;
			}
			return DefaultValue.Compute(Context);
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
		internal BgStringOption(string Name, BgString Description, BgString DefaultValue)
		{
			this.Name = Name;
			this.Description = Description;
			this.DefaultValue = DefaultValue;
		}

		/// <inheritdoc/>
		public override string Compute(BgExprContext Context)
		{
			string? Value;
			if (Context.Options.TryGetValue(Name, out Value))
			{
				if (!Object.ReferenceEquals(Pattern, null))
				{
					string PatternValue = Pattern.Compute(Context);
					if (!Regex.IsMatch(Value, PatternValue))
					{
						string PatternFailedValue = PatternFailed?.Compute(Context) ?? $"Argument '{Name}' does not match the required pattern: '{PatternValue}'";
						throw new BgOptionValidationException(PatternFailedValue);
					}
				}
				if (!Object.ReferenceEquals(Enum, null))
				{
					List<string> EnumValues = Enum.Compute(Context);
					if (!EnumValues.Any(x => x.Equals(Value, StringComparison.OrdinalIgnoreCase)))
					{
						throw new BgOptionValidationException($"Argument '{Name}' is invalid");
					}
				}
				return Value;
			}
			return DefaultValue.Compute(Context);
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
		internal BgEnumListOption(string Name, BgString Description, BgList<BgEnum<TEnum>> DefaultValue)
		{
			this.Name = Name;
			this.Description = Description;
			this.DefaultValue = DefaultValue;
		}

		/// <inheritdoc/>
		public override IEnumerable<BgEnum<TEnum>> GetEnumerable(BgExprContext Context)
		{
			BgList<BgEnum<TEnum>> Value = DefaultValue;
			if (Context.Options.TryGetValue(Name, out string? ValueText))
			{
				Value = BgType.Get<BgList<BgEnum<TEnum>>>().DeserializeArgument(ValueText);
			}
			return Value.GetEnumerable(Context);
		}
	}
}
