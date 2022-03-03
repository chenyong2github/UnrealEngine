// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Utils;
using System;
using System.Collections.Generic;

namespace EpicGames.UHT.Tables
{

	/// <summary>
	/// This attribute is placed on classes that represent Unreal Engine classes.
	/// </summary>
	[AttributeUsage(AttributeTargets.Class, AllowMultiple = true)]
	public class UhtEngineClassAttribute : Attribute
	{

		/// <summary>
		/// The name of the engine class excluding any prefix
		/// </summary>
		public string? Name = null;

		/// <summary>
		/// If true, this class is a property
		/// </summary>
		public bool IsProperty = false;
	}

	/// <summary>
	/// Represents an engine class in the engine class table
	/// </summary>
	public struct UhtEngineClass
	{
		/// <summary>
		/// The name of the engine class excluding any prefix
		/// </summary>
		public string Name;

		/// <summary>
		/// If true, this class is a property
		/// </summary>
		public bool bIsProperty;
	}

	/// <summary>
	/// Table of all known engine class names.
	/// </summary>
	public class UhtEngineClassTable
	{
		/// <summary>
		/// Global instance of the engine class table
		/// </summary>
		public static UhtEngineClassTable Instance = new UhtEngineClassTable();

		/// <summary>
		/// Internal mapping from engine class name to information
		/// </summary>
		private Dictionary<StringView, UhtEngineClass> EngineClasses = new Dictionary<StringView, UhtEngineClass>();

		/// <summary>
		/// Test to see if the given class name is a property
		/// </summary>
		/// <param name="Name">Name of the class without the prefix</param>
		/// <returns>True if the class name is a property.  False if the class name isn't a property or isn't an engine class.</returns>
		public bool IsValidPropertyTypeName(StringView Name)
		{
			if (this.EngineClasses.TryGetValue(Name, out UhtEngineClass EngineClass))
			{
				return EngineClass.bIsProperty;
			}
			return false;
		}

		/// <summary>
		/// Add an entry to the table
		/// </summary>
		/// <param name="Type">Type associated with the given attribute</param>
		/// <param name="EngineClassAttribute">The located attribute</param>
		public void OnEngineClassAttribute(Type Type, UhtEngineClassAttribute EngineClassAttribute)
		{
			if (string.IsNullOrEmpty(EngineClassAttribute.Name))
			{
				throw new UhtIceException("EngineClassNames must have a name specified");
			}
			this.EngineClasses.Add(EngineClassAttribute.Name, new UhtEngineClass { Name = EngineClassAttribute.Name, bIsProperty = EngineClassAttribute.IsProperty });
		}
	}
}
