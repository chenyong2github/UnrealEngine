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

	/// <summary>
	/// Delegate to invoke to sanitize a loctext default value
	/// </summary>
	/// <param name="Property">Property in question</param>
	/// <param name="DefaultValueReader">The default value</param>
	/// <param name="MacroToken">Token for the loctext type being parsed</param>
	/// <param name="InnerDefaultValue">Output sanitized value.</param>
	/// <returns>True if sanitized, false if not.</returns>
	public delegate bool UhtLocTextDefaultValueDelegate(UhtTextProperty Property, IUhtTokenReader DefaultValueReader, ref UhtToken MacroToken, StringBuilder InnerDefaultValue);

	/// <summary>
	/// Attribute defining the loctext sanitizer
	/// </summary>
	[AttributeUsage(AttributeTargets.Method, AllowMultiple = true)]
	public sealed class UhtLocTextDefaultValueAttribute : Attribute
	{

		/// <summary>
		/// Name of the sanitizer (i.e. LOCTEXT, NSLOCTEXT, ...)
		/// </summary>
		public string? Name;
	}

	/// <summary>
	/// Loctext sanitizer
	/// </summary>
	public struct UhtLocTextDefaultValue
	{
		/// <summary>
		/// Delegate to invoke
		/// </summary>
		public UhtLocTextDefaultValueDelegate Delegate;
	}

	/// <summary>
	/// Table of loctext sanitizers
	/// </summary>
	public class UhtLocTextDefaultValueTable
	{

		private readonly Dictionary<StringView, UhtLocTextDefaultValue> LocTextDefaultValues = new Dictionary<StringView, UhtLocTextDefaultValue>();

		/// <summary>
		/// Return the loc text default value associated with the given name
		/// </summary>
		/// <param name="Name"></param>
		/// <param name="LocTextDefaultValue">Loc text default value handler</param>
		/// <returns></returns>
		public bool TryGet(StringView Name, out UhtLocTextDefaultValue LocTextDefaultValue)
		{
			return this.LocTextDefaultValues.TryGetValue(Name, out LocTextDefaultValue);
		}

		/// <summary>
		/// Handle a loctext default value attribute
		/// </summary>
		/// <param name="Type">Containing type</param>
		/// <param name="MethodInfo">Method info</param>
		/// <param name="LocTextDefaultValueAttribute">Defining attribute</param>
		/// <exception cref="UhtIceException">Thrown if the attribute isn't properly defined</exception>
		public void OnLocTextDefaultValueAttribute(Type Type, MethodInfo MethodInfo, UhtLocTextDefaultValueAttribute LocTextDefaultValueAttribute)
		{
			if (string.IsNullOrEmpty(LocTextDefaultValueAttribute.Name))
			{
				throw new UhtIceException("A loc text default value attribute must have a name");
			}

			UhtLocTextDefaultValue LocTextDefaultValue = new UhtLocTextDefaultValue
			{
				Delegate = (UhtLocTextDefaultValueDelegate)Delegate.CreateDelegate(typeof(UhtLocTextDefaultValueDelegate), MethodInfo)
			};

			this.LocTextDefaultValues.Add(new StringView(LocTextDefaultValueAttribute.Name), LocTextDefaultValue);
		}
	}
}
