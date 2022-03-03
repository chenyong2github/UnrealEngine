// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Types;
using System;
using System.Collections.Generic;

namespace EpicGames.UHT.Utils
{

	/// <summary>
	/// Symbol table
	/// </summary>
	internal class UhtSymbolTable
	{

		/// <summary>
		/// Represents symbol name lookup chain start.  Symbol chains are based on 
		/// the caseless name.
		/// </summary>
		struct Lookup
		{

			/// <summary>
			/// The type index of the symbol
			/// </summary>
			public int SymbolIndex;

			/// <summary>
			/// When searching the caseless chain, the cased index is used to match the symbol based
			/// on the case named.
			/// </summary>
			public int CasedIndex;
		}

		/// <summary>
		/// Entry in the symbol table
		/// </summary>
		struct Symbol
		{

			/// <summary>
			/// The type associated with the symbol
			/// </summary>
			public UhtType Type;

			/// <summary>
			/// Mask of different find options which will match this symbol
			/// </summary>
			public UhtFindOptions MatchOptions;

			/// <summary>
			/// The case lookup index for matching by case
			/// </summary>
			public int CasedIndex;

			/// <summary>
			/// The next index in the symbol change based on cassless lookup
			/// </summary>
			public int NextIndex;

			/// <summary>
			/// The last index in the chain.  This index is only used when the symbol entry is also acting as the list
			/// </summary>
			public int LastIndex;
		}

		/// <summary>
		/// Number of unique cased symbol names.  
		/// </summary>
		private int CasedCount = 0;

		/// <summary>
		/// Case name lookup table that returns the symbol index and the case index
		/// </summary>
		private Dictionary<string, Lookup> CasedDictionary = new Dictionary<string, Lookup>(StringComparer.Ordinal);

		/// <summary>
		/// Caseless name lookup table that returns the symbol index
		/// </summary>
		private Dictionary<string, int> CaselessDictionary = new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase);

		/// <summary>
		/// Collection of symbols in the table
		/// </summary>
		private Symbol[] Symbols;

		/// <summary>
		/// Constructs a new symbol table.
		/// </summary>
		/// <param name="TypeCount">Number of types in the table.</param>
		public UhtSymbolTable(int TypeCount)
		{
			this.Symbols = new Symbol[TypeCount];
		}

		/// <summary>
		/// Add a new type to the symbol table
		/// </summary>
		/// <param name="Type">The type being added</param>
		/// <param name="Name">The name of the type which could be the source name or the engine name</param>
		public void Add(UhtType Type, string Name)
		{
			Lookup Existing;
			if (this.CasedDictionary.TryGetValue(Name, out Existing))
			{
				AddExisting(Type, Existing.CasedIndex, Existing.SymbolIndex);
				return;
			}

			int CasedIndex = ++this.CasedCount;

			int SymbolIndex;
			if (this.CaselessDictionary.TryGetValue(Name, out SymbolIndex))
			{
				this.CasedDictionary.Add(Name, new Lookup { SymbolIndex = SymbolIndex, CasedIndex = CasedIndex });
				AddExisting(Type, CasedIndex, SymbolIndex);
				return;
			}

			SymbolIndex = Type.TypeIndex;
			this.Symbols[SymbolIndex] = new Symbol { Type = Type, MatchOptions = Type.EngineType.FindOptions(), CasedIndex = CasedIndex, NextIndex = 0, LastIndex = SymbolIndex };
			this.CaselessDictionary.Add(Name, SymbolIndex);
			this.CasedDictionary.Add(Name, new Lookup { SymbolIndex = SymbolIndex, CasedIndex = CasedIndex });
		}

		/// <summary>
		/// Replace an entry in the symbol table.  This is used during property resolution to replace the 
		/// parser property (which could not resolve the property prior to the symbol table being created)
		/// with the fully resolved property.
		/// </summary>
		/// <param name="OldType">The old type being replaced.</param>
		/// <param name="NewType">The new type.</param>
		/// <param name="Name">The name of the type.</param>
		/// <exception cref="UhtIceException">Thrown if the symbol wasn't found.</exception>
		public void Replace(UhtType OldType, UhtType NewType, string Name)
		{
			int SymbolIndex;
			if (this.CaselessDictionary.TryGetValue(Name, out SymbolIndex))
			{
				for (; SymbolIndex != 0; SymbolIndex = this.Symbols[SymbolIndex].NextIndex)
				{
					if (this.Symbols[SymbolIndex].Type == OldType)
					{
						this.Symbols[SymbolIndex].Type = NewType;
						return;
					}
				}
			}
			throw new UhtIceException("Attempt to replace a type that wasn't found");
		}

