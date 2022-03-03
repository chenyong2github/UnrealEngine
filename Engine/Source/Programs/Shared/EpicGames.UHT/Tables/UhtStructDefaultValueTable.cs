// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tokenizer;
using System;
using System.Collections.Generic;
using System.Reflection;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;
using System.Text;

namespace EpicGames.UHT.Tables
{
	public delegate bool UhtStructDefaultValueDelegate(UhtStructProperty Property, IUhtTokenReader DefaultValueReader, StringBuilder InnerDefaultValue);

	[Flags]
	public enum UhtStructDefaultValueOptions
	{
		None = 0,

		/// <summary>
		/// This method is to be invoked when there are no keyword matches found
		/// </summary>
		Default = 1 << 2,
	}

	/// <summary>
	/// Helper methods for testing flags.  These methods perform better than the generic HasFlag which hits
	/// the GC and stalls.
	/// </summary>
	public static class UhtStructDefaultValueOptionsExtensions
	{

		/// <summary>
		/// Test to see if any of the specified flags are set
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <returns>True if any of the flags are set</returns>
		public static bool HasAnyFlags(this UhtStructDefaultValueOptions InFlags, UhtStructDefaultValueOptions TestFlags)
		{
			return (InFlags & TestFlags) != 0;
		}

		/// <summary>
		/// Test to see if all of the specified flags are set
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <returns>True if all the flags are set</returns>
		public static bool HasAllFlags(this UhtStructDefaultValueOptions InFlags, UhtStructDefaultValueOptions TestFlags)
		{
			return (InFlags & TestFlags) == TestFlags;
		}

		/// <summary>
		/// Test to see if a specific set of flags have a specific value.
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <param name="MatchFlags">Expected value of the tested flags</param>
		/// <returns>True if the given flags have a specific value.</returns>
		public static bool HasExactFlags(this UhtStructDefaultValueOptions InFlags, UhtStructDefaultValueOptions TestFlags, UhtStructDefaultValueOptions MatchFlags)
		{
			return (InFlags & TestFlags) == MatchFlags;
		}
	}

	[AttributeUsage(AttributeTargets.Method, AllowMultiple = true)]
	public class UhtStructDefaultValueAttribute : Attribute
	{
		public string? Name;
		public UhtStructDefaultValueOptions Options = UhtStructDefaultValueOptions.None;
	}

	public struct UhtStructDefaultValue
	{
		public UhtStructDefaultValueDelegate Delegate;
	}

	public class UhtStructDefaultValueTable
	{
		public static UhtStructDefaultValueTable Instance = new UhtStructDefaultValueTable();

		private Dictionary<StringView, UhtStructDefaultValue> StructDefaultValues = new Dictionary<StringView, UhtStructDefaultValue>();
		private UhtStructDefaultValue? DefaultInternal = null;
		public UhtStructDefaultValue Default
		{
			get
			{
				if (this.DefaultInternal == null)
				{
					throw new UhtIceException("No struct default value has been marked as default");
				}
				return (UhtStructDefaultValue)this.DefaultInternal;
			}
		}

		/// <summary>
		/// Return the structure default value associated with the given name
		/// </summary>
		/// <param name="Name"></param>
		/// <param name="StructDefaultValue">Structure default value handler</param>
		/// <returns></returns>
		public bool TryGet(StringView Name, out UhtStructDefaultValue StructDefaultValue)
		{
			return this.StructDefaultValues.TryGetValue(Name, out StructDefaultValue);
		}

		public void OnStructDefaultValueAttribute(Type Type, MethodInfo MethodInfo, UhtStructDefaultValueAttribute StructDefaultValueAttribute)
		{
			if (string.IsNullOrEmpty(StructDefaultValueAttribute.Name) && !StructDefaultValueAttribute.Options.HasAnyFlags(UhtStructDefaultValueOptions.Default))
			{
				throw new UhtIceException("A struct default value attribute must have a name or be marked as default");
			}

			UhtStructDefaultValue StructDefaultValue = new UhtStructDefaultValue
			{
				Delegate = (UhtStructDefaultValueDelegate)Delegate.CreateDelegate(typeof(UhtStructDefaultValueDelegate), MethodInfo)
			};

			if (StructDefaultValueAttribute.Options.HasAnyFlags(UhtStructDefaultValueOptions.Default))
			{
				if (this.DefaultInternal != null)
				{
					throw new UhtIceException("Only one struct default value attribute dispatcher can be marked as default");
				}
				this.DefaultInternal = StructDefaultValue;
			}
			else if (!string.IsNullOrEmpty(StructDefaultValueAttribute.Name))
			{
				this.StructDefaultValues.Add(new StringView(StructDefaultValueAttribute.Name), StructDefaultValue);
			}
		}
	}
}
