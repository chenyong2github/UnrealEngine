// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;
using System;
using System.Collections.Generic;
using System.Reflection;

namespace EpicGames.UHT.Tables
{
	[Flags]
	public enum UhtPropertyTypeOptions
	{
		None = 0,

		/// <summary>
		/// Simple property type with just the property type.  (i.e. "int32 MyValue")
		/// Simple types are not required to parse the supplied token list.
		/// </summary>
		Simple = 1 << 0,

		/// <summary>
		/// Use case insensitive string compares
		/// </summary>
		CaseInsensitive = 1 << 1,

		/// <summary>
		/// This property type is to be invoked when there are no keyword matches found
		/// </summary>
		Default = 1 << 2,

		/// <summary>
		/// This property type doesn't reference any engine types an can be resolved immediately
		/// </summary>
		Immediate = 1 << 3,
	}

	/// <summary>
	/// Helper methods for testing flags.  These methods perform better than the generic HasFlag which hits
	/// the GC and stalls.
	/// </summary>
	public static class UhtPropertyTypeOptionsExtensions
	{

		/// <summary>
		/// Test to see if any of the specified flags are set
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <returns>True if any of the flags are set</returns>
		public static bool HasAnyFlags(this UhtPropertyTypeOptions InFlags, UhtPropertyTypeOptions TestFlags)
		{
			return (InFlags & TestFlags) != 0;
		}

		/// <summary>
		/// Test to see if all of the specified flags are set
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <returns>True if all the flags are set</returns>
		public static bool HasAllFlags(this UhtPropertyTypeOptions InFlags, UhtPropertyTypeOptions TestFlags)
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
		public static bool HasExactFlags(this UhtPropertyTypeOptions InFlags, UhtPropertyTypeOptions TestFlags, UhtPropertyTypeOptions MatchFlags)
		{
			return (InFlags & TestFlags) == MatchFlags;
		}
	}

	public enum UhtPropertyResolvePhase
	{
		Parsing,
		Resolving,
	}

	/// <summary>
	/// Delegate invoked to resolve a tokenized type into a UHTProperty type
	/// </summary>
	/// <param name="ResolvePhase">Specifies if this is being resolved during the parsing phase or the resolution phase.  Type lookups can not happen during the parsing phase</param>
	/// <param name="PropertySettings">The configuration of the property</param>
	/// <param name="TokenReader">The token reader containing the type</param>
	/// <param name="MatchedToken">The token that matched the delegate unless the delegate is the default resolver.</param>
	/// <returns></returns>
	public delegate UhtProperty? UhtResolvePropertyDelegate(UhtPropertyResolvePhase ResolvePhase, UhtPropertySettings PropertySettings, IUhtTokenReader TokenReader, UhtToken MatchedToken);

	[AttributeUsage(AttributeTargets.Method, AllowMultiple = true)]
	public class UhtPropertyTypeAttribute : Attribute
	{
		public string? Keyword = null;
		public UhtPropertyTypeOptions Options = UhtPropertyTypeOptions.None;
	}

	/// <summary>
	/// Represents a property type as specified by the PropertyTypeAttribute
	/// </summary>
	public struct UhtPropertyType
	{
		public UhtResolvePropertyDelegate Delegate;
		public UhtPropertyTypeOptions Options;
	}

	public class UhtPropertyTypeTable
	{
		public static UhtPropertyTypeTable Instance = new UhtPropertyTypeTable();

		private Dictionary<StringView, UhtPropertyType> CaseSensitive = new Dictionary<StringView, UhtPropertyType>();
		private Dictionary<StringView, UhtPropertyType> CaseInsensitive = new Dictionary<StringView, UhtPropertyType>(StringViewComparer.OrdinalIgnoreCase);
		private UhtPropertyType? DefaultInternal = null;

		public UhtPropertyType Default
		{
			get
			{
				if (this.DefaultInternal == null)
				{
					throw new UhtIceException("No property type has been marked as default");
				}
				return (UhtPropertyType)this.DefaultInternal;
			}
		}

		/// <summary>
		/// Return the property type associated with the given name
		/// </summary>
		/// <param name="Name"></param>
		/// <param name="PropertyType">Property type if matched</param>
		/// <returns></returns>
		public bool TryGet(StringView Name, out UhtPropertyType PropertyType)
		{
			return
				this.CaseSensitive.TryGetValue(Name, out PropertyType) ||
				this.CaseInsensitive.TryGetValue(Name, out PropertyType);
		}

		public void OnPropertyTypeAttribute(Type Type, MethodInfo MethodInfo, UhtPropertyTypeAttribute PropertyTypeAttribute)
		{
			if (string.IsNullOrEmpty(PropertyTypeAttribute.Keyword) && !PropertyTypeAttribute.Options.HasAnyFlags(UhtPropertyTypeOptions.Default))
			{
				throw new UhtIceException("A property type must have a keyword or be marked as default");
			}

			UhtPropertyType PropertyType = new UhtPropertyType
			{
				Delegate = (UhtResolvePropertyDelegate)Delegate.CreateDelegate(typeof(UhtResolvePropertyDelegate), MethodInfo),
				Options = PropertyTypeAttribute.Options,
			};

			if (PropertyTypeAttribute.Options.HasAnyFlags(UhtPropertyTypeOptions.Default))
			{
				if (this.DefaultInternal != null)
				{
					throw new UhtIceException("Only one property type dispatcher can be marked as default");
				}
				this.DefaultInternal = PropertyType;
			}
			else if (!string.IsNullOrEmpty(PropertyTypeAttribute.Keyword))
			{
				if (PropertyTypeAttribute.Options.HasAnyFlags(UhtPropertyTypeOptions.CaseInsensitive))
				{
					this.CaseInsensitive.Add(PropertyTypeAttribute.Keyword, PropertyType);
				}
				else
				{
					this.CaseSensitive.Add(PropertyTypeAttribute.Keyword, PropertyType);
				}
			}
		}
	}
}
