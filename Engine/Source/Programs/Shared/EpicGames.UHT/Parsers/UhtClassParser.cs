// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;
using System;
using System.Collections.Generic;
using System.Text;

namespace EpicGames.UHT.Parsers
{
	[UnrealHeaderTool]
	public class UhtClassParser : UhtClassBaseParser
	{
		private static UhtKeywordTable KeywordTable = UhtKeywordTables.Instance.Get(UhtTableNames.Class);
		private static UhtSpecifierTable SpecifierTable = UhtSpecifierTables.Instance.Get(UhtTableNames.Class);

		public EClassFlags RemovedClassFlags = EClassFlags.None;
		public string ClassWithinIdentifier = String.Empty;

		public List<string> ShowCategories = new List<string>();
		public List<string> HideCategories = new List<string>();
		public List<string> AutoExpandCategories = new List<string>();
		public List<string> AutoCollapseCategories = new List<string>();
		public List<string> PrioritizeCategories = new List<string>();
		public List<string> ShowFunctions = new List<string>();
		public List<string> HideFunctions = new List<string>();
		public List<string> SparseClassDataTypes = new List<string>();
		public List<string> ClassGroupNames = new List<string>();

		public void AddClassFlags(EClassFlags Flags)
		{
			this.ClassFlags |= Flags;
			this.RemovedClassFlags &= ~Flags;
		}

		public void RemoveClassFlags(EClassFlags Flags)
		{
			this.RemovedClassFlags |= Flags;
			this.ClassFlags &= ~Flags;
		}

		public UhtClassParser(UhtType Outer, int LineNumber) : base(Outer, LineNumber)
		{
		}

