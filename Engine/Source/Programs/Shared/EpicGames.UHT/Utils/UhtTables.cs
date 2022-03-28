// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.Reflection;
using UnrealBuildBase;

namespace EpicGames.UHT.Utils
{

	/// <summary>
	/// Defines all the table names for the standard UHT types
	/// </summary>
	public static class UhtTableNames
	{
		/// <summary>
		/// The class base table is common to UCLASS, UINTERFACE and native interfaces
		/// </summary>
		public const string ClassBase = "ClassBase";

		/// <summary>
		/// Table for UCLASS
		/// </summary>
		public const string Class = "Class";

		/// <summary>
		/// Default table applied to all other tables
		/// </summary>
		public const string Default = "Default";

		/// <summary>
		/// Table for UENUM
		/// </summary>
		public const string Enum = "Enum";

		/// <summary>
		/// Table for all types considered a UField
		/// </summary>
		public const string Field = "Field";

		/// <summary>
		/// Table for all functions
		/// </summary>
		public const string Function = "Function";

		/// <summary>
		/// Table for the global/file scope
		/// </summary>
		public const string Global = "Global";

		/// <summary>
		/// Table for UINTERFACE
		/// </summary>
		public const string Interface = "Interface";

		/// <summary>
		/// Table for interfaces
		/// </summary>
		public const string NativeInterface = "NativeInterface";

		/// <summary>
		/// Table for all UObject types
		/// </summary>
		public const string Object = "Object";

		/// <summary>
		/// Table for properties that are function arguments
		/// </summary>
		public const string PropertyArgument = "PropertyArgument";

		/// <summary>
		/// Table for properties that are struct/class members
		/// </summary>
		public const string PropertyMember = "PropertyMember";

		/// <summary>
		/// Table for USTRUCT
		/// </summary>
		public const string ScriptStruct = "ScriptStruct";

		/// <summary>
		/// Table for all UStruct objects (structs, classes, and functions)
		/// </summary>
		public const string Struct = "Struct";
	}

	/// <summary>
	/// Base class for table lookup system.
	/// </summary>
	public class UhtLookupTableBase
	{
		/// <summary>
		/// This table inherits entries for the given table
		/// </summary>
		public UhtLookupTableBase? ParentTable = null;

		/// <summary>
		/// Name of the table
		/// </summary>
		public string TableName = string.Empty;

		/// <summary>
		/// User facing name of the table
		/// </summary>
		public string UserName
		{
			get => string.IsNullOrEmpty(this.UserNameInternal) ? this.TableName : this.UserNameInternal;
			set => this.UserNameInternal = value;
		}

		/// <summary>
		/// Check to see if the table is internal
		/// </summary>
		public bool Internal = false;

		/// <summary>
		/// If true, this table has been implemented and not just created on demand by another table
		/// </summary>
		public bool Implemented = false;

		/// <summary>
		/// Internal version of the user name.  If it hasn't been set, then the table name will be used
		/// </summary>
		private string UserNameInternal = string.Empty;

		/// <summary>
		/// Merge the lookup table.  Duplicates will be ignored.
		/// </summary>
		/// <param name="BaseTable">Base table being merged</param>
		public virtual void Merge(UhtLookupTableBase BaseTable)
		{
		}

		/// <summary>
		/// Given a method name, try to extract the entry name for a table
		/// </summary>
		/// <param name="ClassType">Class containing the method</param>
		/// <param name="MethodInfo">Method information</param>
		/// <param name="InName">Optional name supplied by the attributes.  If specified, this name will be returned instead of extracted from the method name</param>
		/// <param name="Suffix">Required suffix</param>
		/// <returns>The extracted name or the supplied name</returns>
		public static string GetSuffixedName(Type ClassType, MethodInfo MethodInfo, string? InName, string Suffix)
		{
			string Name = InName ?? string.Empty;
			if (string.IsNullOrEmpty(Name))
			{
				if (MethodInfo.Name.EndsWith(Suffix))
				{
					Name = MethodInfo.Name.Substring(0, MethodInfo.Name.Length - Suffix.Length);
				}
				else
				{
					throw new UhtIceException($"The '{Suffix}' attribute on the {ClassType.Name}.{MethodInfo.Name} method doesn't have a name specified or the method name doesn't end in '{Suffix}'.");
				}
			}
			return Name;
		}
	}

	/// <summary>
	/// Lookup tables provide a method of associating actions with given C++ keyword or UE specifier
	/// </summary>
	/// <typeparam name="TValue">Keyword or specifier information</typeparam>
	public class UhtLookupTable<TValue> : UhtLookupTableBase
	{