		/// <summary>
		/// Lookup the given name using cased string compare.
		/// </summary>
		/// <param name="StartingType">Starting type used to limit the scope of the search.</param>
		/// <param name="Options">Options controlling what is search and what is returned.</param>
		/// <param name="Name">Name to locate.</param>
		/// <returns>Found type or null if not found.</returns>
		public UhtType? FindCasedType(UhtType? StartingType, UhtFindOptions Options, string Name)
		{
			Lookup Existing;
			if (this.CasedDictionary.TryGetValue(Name, out Existing))
			{
				return FindType(StartingType, Options, Existing);
			}
			return null;
		}

		/// <summary>
		/// Lookup the given name using caseless string compare.
		/// </summary>
		/// <param name="StartingType">Starting type used to limit the scope of the search.</param>
		/// <param name="Options">Options controlling what is search and what is returned.</param>
		/// <param name="Name">Name to locate.</param>
		/// <returns>Found type or null if not found.</returns>
		public UhtType? FindCaselessType(UhtType? StartingType, UhtFindOptions Options, string Name)
		{
			int SymbolIndex;
			if (this.CaselessDictionary.TryGetValue(Name, out SymbolIndex))
			{
				return FindType(StartingType, Options, new Lookup { SymbolIndex = SymbolIndex, CasedIndex = 0 });
			}
			return null;
		}

		/// <summary>
		/// Lookup the given name.
		/// </summary>
		/// <param name="StartingType">Starting type used to limit the scope of the search.</param>
		/// <param name="Options">Options controlling what is search and what is returned.</param>
		/// <param name="Lookup">Starting lookup location.</param>
		/// <returns>Found type or null if not found.</returns>
		private UhtType? FindType(UhtType? StartingType, UhtFindOptions Options, Lookup Lookup)
		{
			if (StartingType != null)
			{
				UhtType? Found = FindTypeSuperChain(StartingType, Options, ref Lookup);
				if (Found != null)
				{
					return Found;
				}

				Found = FindTypeOuterChain(StartingType, Options, ref Lookup);
				if (Found != null)
				{
					return Found;
				}

				if (!Options.HasAnyFlags(UhtFindOptions.NoIncludes))
				{
					UhtHeaderFile HeaderFile = StartingType.HeaderFile;
					foreach (UhtHeaderFile IncludedFile in HeaderFile.IncludedHeaders)
					{
						Found = this.FindSymbolChain(IncludedFile, Options, ref Lookup);
						if (Found != null)
						{
							break;
						}
					}
					if (Found != null)
					{
						return Found;
					}
				}
			}

			// Global search.  Match anything that has an owner of parent
			if (!Options.HasAnyFlags(UhtFindOptions.NoGlobal))
			{
				for (int Index = Lookup.SymbolIndex; Index != 0; Index = this.Symbols[Index].NextIndex)
				{
					if (IsMatch(Options, Index, Lookup.CasedIndex) && this.Symbols[Index].Type.Outer is UhtHeaderFile)
					{
						return this.Symbols[Index].Type;
					}
				}
			}

			// Can't find at all
			return null;
		}