		protected override void ResolveSuper(UhtResolvePhase ResolvePhase)
		{
			base.ResolveSuper(ResolvePhase);

			// Make sure the class within is resolved.  We have to make sure we don't try to resolve ourselves.
			if (this.ClassWithin != null && this.ClassWithin != this)
			{
				this.ClassWithin.Resolve(ResolvePhase);
			}

			switch (ResolvePhase)
			{
				case UhtResolvePhase.Bases:
					UhtClass? SuperClass = this.SuperClass;

					// Merge the super class flags
					if (SuperClass != null)
					{
						this.ClassFlags |= SuperClass.ClassFlags & EClassFlags.ScriptInherit & ~this.RemovedClassFlags;
					}

					if (this.Bases != null)
					{
						foreach (UhtStruct Base in this.Bases)
						{
							if (Base is UhtClass BaseClass)
							{
								if (!BaseClass.ClassFlags.HasAnyFlags(EClassFlags.Interface))
								{
									this.LogError($"Class '{BaseClass.SourceName}' is not an interface; Can only inherit from non-UObjects or UInterface derived interfaces");
								}
								this.ClassFlags |= BaseClass.ClassFlags & EClassFlags.ScriptInherit & ~this.RemovedClassFlags;
							}
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

					SetAndValidateWithinClass(ResolvePhase);
					SetAndValidateConfigName(ResolvePhase);

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
			foreach (string Value in this.ShowFunctions)
			{
				this.HideFunctions.RemoveSwap(Value);
			}
			this.ShowFunctions.Clear();

			// Merge AutoExpandCategories and AutoCollapseCategories (we still want to keep AutoExpandCategories though!)
			List<string> ParentAutoExpandCategories = GetStringListMetaData(this.SuperClass, UhtNames.AutoExpandCategories);
			List<string> ParentAutoCollapseCategories = GetStringListMetaData(this.SuperClass, UhtNames.AutoCollapseCategories);

			foreach (string Value in this.AutoExpandCategories)
			{
				this.AutoCollapseCategories.RemoveSwap(Value);
				ParentAutoCollapseCategories.RemoveSwap(Value);
			}

			// Do the same as above but the other way around
			foreach (string Value in this.AutoCollapseCategories)
			{
				this.AutoExpandCategories.RemoveSwap(Value);
				ParentAutoExpandCategories.RemoveSwap(Value);
			}

			// Once AutoExpandCategories and AutoCollapseCategories for THIS class have been parsed, add the parent inherited categories
			this.AutoCollapseCategories.AddRange(ParentAutoCollapseCategories);
			this.AutoExpandCategories.AddRange(ParentAutoExpandCategories);
		}

		private void MergeShowCategories()
		{

			// Add the super class hide categories and prime the output show categories with the parent
			List<string> OutShowCategories = GetStringListMetaData(this.SuperClass, UhtNames.ShowCategories);
			AppendStringListMetaData(this.SuperClass, UhtNames.HideCategories, this.HideCategories);

			// If this class has new show categories, then merge them
			if (this.ShowCategories.Count != 0)
			{
				StringBuilder SubCategoryPath = new StringBuilder();
				foreach (string Value in this.ShowCategories)
				{

					// if we didn't find this specific category path in the HideCategories metadata
					if (!this.HideCategories.RemoveSwap(Value))
					{
						string[] SubCategories = Value.ToString().Split('|', StringSplitOptions.RemoveEmptyEntries);

						SubCategoryPath.Clear();
						// look to see if any of the parent paths are excluded in the HideCategories list
						for (int CategoryPathIndex = 0; CategoryPathIndex < SubCategories.Length - 1; ++CategoryPathIndex)
						{
							SubCategoryPath.Append(SubCategories[CategoryPathIndex]);
							// if we're hiding a parent category, then we need to flag this sub category for show
							if (HideCategories.Contains(SubCategoryPath.ToString()))
							{
								OutShowCategories.AddUnique(Value);
								break;
							}
							SubCategoryPath.Append("|");
						}
					}
				}
			}

			// Replace the show categories
			this.ShowCategories = OutShowCategories;
		}

		private void SetAndValidateWithinClass(UhtResolvePhase ResolvePhase)
		{
			// The class within must be a child of any super class within
			UhtClass ExpectedClassWithin = this.SuperClass != null ? this.SuperClass.ClassWithin : this.Session.UObject;

			// Process all of the class specifiers
			if (!string.IsNullOrEmpty(this.ClassWithinIdentifier))
			{
				UhtClass? SpecifiedClassWithin = (UhtClass?)this.Session.FindType(null, UhtFindOptions.EngineName | UhtFindOptions.Class, this.ClassWithinIdentifier);
				if (SpecifiedClassWithin == null)
				{
					this.LogError($"Within class '{this.ClassWithinIdentifier}' not found");
				}
				else
				{

					// Make sure the class within has been resolved to this phase.  We don't need to worry about the super since we know 
					// that it has already been resolved.
					if (SpecifiedClassWithin != this)
					{
						SpecifiedClassWithin.Resolve(ResolvePhase);
					}

					if (SpecifiedClassWithin.IsChildOf(this.Session.UInterface))
					{
						this.LogError("Classes cannot be 'within' interfaces");
					}
					else if (!SpecifiedClassWithin.IsChildOf(ExpectedClassWithin))
					{
						this.LogError($"Cannot override within class with '{SpecifiedClassWithin.SourceName}' since it isn't a child of parent class's expected within '{ExpectedClassWithin.SourceName}'");
					}
					else
					{
						this.ClassWithin = SpecifiedClassWithin;
					}
				}
			}

			// If we don't have a class within set, then just use the expected within
			else
			{
				this.ClassWithin = ExpectedClassWithin;
			}
		}

		private void SetAndValidateConfigName(UhtResolvePhase ResolvePhase)
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
				foreach (UhtProperty Property in this.Properties)
				{
					if (Property.PropertyFlags.HasAnyFlags(EPropertyFlags.Config))
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

		private static List<string> GetStringListMetaData(UhtType? Type, string Key)
		{
			List<string> Out = new List<string>();
			AppendStringListMetaData(Type, Key, Out);
			return Out;
		}

		private static void AppendStringListMetaData(UhtType? Type, string Key, List<string> StringList)
		{
			if (Type != null)
			{
				string[]? Values = Type.MetaData.GetStringArray(Key);
				if (Values != null)
				{
					foreach (string Value in Values)
					{
						//COMPATIBILITY-TODO - TEST - This preserves duplicates that are in old UHT
						//StringList.AddUnique(Value);
						StringList.Add(Value);
					}
				}
			}
		}

		#region Keywords
		[UhtKeyword(Extends = UhtTableNames.Global)]
		private static UhtParseResult UCLASSKeyword(UhtParsingScope TopScope, UhtParsingScope ActionScope, ref UhtToken Token)
		{
			return ParseUClass(TopScope, ref Token);
		}

		[UhtKeyword(Extends = UhtTableNames.Class)]
		[UhtKeyword(Extends = UhtTableNames.Class, Keyword = "GENERATED_BODY")]
		private static UhtParseResult GENERATED_UCLASS_BODYKeyword(UhtParsingScope TopScope, UhtParsingScope ActionScope, ref UhtToken Token)
		{
			UhtClass Class = (UhtClass)TopScope.ScopeType;

			if (Token.IsValue("GENERATED_BODY"))
			{
				Class.bHasGeneratedBody = true;
				Class.GeneratedBodyAccessSpecifier = TopScope.AccessSpecifier;
			}
			else
			{
				TopScope.AccessSpecifier = UhtAccessSpecifier.Public;
			}

			UhtParserHelpers.ParseCompileVersionDeclaration(TopScope.TokenReader, Class);

			Class.GeneratedBodyLineNumber = TopScope.TokenReader.InputLine;

			TopScope.TokenReader.Optional(';');
			return UhtParseResult.Handled;
		}
		#endregion

		public static UhtParseResult ParseUClass(UhtParsingScope ParentScope, ref UhtToken Token)
		{
			UhtClassParser Class = new UhtClassParser(ParentScope.ScopeType, Token.InputLine);
			using (var TopScope = new UhtParsingScope(ParentScope, Class, UhtClassParser.KeywordTable, UhtAccessSpecifier.Private))
			{
				const string ScopeName = "class";

				using (var TokenContext = new UhtMessageContext(TopScope.TokenReader, ScopeName))
				{
					// Parse the specifiers
					UhtSpecifierContext SpecifierContext = new UhtSpecifierContext(TopScope, TopScope.TokenReader, Class.MetaData);
					UhtSpecifierParser Specifiers = TopScope.HeaderParser.GetSpecifierParser(SpecifierContext, ScopeName, UhtClassParser.SpecifierTable);
					Specifiers.ParseSpecifiers();
					Class.PrologLineNumber = TopScope.TokenReader.InputLine;
					Class.ClassFlags |= EClassFlags.Native;

					TopScope.AddFormattedCommentsAsTooltipMetaData();

					// Consume the class specifier
					TopScope.TokenReader.Require("class");

					TopScope.TokenReader.SkipAlignasAndDeprecatedMacroIfNecessary();

					// Read the class name and possible API macro name
					UhtToken APIMacroToken;
					TopScope.TokenReader.TryOptionalAPIMacro(out APIMacroToken);
					Class.SourceName = TopScope.TokenReader.GetIdentifier().Value.ToString();

					// Update the context for better error messages
					TokenContext.Reset($"class '{Class.SourceName}'");

					// Split the source name into the different parts
					UhtEngineNameParts NameParts = UhtUtilities.GetEngineNameParts(Class.SourceName);
					Class.EngineName = NameParts.EngineName.ToString();

					// Check for an empty engine name
					if (Class.EngineName.Length == 0)
					{
						TopScope.TokenReader.LogError($"When compiling class definition for '{Class.SourceName}', attempting to strip prefix results in an empty name. Did you leave off a prefix?");
					}

					// Skip optional final keyword
					TopScope.TokenReader.Optional("final");

					// Parse the inheritance
					UhtParserHelpers.ParseInheritance(TopScope.TokenReader, out Class.SuperIdentifier, out Class.BaseIdentifiers);

					if (APIMacroToken)
					{
						Class.ClassFlags |= EClassFlags.RequiredAPI;
					}

					Specifiers.ParseDeferred();

					if (Class.Outer != null)
					{
						Class.Outer.AddChild(Class);
					}

					TopScope.AddModuleRelativePathToMetaData();

					TopScope.HeaderParser.ParseStatements('{', '}', true);

					TopScope.TokenReader.Require(';');

					if (Class.GeneratedBodyLineNumber == -1)
					{
						TopScope.TokenReader.LogError("Expected a GENERATED_BODY() at the start of the class");
					}
				}
			}

			return UhtParseResult.Handled;
		}
	}
}