		/// <summary>
		/// Lookup dictionary for the specifiers
		/// </summary>
		private Dictionary<StringView, TValue> Lookup;

		/// <summary>
		/// Construct a new table
		/// </summary>
		public UhtLookupTable(StringViewComparer Comparer)
		{
			this.Lookup = new Dictionary<StringView, TValue>(Comparer);
		}

		/// <summary>
		/// Add the given value to the lookup table.  It will throw an exception if it is a duplicate.
		/// </summary>
		/// <param name="Key">Key to be added</param>
		/// <param name="Value">Value to be added</param>
		public UhtLookupTable<TValue> Add(string Key, TValue Value)
		{
			this.Lookup.Add(Key, Value);
			return this;
		}

		/// <summary>
		/// Attempt to fetch the value associated with the key
		/// </summary>
		/// <param name="Key">Lookup key</param>
		/// <param name="Value">Value associated with the key</param>
		/// <returns>True if the key was found, false if not</returns>
		public bool TryGetValue(StringView Key, [MaybeNullWhen(false)] out TValue Value)
		{
			return this.Lookup.TryGetValue(Key, out Value);
		}

		/// <summary>
		/// Merge the lookup table.  Duplicates will be ignored.
		/// </summary>
		/// <param name="BaseTable">Base table being merged</param>
		public override void Merge(UhtLookupTableBase BaseTable)
		{
			foreach (var KVP in ((UhtLookupTable<TValue>)BaseTable).Lookup)
			{
				this.Lookup.TryAdd(KVP.Key, KVP.Value);
			}
		}
	}

	/// <summary>
	/// A collection of lookup tables by name.
	/// </summary>
	/// <typeparam name="TTable">Table type</typeparam>
	public class UhtLookupTables<TTable> where TTable : UhtLookupTableBase, new()
	{

		/// <summary>
		/// Collection of named tables
		/// </summary>
		public Dictionary<string, TTable> Tables = new Dictionary<string, TTable>();

		/// <summary>
		/// The name of the group of tables
		/// </summary>
		public string Name;

		/// <summary>
		/// Create a new group of tables
		/// </summary>
		/// <param name="Name">The name of the group</param>
		public UhtLookupTables(string Name)
		{
			this.Name = Name;
		}

		/// <summary>
		/// Given a table name, return the table.  If not found, a new one will be added with the given name.
		/// </summary>
		/// <param name="TableName">The name of the table to return</param>
		/// <returns>The table associated with the name.</returns>
		public TTable Get(string TableName)
		{
			TTable? Table;
			if (!this.Tables.TryGetValue(TableName, out Table))
			{
				Table = new TTable();
				Table.TableName = TableName;
				this.Tables.Add(TableName, Table);
			}
			return Table;
		}

		/// <summary>
		/// Create a table with the given information.  If the table already exists, it will be initialized with the given data.
		/// </summary>
		/// <param name="TableName">The name of the table</param>
		/// <param name="UserName">The user facing name of the name</param>
		/// <param name="ParentTableName">The parent table name used to merge table entries</param>
		/// <param name="bInternal">If true, the table is internal and won't be visible to the user.</param>
		/// <returns>The created table</returns>
		public TTable Create(string TableName, string UserName, string? ParentTableName, bool bInternal = false)
		{
			TTable Table = Get(TableName);
			Table.UserName = UserName;
			Table.Internal = bInternal;
			Table.Implemented = true;
			if (!string.IsNullOrEmpty(ParentTableName))
			{
				Table.ParentTable = Get(ParentTableName);
			}
			return Table;
		}

		/// <summary>
		/// Merge the contents of all parent tables into their children.  This is done so that the 
		/// parent chain need not be searched when looking for table entries.
		/// </summary>
		/// <exception cref="UhtIceException">Thrown if there are problems with the tables.</exception>
		public void Merge()
		{
			List<TTable> OrderedList = new List<TTable>(this.Tables.Count);
			List<TTable> RemainingList = new List<TTable>(this.Tables.Count);
			HashSet<UhtLookupTableBase> DoneTables = new HashSet<UhtLookupTableBase>();

			// Collect all of the tables
			foreach (KeyValuePair<string, TTable> KVP in this.Tables)
			{
				if (!KVP.Value.Implemented)
				{
					throw new UhtIceException($"{this.Name} table '{KVP.Value.TableName}' has been referenced but not implemented");
				}
				RemainingList.Add(KVP.Value);
			}

			// Perform a topological sort of the tables
			while (RemainingList.Count != 0)
			{
				bool bAddedOne = false;
				for (int i = 0; i < RemainingList.Count;)
				{
					TTable Table = RemainingList[i];
					if (Table.ParentTable == null || DoneTables.Contains(Table.ParentTable))
					{
						OrderedList.Add(Table);
						DoneTables.Add(Table);
						RemainingList[i] = RemainingList[RemainingList.Count - 1];
						RemainingList.RemoveAt(RemainingList.Count - 1);
						bAddedOne = true;
					}
					else
					{
						++i;
					}
				}
				if (!bAddedOne)
				{
					throw new UhtIceException($"Circular dependency in {this.GetType().Name}.{this.Name} tables detected");
				}
			}

			// Merge the tables
			foreach (TTable Table in OrderedList)
			{
				if (Table.ParentTable != null)
				{
					Table.Merge((TTable)Table.ParentTable);
				}
			}
		}
	}

