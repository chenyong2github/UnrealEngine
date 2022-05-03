// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Text;
using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;

namespace EpicGames.UHT.Parsers
{
	/// <summary>
	/// UCLASS parser
	/// </summary>
	[UnrealHeaderTool]
	public class UhtClassParser : UhtClassBaseParser
	{
		/// <summary>
		/// Engine class flags removed
		/// </summary>
		public EClassFlags RemovedClassFlags { get; set; } = EClassFlags.None;

		/// <summary>
		/// Class within identifier
		/// </summary>
		public string ClassWithinIdentifier { get; set; } = String.Empty;

		/// <summary>
		/// Collection of show categories
		/// </summary>
		public List<string> ShowCategories { get; } = new List<string>();

		/// <summary>
		/// Collection of hide categories
		/// </summary>
		public List<string> HideCategories { get; } = new List<string>();

		/// <summary>
		/// Collection of auto expand categories
		/// </summary>
		public List<string> AutoExpandCategories { get; } = new List<string>();

		/// <summary>
		/// Collection of auto collapse categories
		/// </summary>
		public List<string> AutoCollapseCategories { get; } = new List<string>();

		/// <summary>
		/// Collection of prioritize categories
		/// </summary>
		public List<string> PrioritizeCategories { get; } = new List<string>();

		/// <summary>
		/// Collection of show functions
		/// </summary>
		public List<string> ShowFunctions { get; } = new List<string>();

		/// <summary>
		/// Collection of hide functions
		/// </summary>
		public List<string> HideFunctions { get; } = new List<string>();

		/// <summary>
		/// Sparse class data types
		/// </summary>
		public List<string> SparseClassDataTypes { get; } = new List<string>();

		/// <summary>
		/// Class group names
		/// </summary>
		public List<string> ClassGroupNames { get; } = new List<string>();

		/// <summary>
		/// Add the given class flags
		/// </summary>
		/// <param name="flags">Flags to add</param>
		public void AddClassFlags(EClassFlags flags)
		{
			this.ClassFlags |= flags;
			this.RemovedClassFlags &= ~flags;
		}

		/// <summary>
		/// Remove the given class flags
		/// </summary>
		/// <param name="flags">Flags to remove</param>
		public void RemoveClassFlags(EClassFlags flags)
		{
			this.RemovedClassFlags |= flags;
			this.ClassFlags &= ~flags;
		}

		/// <summary>
		/// Construct a new class parser
		/// </summary>
		/// <param name="outer">Outer type</param>
		/// <param name="lineNumber">Line number</param>
		public UhtClassParser(UhtType outer, int lineNumber) : base(outer, lineNumber)
		{
		}

		/// <inheritdoc/>
		protected override void ResolveSuper(UhtResolvePhase resolvePhase)
		{
			base.ResolveSuper(resolvePhase);

			// Make sure the class within is resolved.  We have to make sure we don't try to resolve ourselves.
			if (this.ClassWithin != null && this.ClassWithin != this)
			{
				this.ClassWithin.Resolve(resolvePhase);
			}

			switch (resolvePhase)
			{
				case UhtResolvePhase.Bases:
					UhtClass? superClass = this.SuperClass;

					// Merge the super class flags
					if (superClass != null)
					{
						this.ClassFlags |= superClass.ClassFlags & EClassFlags.ScriptInherit & ~this.RemovedClassFlags;
					}

					foreach (UhtStruct baseStruct in this.Bases)
					{
						if (baseStruct is UhtClass baseClass)
						{
							if (!baseClass.ClassFlags.HasAnyFlags(EClassFlags.Interface))
							{
								this.LogError($"Class '{baseClass.SourceName}' is not an interface; Can only inherit from non-UObjects or UInterface derived interfaces");
							}
							this.ClassFlags |= baseClass.ClassFlags & EClassFlags.ScriptInherit & ~this.RemovedClassFlags;
						}
					}

					// These following collections only inherit from he parent if they are empty in this class
					if (this.SparseClassDataTypes.Count == 0)
					{
						AppendStringListMetaData(this.SuperClass, UhtNames.SparseClassDataTypes, this.SparseClassDataTypes);
					}
					if (this.PrioritizeCategories.Count == 0)
					{
						AppendStringListMetaData(this.SuperClass, UhtNames.PrioritizeCategories, this.PrioritizeCategories);
					}

					// Merge with categories inherited from the parent.
					MergeCategories();

					SetAndValidateWithinClass(resolvePhase);
					SetAndValidateConfigName();

					// Copy all of the lists back to the meta data
					this.MetaData.AddIfNotEmpty(UhtNames.ClassGroupNames, this.ClassGroupNames);
					this.MetaData.AddIfNotEmpty(UhtNames.AutoCollapseCategories, this.AutoCollapseCategories);
					this.MetaData.AddIfNotEmpty(UhtNames.AutoExpandCategories, this.AutoExpandCategories);
					this.MetaData.AddIfNotEmpty(UhtNames.PrioritizeCategories, this.PrioritizeCategories);
					this.MetaData.AddIfNotEmpty(UhtNames.HideCategories, this.HideCategories);
					this.MetaData.AddIfNotEmpty(UhtNames.ShowCategories, this.ShowCategories);
					this.MetaData.AddIfNotEmpty(UhtNames.SparseClassDataTypes, this.SparseClassDataTypes);
					this.MetaData.AddIfNotEmpty(UhtNames.HideFunctions, this.HideFunctions);

					this.MetaData.Add(UhtNames.IncludePath, this.HeaderFile.IncludeFilePath);
					break;
			}
		}

