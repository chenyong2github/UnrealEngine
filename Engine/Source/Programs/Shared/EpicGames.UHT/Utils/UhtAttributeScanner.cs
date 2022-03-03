// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;

namespace EpicGames.UHT.Utils
{
	/// <summary>
	/// UnrealHeaderTool avoids hard coding any table contents by using attributes to add entries to the tables.
	/// 
	/// There are two basic styles of tables in UHT.  
	/// 
	/// The first style is just a simple association of a string and an attribute.  For example, 
	/// the engine class table is just a collection of all the engine class names supported by the engine.
	/// 
	/// The second style is a table of tables.  Depending on the context (i.e. is a "class" or "function" 
	/// being parsed), attributes will "extend" a given table adding an entry to that table and every
	/// table the derives from that table.  For example, the Keywords table will add "private" to the 
	/// "ClassBase" table.  Since the "Class", "Interface", and "NativeInterface" tables derive from
	/// "ClassBase", all of those tables will contain the keyword "private".
	/// 
	/// See UhtTables.cs for a list of table names and how they relate to each other.
	/// 
	/// Tables are implemented in the following source files:
	/// 
	///		UhtEngineClassTable.cs - Collection of all the engine class names.
	///		UhtKeywordTable.cs - Collection of the C++ keywords that UHT understands.
	///		UhtLocTextDefaultValueTable.cs - Collection of loctext default value parsing
	///		UhtPropertyTypeTable.cs - Collection of the property type keywords.
	///		UhtSpecifierTable.cs - Collection of the known specifiers
	///		UhtSpecifierValidatorTable.cs - Collection of the specifier validators
	///		UhtStructDefaultValueTable.cs - Collection of structure default value parsing
	/// </summary>
	public class UhtAttributeScanner
	{
		private static readonly Lazy<UhtAttributeScanner> LazyInit = new Lazy<UhtAttributeScanner>(() =>
		{
			UhtAttributeScanner Scanner = new UhtAttributeScanner();
			Scanner.Initialize();
			return Scanner;
		});

		private readonly UhtSpecifierTables SpecifierTables = UhtSpecifierTables.Instance;
		private readonly UhtSpecifierValidatorTables SpecifierValidatorTables = UhtSpecifierValidatorTables.Instance;
		private readonly UhtKeywordTables KeywordTables = UhtKeywordTables.Instance;
		private readonly UhtPropertyTypeTable PropertyTypeTable = UhtPropertyTypeTable.Instance;
		private readonly UhtStructDefaultValueTable StructDefaultValueTable = UhtStructDefaultValueTable.Instance;
		private readonly UhtEngineClassTable EngineClassTable = UhtEngineClassTable.Instance;
		private readonly UhtExporterTable GeneraterTable = UhtExporterTable.Instance;
		private readonly UhtLocTextDefaultValueTable LocTextDefaultValueTable = UhtLocTextDefaultValueTable.Instance;

		/// <summary>
		/// Initialize the attribute scanner and scan all assemblies for attributes
		/// </summary>
		public static void Scan()
		{
			_ = LazyInit.Value;
		}

		/// <summary>
		/// Perform one time initialization of the system
		/// </summary>
		private void Initialize()
		{
			IEnumerable<Assembly> LoadedAssemblies = AppDomain.CurrentDomain.GetAssemblies()
				// Exclude the AutomationTool driver assembly - it contains no tasks, and trying to load it in the context of
				// BuildGraph can result in an exception if Microsoft.Build.Framework is not able to be loaded
				.Where(A => !String.Equals("AutomationTool", A.GetName().Name));

			// Collect all of the types from the loaded assemblies
			foreach (Assembly LoadedAssembly in LoadedAssemblies)
			{
				Type[] Types;
				try
				{
					Types = LoadedAssembly.GetTypes();
				}
				catch (ReflectionTypeLoadException ex)
				{
					Log.TraceWarning("Exception {0} while trying to get types from assembly {1}. LoaderExceptions: {2}", 
						ex, LoadedAssembly, string.Join("\n", ex.LoaderExceptions.Select(x => x?.Message)));
					continue;
				}

				foreach (Type Type in Types)
				{
					CheckForAttributes(Type);
				}
			}

			PerformPostInitialization();
		}