	/// <summary>
	/// Bootstraps the standard UHT tables
	/// </summary>
	public static class UhtStandardTables
	{

		/// <summary>
		/// Enumeration that specifies if the table is a specifier and/or keyword table
		/// </summary>
		[Flags]
		public enum EUhtCreateTablesFlags
		{
			/// <summary>
			/// A keyword table will be created
			/// </summary>
			Keyword = 1 << 0,

			/// <summary>
			/// A specifier table will be created
			/// </summary>
			Specifiers = 1 << 1,
		}

		/// <summary>
		/// Create all of the standard scope tables.
		/// </summary>
		/// <param name="Tables">UHT tables</param>
		public static void InitStandardTables(UhtTables Tables)
		{
			CreateTables(Tables, UhtTableNames.Default, "Default", EUhtCreateTablesFlags.Specifiers | EUhtCreateTablesFlags.Keyword, null, true);
			CreateTables(Tables, UhtTableNames.Global, "Global", EUhtCreateTablesFlags.Keyword, UhtTableNames.Default, false);
			CreateTables(Tables, UhtTableNames.PropertyArgument, "Argument/Return", EUhtCreateTablesFlags.Specifiers, UhtTableNames.Default, false);
			CreateTables(Tables, UhtTableNames.PropertyMember, "Member", EUhtCreateTablesFlags.Specifiers, UhtTableNames.Default, false);
			CreateTables(Tables, UhtTableNames.Object, "Object", EUhtCreateTablesFlags.Specifiers | EUhtCreateTablesFlags.Keyword, UhtTableNames.Default, true);
			CreateTables(Tables, UhtTableNames.Field, "Field", EUhtCreateTablesFlags.Specifiers | EUhtCreateTablesFlags.Keyword, UhtTableNames.Object, true);
			CreateTables(Tables, UhtTableNames.Enum, "Enum", EUhtCreateTablesFlags.Specifiers | EUhtCreateTablesFlags.Keyword, UhtTableNames.Field, false);
			CreateTables(Tables, UhtTableNames.Struct, "Struct", EUhtCreateTablesFlags.Specifiers | EUhtCreateTablesFlags.Keyword, UhtTableNames.Field, true);
			CreateTables(Tables, UhtTableNames.ClassBase, "ClassBase", EUhtCreateTablesFlags.Specifiers | EUhtCreateTablesFlags.Keyword, UhtTableNames.Struct, true);
			CreateTables(Tables, UhtTableNames.Class, "Class", EUhtCreateTablesFlags.Specifiers | EUhtCreateTablesFlags.Keyword, UhtTableNames.ClassBase, false);
			CreateTables(Tables, UhtTableNames.Interface, "Interface", EUhtCreateTablesFlags.Specifiers | EUhtCreateTablesFlags.Keyword, UhtTableNames.ClassBase, false);
			CreateTables(Tables, UhtTableNames.NativeInterface, "IInterface", EUhtCreateTablesFlags.Specifiers | EUhtCreateTablesFlags.Keyword, UhtTableNames.ClassBase, false);
			CreateTables(Tables, UhtTableNames.Function, "Function", EUhtCreateTablesFlags.Specifiers | EUhtCreateTablesFlags.Keyword, UhtTableNames.Struct, false);
			CreateTables(Tables, UhtTableNames.ScriptStruct, "Struct", EUhtCreateTablesFlags.Specifiers | EUhtCreateTablesFlags.Keyword, UhtTableNames.Struct, false);
		}