		private void MergeCategories()
		{
			MergeShowCategories();

			// Merge ShowFunctions and HideFunctions
			AppendStringListMetaData(this.SuperClass, UhtNames.HideFunctions, this.HideFunctions);
			foreach (string value in this.ShowFunctions)
			{
				this.HideFunctions.RemoveSwap(value);
			}
			this.ShowFunctions.Clear();

			// Merge AutoExpandCategories and AutoCollapseCategories (we still want to keep AutoExpandCategories though!)
			List<string> parentAutoExpandCategories = GetStringListMetaData(this.SuperClass, UhtNames.AutoExpandCategories);
			List<string> parentAutoCollapseCategories = GetStringListMetaData(this.SuperClass, UhtNames.AutoCollapseCategories);

			foreach (string value in this.AutoExpandCategories)
			{
				this.AutoCollapseCategories.RemoveSwap(value);
				parentAutoCollapseCategories.RemoveSwap(value);
			}

			// Do the same as above but the other way around
			foreach (string value in this.AutoCollapseCategories)
			{
				this.AutoExpandCategories.RemoveSwap(value);
				parentAutoExpandCategories.RemoveSwap(value);
			}

			// Once AutoExpandCategories and AutoCollapseCategories for THIS class have been parsed, add the parent inherited categories
			this.AutoCollapseCategories.AddRange(parentAutoCollapseCategories);
			this.AutoExpandCategories.AddRange(parentAutoExpandCategories);
		}

		private void MergeShowCategories()
		{

			// Add the super class hide categories and prime the output show categories with the parent
			List<string> outShowCategories = GetStringListMetaData(this.SuperClass, UhtNames.ShowCategories);
			AppendStringListMetaData(this.SuperClass, UhtNames.HideCategories, this.HideCategories);

			// If this class has new show categories, then merge them
			if (this.ShowCategories.Count != 0)
			{
				StringBuilder subCategoryPath = new();
				foreach (string value in this.ShowCategories)
				{

					// if we didn't find this specific category path in the HideCategories metadata
					if (!this.HideCategories.RemoveSwap(value))
					{
						string[] subCategories = value.ToString().Split('|', StringSplitOptions.RemoveEmptyEntries);

						subCategoryPath.Clear();
						// look to see if any of the parent paths are excluded in the HideCategories list
						for (int categoryPathIndex = 0; categoryPathIndex < subCategories.Length - 1; ++categoryPathIndex)
						{
							subCategoryPath.Append(subCategories[categoryPathIndex]);
							// if we're hiding a parent category, then we need to flag this sub category for show
							if (HideCategories.Contains(subCategoryPath.ToString()))
							{
								outShowCategories.AddUnique(value);
								break;
							}
							subCategoryPath.Append('|');
						}
					}
				}
			}

			// Replace the show categories
			this.ShowCategories.Clear();
			this.ShowCategories.AddRange(outShowCategories);
		}

