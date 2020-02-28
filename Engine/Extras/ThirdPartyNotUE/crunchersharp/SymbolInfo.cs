using System.Collections.Generic;
using Dia2Lib;

namespace CruncherSharp
{
	/**
	* @brief	Represents data inside of a symbol loaded from a PDB. 
	*			Used to calculate padding, offsets, and child symbols
	*/
	public class SymbolInfo
	{
		public string Name { get; }
		public string TypeName { get; }

		public bool IsBase { get; }

		/** Total size of this symbol in bytes */
		public ulong Size { get; set; }

		/** Offset of this symbol in bytes */
		public long Offset { get; set; }

		/** Total padding of this symbol in bytes */
		public long Padding { get; set; }
		public List<SymbolInfo> Children { get; }

		public bool HasChildren { get { return Children.Count > 0; } }

		public SymbolInfo(string name, string typeName, ulong size, long offset, IDiaSymbol symbol)
		{
			Name = name;
			TypeName = typeName;
			Size = size;
			Offset = offset;
			IsBase = Name.IndexOf("Base: ") == 0;			
			Children = new List<SymbolInfo>();

			if(symbol != null)
			{
				ProcessChildren(symbol);
			}
		}

		public SymbolInfo(IDiaSymbol symbol)
		{
			if (symbol == null)
			{
				throw new System.NullReferenceException("ERROR: Symbol info is null");
			}

			Name = symbol.name;
			Size = symbol.length;
			ProcessChildren(symbol);

			TypeName = "";	
			Offset = 0;
			Children = new List<SymbolInfo>();
		}

		private void ProcessChildren(IDiaSymbol symbol)
		{
			IDiaEnumSymbols children;
			symbol.findChildren(SymTagEnum.SymTagNull, null, 0, out children);

			foreach (IDiaSymbol child in children)
			{
				SymbolInfo childInfo = ProcessChild(child);
				if (childInfo != null)
				{
					Children.Add(childInfo);
				}
			}

			// Sort children by offset, recalculate padding.
			// Sorting is not needed normally (for data fields), but sometimes base class order is wrong.
			if (HasChildren)
			{
				Children.Sort(CompareOffsets);
				for (int i = 0; i < Children.Count; ++i)
				{
					SymbolInfo child = Children[i];
					child.Padding = CalcPadding(child.Offset, i);
				}
				Padding = CalcPadding((long)Size, Children.Count);
			}
		}

		/**
		*  If this symbol is a basic type in C/C++ we can extract that data to a string version
		*  See https://docs.microsoft.com/en-us/visualstudio/debugger/debug-interface-access/basictype?view=vs-2015&redirectedfrom=MSDN
		*  for more definitions. This can work for function return types/paramaters, or regular member 
		*  variables
		*/
		public static string GetBaseType(IDiaSymbol typeSymbol)
		{
			switch (typeSymbol.baseType)
			{
				case 0:
					return string.Empty;
				case 1:
					return "void";
				case 2:
					return "char";
				case 3:
					return "wchar";
				// signed int
				case 6:
					{
						switch (typeSymbol.length)
						{
							case 1:
								return "int8";
							case 2:
								return "int16";
							case 4:
								return "int32";
							case 8:
								return "int64";
							default:
								return "int";
						}
					}
				// unsigned int
				case 7:
					switch (typeSymbol.length)
					{
						case 1:
							return "uint8";
						case 2:
							return "uint16";
						case 4:
							return "uint32";
						case 8:
							return "uint64";
						default:
							return "uint";
					}
				case 8:
					return "float";
				case 9:
					return "BCS";
				case 10:
					return "bool";
				case 13:
					return "int32";
				case 14:
					return "uint32";
				case 29:
					return "bit";
				default:
					return $"Unhandled: {typeSymbol.baseType}";
			}
		}

		/**
		* @brief	 Returns true if this is a valid symbol for us to display
		*/
		private static bool IsValidDIASymbol(IDiaSymbol symbol)
		{
			if (symbol.isStatic != 0 || (symbol.symTag != (uint)SymTagEnum.SymTagData && symbol.symTag != (uint)SymTagEnum.SymTagBaseClass))
			{
				return false;
			}

			if (symbol.locationType != LocationType.IsThisRel && symbol.locationType != LocationType.IsNull && symbol.locationType != LocationType.IsBitField)
			{
				return false;
			}

			return true;
		}

		/**
		* 
		*/
		private SymbolInfo ProcessChild(IDiaSymbol symbol)
		{
			if(!IsValidDIASymbol(symbol))
			{
				return null;
			}

			ulong len = symbol.length;
			IDiaSymbol typeSymbol = symbol.type;
			if (typeSymbol != null)
			{
				len = typeSymbol.length;
			}

			string symbolName = symbol.name;
			if (symbol.symTag == (uint)SymTagEnum.SymTagBaseClass)
			{
				symbolName = "Base: " + symbolName;
			}

			return new SymbolInfo(symbolName, (typeSymbol != null ? typeSymbol.name : ""), len, symbol.offset, null);
		}

		/** Padding of only this local symbol, does not include children */
		private long CalcPadding(long offset, int index)
		{
			long padding = 0;
			if (HasChildren && index > 0)
			{
				SymbolInfo lastInfo = Children[index - 1];
				padding = offset - (lastInfo.Offset + (long)lastInfo.Size);
			}
			return padding > 0 ? padding : 0;
		}

		/** Total padding of this symbol which includes all children */
		public long CalcTotalPadding()
		{
			long totalPadding = Padding;
			if (HasChildren)
			{
				foreach (SymbolInfo info in Children)
				{
					totalPadding += info.Padding;
				}
			}
			return totalPadding;
		}

		/**
		* @brief	Sorting method used for comparing two symbols
		*/
		private static int CompareOffsets(SymbolInfo x, SymbolInfo y)
		{
			// Base classes have to go first.
			if ((x.IsBase && !y.IsBase) || (!x.IsBase && y.IsBase))
			{
				return -1;
			}

			if (x.Offset == y.Offset)
			{
				return (x.Size == y.Size ? 0 : (x.Size < y.Size) ? -1 : 1);
			}
			else
			{
				return (x.Offset < y.Offset) ? -1 : 1;
			}
		}
	};
}