		/// <summary>
		/// Lookup the given name using the super class/struct chain.
		/// </summary>
		/// <param name="StartingType">Starting type used to limit the scope of the search.</param>
		/// <param name="Options">Options controlling what is search and what is returned.</param>
		/// <param name="Lookup">Starting lookup location.</param>
		/// <returns>Found type or null if not found.</returns>
		private UhtType? FindTypeSuperChain(UhtType StartingType, UhtFindOptions Options, ref Lookup Lookup)
		{
			UhtType? CurrentType = StartingType;

			// In a super chain search, we have to start at a UHTStruct that contributes to the symbol table
			for (; CurrentType != null; CurrentType = CurrentType.Outer)
			{
				if (CurrentType is UhtStruct Struct && Struct.EngineType.AddChildrenToSymbolTable())
				{
					break;
				}
			}

			// Not symbol that supports a super chain
			if (CurrentType == null)
			{
				return null;
			}

			// If requested, skip self
			if (Options.HasAnyFlags(UhtFindOptions.NoSelf) && CurrentType == StartingType)
			{
				CurrentType = ((UhtStruct)CurrentType).Super;
			}

			// Search the chain
			for (; CurrentType != null; CurrentType = ((UhtStruct)CurrentType).Super)
			{
				UhtType? FoundType = FindSymbolChain(CurrentType, Options, ref Lookup);
				if (FoundType != null)
				{
					return FoundType;
				}
				if (Options.HasAnyFlags(UhtFindOptions.NoParents))
				{
					return null;
				}
			}
			return null;
		}

		/// <summary>
		/// Lookup the given name using the outer chain
		/// </summary>
		/// <param name="StartingType">Starting type used to limit the scope of the search.</param>
		/// <param name="Options">Options controlling what is search and what is returned.</param>
		/// <param name="Lookup">Starting lookup location.</param>
		/// <returns>Found type or null if not found.</returns>
		private UhtType? FindTypeOuterChain(UhtType StartingType, UhtFindOptions Options, ref Lookup Lookup)
		{
			UhtType? CurrentType = StartingType;

			// If requested, skip self
			if (Options.HasAnyFlags(UhtFindOptions.NoSelf) && CurrentType == StartingType)
			{
				CurrentType = CurrentType.Outer;
			}

			// Search the chain
			for (; CurrentType != null; CurrentType = CurrentType.Outer)
			{
				if (CurrentType is UhtPackage)
				{
					return null;
				}
				UhtType? FoundType = FindSymbolChain(CurrentType, Options, ref Lookup);
				if (FoundType != null)
				{
					return FoundType;
				}
				if (Options.HasAnyFlags(UhtFindOptions.NoOuter))
				{
					return null;
				}
			}
			return null;
		}

		/// <summary>
		/// Lookup the given name 
		/// </summary>
		/// <param name="Owner">Matching owner.</param>
		/// <param name="Options">Options controlling what is search and what is returned.</param>
		/// <param name="Lookup">Starting lookup location.</param>
		/// <returns>Found type or null if not found.</returns>
		private UhtType? FindSymbolChain(UhtType Owner, UhtFindOptions Options, ref Lookup Lookup)
		{
			for (int Index = Lookup.SymbolIndex; Index != 0; Index = this.Symbols[Index].NextIndex)
			{
				if (IsMatch(Options, Index, Lookup.CasedIndex) && this.Symbols[Index].Type.Outer == Owner)
				{
					return this.Symbols[Index].Type;
				}
			}
			return null;
		}

		/// <summary>
		/// Add a new type to the given symbol chain
		/// </summary>
		/// <param name="Type">Type being added</param>
		/// <param name="CasedIndex">Cased index</param>
		/// <param name="SymbolIndex">Symbol index</param>
		private void AddExisting(UhtType Type, int CasedIndex, int SymbolIndex)
		{
			int TypeIndex = Type.TypeIndex;
			this.Symbols[TypeIndex] = new Symbol { Type = Type, MatchOptions = Type.EngineType.FindOptions(), CasedIndex = CasedIndex, NextIndex = 0, LastIndex = 0 };
			this.Symbols[this.Symbols[SymbolIndex].LastIndex].NextIndex = TypeIndex;
			this.Symbols[SymbolIndex].LastIndex = TypeIndex;
		}

		/// <summary>
		/// Test to see if the given symbol matches the options
		/// </summary>
		/// <param name="Options">Options to match</param>
		/// <param name="SymbolIndex">Symbol index</param>
		/// <param name="CasedIndex">Case index</param>
		/// <returns>True if the symbol is a match</returns>
		private bool IsMatch(UhtFindOptions Options, int SymbolIndex, int CasedIndex)
		{
			return (CasedIndex == 0 || CasedIndex == this.Symbols[SymbolIndex].CasedIndex) && 
				this.Symbols[SymbolIndex].Type.bVisibleType && 
				(this.Symbols[SymbolIndex].MatchOptions & Options) != 0;
		}
	}
}