		private void SetAndValidateWithinClass(UhtResolvePhase resolvePhase)
		{
			// The class within must be a child of any super class within
			UhtClass expectedClassWithin = this.SuperClass != null ? this.SuperClass.ClassWithin : this.Session.UObject;

			// Process all of the class specifiers
			if (!String.IsNullOrEmpty(this.ClassWithinIdentifier))
			{
				UhtClass? specifiedClassWithin = (UhtClass?)this.Session.FindType(null, UhtFindOptions.EngineName | UhtFindOptions.Class, this.ClassWithinIdentifier);
				if (specifiedClassWithin == null)
				{
					this.LogError($"Within class '{this.ClassWithinIdentifier}' not found");
				}
				else
				{

					// Make sure the class within has been resolved to this phase.  We don't need to worry about the super since we know 
					// that it has already been resolved.
					if (specifiedClassWithin != this)
					{
						specifiedClassWithin.Resolve(resolvePhase);
					}

					if (specifiedClassWithin.IsChildOf(this.Session.UInterface))
					{
						this.LogError("Classes cannot be 'within' interfaces");
					}
					else if (!specifiedClassWithin.IsChildOf(expectedClassWithin))
					{
						this.LogError($"Cannot override within class with '{specifiedClassWithin.SourceName}' since it isn't a child of parent class's expected within '{expectedClassWithin.SourceName}'");
					}
					else
					{
						this.ClassWithin = specifiedClassWithin;
					}
				}
			}

			// If we don't have a class within set, then just use the expected within
			else
			{
				this.ClassWithin = expectedClassWithin;
			}
		}

		private void SetAndValidateConfigName()
		{
			// Since this flag is computed in this method, we have to re-propagate the flag from the super
			// just in case they were defined in this source file.
			if (this.SuperClass != null)
			{
				this.ClassFlags |= this.SuperClass.ClassFlags & EClassFlags.Config;
			}

			// Set the class config flag if any properties have config
			if (!this.ClassFlags.HasAnyFlags(EClassFlags.Config))
			{
				foreach (UhtProperty property in this.Properties)
				{
					if (property.PropertyFlags.HasAnyFlags(EPropertyFlags.Config))
					{
						this.ClassFlags |= EClassFlags.Config;
						break;
					}
				}
			}

			if (this.Config.Length > 0)
			{
				// if the user specified "inherit", we're just going to use the parent class's config filename
				// this is not actually necessary but it can be useful for explicitly communicating config-ness
				if (this.Config.Equals("inherit", StringComparison.OrdinalIgnoreCase))
				{
					if (this.SuperClass == null)
					{
						this.LogError($"Cannot inherit config filename for class '{this.SourceName}' when it has no super class");
					}
					else if (this.SuperClass.Config.Length == 0)
					{
						this.LogError($"Cannot inherit config filename for class '{this.SourceName}' when parent class '{this.SuperClass.SourceName}' has no config filename");
					}
					else
					{
						this.Config = this.SuperClass.Config;
					}
				}
			}
			else if (this.ClassFlags.HasAnyFlags(EClassFlags.Config) && this.SuperClass != null)
			{
				this.Config = this.SuperClass.Config;
			}

			if (this.ClassFlags.HasAnyFlags(EClassFlags.Config) && this.Config.Length == 0)
			{
				this.LogError("Classes with config / globalconfig member variables need to specify config file.");
				this.Config = "Engine";
			}
		}

		private static List<string> GetStringListMetaData(UhtType? type, string key)
		{
			List<string> outStrings = new();
			AppendStringListMetaData(type, key, outStrings);
			return outStrings;
		}

		private static void AppendStringListMetaData(UhtType? type, string key, List<string> stringList)
		{
			if (type != null)
			{
				string[]? values = type.MetaData.GetStringArray(key);
				if (values != null)
				{
					foreach (string value in values)
					{
						//COMPATIBILITY-TODO - TEST - This preserves duplicates that are in old UHT
						//StringList.AddUnique(Value);
						stringList.Add(value);
					}
				}
			}
		}

		#region Keywords
		[UhtKeyword(Extends = UhtTableNames.Global)]
		[SuppressMessage("CodeQuality", "IDE0051:Remove unused private members", Justification = "Attribute accessed method")]
		[SuppressMessage("Style", "IDE0060:Remove unused parameter", Justification = "Attribute accessed method")]
		private static UhtParseResult UCLASSKeyword(UhtParsingScope topScope, UhtParsingScope actionScope, ref UhtToken token)
		{
			return ParseUClass(topScope, ref token);
		}