		/// <summary>
		/// For the given type, look for any table related attributes 
		/// </summary>
		/// <param name="Type">Type in question</param>
		private void CheckForAttributes(Type Type)
		{
			if (Type.IsClass)
			{

				// Loop through the attributes
				foreach (Attribute ClassAttribute in Type.GetCustomAttributes(false))
				{
					if (ClassAttribute is UnrealHeaderToolAttribute ParserAttribute)
					{
						HandleUnrealHeaderToolAttribute(Type, ParserAttribute);
					}
					else if (ClassAttribute is UhtEngineClassAttribute EngineClassAttribute)
					{
						this.EngineClassTable.OnEngineClassAttribute(Type, EngineClassAttribute);
					}
				}
			}
		}

		private void PerformPostInitialization()
		{
			this.KeywordTables.Merge();
			this.SpecifierTables.Merge();
			this.SpecifierValidatorTables.Merge();

			// Invoke this to generate an exception if there is no default
			_ = this.PropertyTypeTable.Default;
			_ = this.StructDefaultValueTable.Default;
		}

		private void HandleUnrealHeaderToolAttribute(Type Type, UnrealHeaderToolAttribute ParserAttribute)
		{
			if (!string.IsNullOrEmpty(ParserAttribute.InitMethod))
			{
				MethodInfo? InitInfo = Type.GetMethod(ParserAttribute.InitMethod, BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic);
				if (InitInfo == null)
				{
					throw new Exception($"Unable to find UnrealHeaderTool attribute InitMethod {ParserAttribute.InitMethod}");
				}
				InitInfo.Invoke(null, new Object[] { });
			}

			// Scan the methods for things we are interested in
			foreach (MethodInfo MethodInfo in Type.GetMethods(BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic))
			{

				// Scan for attributes we care about
				foreach (Attribute MethodAttribute in MethodInfo.GetCustomAttributes())
				{
					if (MethodAttribute is UhtSpecifierAttribute SpecifierAttribute)
					{
						this.SpecifierTables.OnSpecifierAttribute(Type, MethodInfo, SpecifierAttribute);
					}
					else if (MethodAttribute is UhtSpecifierValidatorAttribute SpecifierValidatorAttribute)
					{
						this.SpecifierValidatorTables.OnSpecifierValidatorAttribute(Type, MethodInfo, SpecifierValidatorAttribute);
					}
					else if (MethodAttribute is UhtKeywordCatchAllAttribute KeywordCatchAllAttribute)
					{
						this.KeywordTables.OnKeywordCatchAllAttribute(Type, MethodInfo, KeywordCatchAllAttribute);
					}
					else if (MethodAttribute is UhtKeywordAttribute KeywordAttribute)
					{
						this.KeywordTables.OnKeywordAttribute(Type, MethodInfo, KeywordAttribute);
					}
					else if (MethodAttribute is UhtPropertyTypeAttribute PropertyTypeAttribute)
					{
						this.PropertyTypeTable.OnPropertyTypeAttribute(Type, MethodInfo, PropertyTypeAttribute);
					}
					else if (MethodAttribute is UhtStructDefaultValueAttribute StructDefaultValueAttribute)
					{
						this.StructDefaultValueTable.OnStructDefaultValueAttribute(Type, MethodInfo, StructDefaultValueAttribute);
					}
					else if (MethodAttribute is UhtExporterAttribute ExporterAttribute)
					{
						this.GeneraterTable.OnExporterAttribute(Type, MethodInfo, ExporterAttribute);
					}
					else if (MethodAttribute is UhtLocTextDefaultValueAttribute LocTextDefaultValueAttribute)
					{
						this.LocTextDefaultValueTable.OnLocTextDefaultValueAttribute(Type, MethodInfo, LocTextDefaultValueAttribute);
					}
				}
			}
		}
	}

	/// <summary>
	/// This attribute must be applied to any class that contains other UHT attributes.
	/// </summary>
	[AttributeUsage(AttributeTargets.Class)]
	public class UnrealHeaderToolAttribute : Attribute
	{

		/// <summary>
		/// If specified, this method will be invoked once during the scan for attributes.
		/// It can be used to perform some one time initialization.
		/// </summary>
		public string InitMethod = string.Empty;
	}
}