		/// <summary>
		/// Creates a series of tables given the supplied setup
		/// </summary>
		/// <param name="Tables">UHT tables</param>
		/// <param name="TableName">Name of the table.</param>
		/// <param name="TableUserName">Name presented to the user via error messages.</param>
		/// <param name="CreateTables">Types of tables to be created.</param>
		/// <param name="ParentTableName">Name of the parent table or null for none.</param>
		/// <param name="bInternal">If true, this table will not be included in any error messages.</param>
		public static void CreateTables(UhtTables Tables, string TableName, string TableUserName, 
			EUhtCreateTablesFlags CreateTables, string? ParentTableName, bool bInternal = false)
		{
			if (CreateTables.HasFlag(EUhtCreateTablesFlags.Keyword))
			{
				Tables.KeywordTables.Create(TableName, TableUserName, ParentTableName, bInternal);
			}
			if (CreateTables.HasFlag(EUhtCreateTablesFlags.Specifiers))
			{
				Tables.SpecifierTables.Create(TableName, TableUserName, ParentTableName, bInternal);
				Tables.SpecifierValidatorTables.Create(TableName, TableUserName, ParentTableName, bInternal);
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
	public class UhtTables
	{
		/// <summary>
		/// Collection of specifier tables
		/// </summary>
		public readonly UhtSpecifierTables SpecifierTables = new UhtSpecifierTables();

		/// <summary>
		/// Collection of specifier validator tables
		/// </summary>
		public readonly UhtSpecifierValidatorTables SpecifierValidatorTables = new UhtSpecifierValidatorTables();

		/// <summary>
		/// Collection of keyword tables
		/// </summary>
		public readonly UhtKeywordTables KeywordTables = new UhtKeywordTables();

		/// <summary>
		/// Collection of property types
		/// </summary>
		public readonly UhtPropertyTypeTable PropertyTypeTable = new UhtPropertyTypeTable();

		/// <summary>
		/// Collection of structure default values
		/// </summary>
		public readonly UhtStructDefaultValueTable StructDefaultValueTable = new UhtStructDefaultValueTable();

		/// <summary>
		/// Collection of engine class types
		/// </summary>
		public readonly UhtEngineClassTable EngineClassTable = new UhtEngineClassTable();

		/// <summary>
		/// Collection of exporters
		/// </summary>
		public readonly UhtExporterTable ExporterTable = new UhtExporterTable();

		/// <summary>
		/// Collection loc text default values
		/// </summary>
		public readonly UhtLocTextDefaultValueTable LocTextDefaultValueTable = new UhtLocTextDefaultValueTable();

		/// <summary>
		/// Construct a new table collection
		/// </summary>
		public UhtTables()
		{
			UhtStandardTables.InitStandardTables(this);
			CheckForAttributes(Assembly.GetExecutingAssembly());
			PerformPostInitialization();
		}

		/// <summary>
		/// Add a collection of plugin assembly file paths
		/// </summary>
		/// <param name="PluginAssembliesFilePaths">Collection of plugins to load</param>
		public void AddPlugins(IEnumerable<string> PluginAssembliesFilePaths)
		{
			foreach (string AssemblyFilePath in PluginAssembliesFilePaths)
			{
				Assembly? Plugin = FindAssemblyByName(Path.GetFileNameWithoutExtension(AssemblyFilePath));
				if (Plugin == null)
				{
					byte[] AssemblyBytes = File.ReadAllBytes(AssemblyFilePath);
					Plugin = Assembly.Load(AssemblyBytes);
				}
				CheckForAttributes(Plugin);
			}
			PerformPostInitialization();
		}

		/// <summary>
		/// Locate the assembly by name
		/// </summary>
		/// <param name="Name">Name of the assembly</param>
		/// <returns>Assembly or null</returns>
		private Assembly? FindAssemblyByName(string? Name)
		{
			if (Name != null)
			{
				foreach (Assembly Assembly in AppDomain.CurrentDomain.GetAssemblies())
				{
					AssemblyName AssemblyName = Assembly.GetName();
					if (AssemblyName.Name != null && String.Compare(AssemblyName.Name, Name, true) == 0)
					{
						return Assembly;
					}
				}
			}
			return null;
		}

		/// <summary>
		/// Check for UHT attributes on all types in the given assembly
		/// </summary>
		/// <param name="Assembly">Assembly to scan</param>
		private void CheckForAttributes(Assembly? Assembly)
		{
			if (Assembly != null)
			{
				foreach (Type Type in Assembly.SafeGetLoadedTypes())
				{
					CheckForAttributes(Type);
				}
			}
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
						this.ExporterTable.OnExporterAttribute(Type, MethodInfo, ExporterAttribute);
					}
					else if (MethodAttribute is UhtLocTextDefaultValueAttribute LocTextDefaultValueAttribute)
					{
						this.LocTextDefaultValueTable.OnLocTextDefaultValueAttribute(Type, MethodInfo, LocTextDefaultValueAttribute);
					}
				}
			}
		}
	}
}