		[UhtKeyword(Extends = UhtTableNames.Class)]
		[UhtKeyword(Extends = UhtTableNames.Class, Keyword = "GENERATED_BODY")]
		[SuppressMessage("CodeQuality", "IDE0051:Remove unused private members", Justification = "Attribute accessed method")]
		[SuppressMessage("Style", "IDE0060:Remove unused parameter", Justification = "Attribute accessed method")]
		private static UhtParseResult GENERATED_UCLASS_BODYKeyword(UhtParsingScope topScope, UhtParsingScope actionScope, ref UhtToken token)
		{
			UhtClass classObj = (UhtClass)topScope.ScopeType;

			if (token.IsValue("GENERATED_BODY"))
			{
				classObj.HasGeneratedBody = true;
				classObj.GeneratedBodyAccessSpecifier = topScope.AccessSpecifier;
			}
			else
			{
				topScope.AccessSpecifier = UhtAccessSpecifier.Public;
			}

			UhtParserHelpers.ParseCompileVersionDeclaration(topScope.TokenReader, topScope.Session.Config!, classObj);

			classObj.GeneratedBodyLineNumber = topScope.TokenReader.InputLine;

			topScope.TokenReader.Optional(';');
			return UhtParseResult.Handled;
		}
		#endregion

		private static UhtParseResult ParseUClass(UhtParsingScope parentScope, ref UhtToken token)
		{
			UhtClassParser classObj = new(parentScope.ScopeType, token.InputLine);
			{
				using UhtParsingScope topScope = new(parentScope, classObj, parentScope.Session.GetKeywordTable(UhtTableNames.Class), UhtAccessSpecifier.Private);
				const string ScopeName = "class";

				{
					using UhtMessageContext tokenContext = new(ScopeName);
					// Parse the specifiers
					UhtSpecifierContext specifierContext = new(topScope, topScope.TokenReader, classObj.MetaData);
					UhtSpecifierParser specifiers = topScope.HeaderParser.GetCachedSpecifierParser(specifierContext, ScopeName, parentScope.Session.GetSpecifierTable(UhtTableNames.Class));
					specifiers.ParseSpecifiers();
					classObj.PrologLineNumber = topScope.TokenReader.InputLine;
					classObj.ClassFlags |= EClassFlags.Native;

					topScope.AddFormattedCommentsAsTooltipMetaData();

					// Consume the class specifier
					topScope.TokenReader.Require("class");

					topScope.TokenReader.SkipAlignasAndDeprecatedMacroIfNecessary();

					// Read the class name and possible API macro name
					topScope.TokenReader.TryOptionalAPIMacro(out UhtToken apiMacroToken);
					classObj.SourceName = topScope.TokenReader.GetIdentifier().Value.ToString();

					// Update the context for better error messages
					tokenContext.Reset($"class '{classObj.SourceName}'");

					// Split the source name into the different parts
					UhtEngineNameParts nameParts = UhtUtilities.GetEngineNameParts(classObj.SourceName);
					classObj.EngineName = nameParts.EngineName.ToString();

					// Check for an empty engine name
					if (classObj.EngineName.Length == 0)
					{
						topScope.TokenReader.LogError($"When compiling class definition for '{classObj.SourceName}', attempting to strip prefix results in an empty name. Did you leave off a prefix?");
					}

					// Skip optional final keyword
					topScope.TokenReader.Optional("final");

					// Parse the inheritance
					UhtParserHelpers.ParseInheritance(topScope.TokenReader, topScope.Session.Config!, out UhtToken superIdentifier, out List<UhtToken[]>? baseIdentifiers);
					classObj.SuperIdentifier = superIdentifier;
					classObj.BaseIdentifiers = baseIdentifiers;

					if (apiMacroToken)
					{
						classObj.ClassFlags |= EClassFlags.RequiredAPI;
					}

					specifiers.ParseDeferred();

					if (classObj.Outer != null)
					{
						classObj.Outer.AddChild(classObj);
					}

					topScope.AddModuleRelativePathToMetaData();

					topScope.HeaderParser.ParseStatements('{', '}', true);

					topScope.TokenReader.Require(';');

					if (classObj.GeneratedBodyLineNumber == -1)
					{
						topScope.TokenReader.LogError("Expected a GENERATED_BODY() at the start of the class");
					}
				}
			}

			return UhtParseResult.Handled;
		}
	}
}
