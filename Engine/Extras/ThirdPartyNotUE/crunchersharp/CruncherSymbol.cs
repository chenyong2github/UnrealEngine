using System.Collections.Generic;
using Dia2Lib;

namespace CruncherSharp
{
	public class CruncherSymbol
	{
		public string Name;
		public string TypeName;

		public bool IsBase;

		/** Total size of this symbol in bytes */
		public ulong Size;

		/** Offset of this symbol in bytes */
		public long Offset;

		/** Total padding of this symbol in bytes */
		public long Padding;
		public List<CruncherSymbol> m_children;

		public CruncherSymbol()
		{ }

		public CruncherSymbol(string name, string typeName, ulong size, long offset)
		{
			Set(name, typeName, size, offset);
		}

		public void Set(string name, string typeName, ulong size, long offset)
		{
			Name = name;
			TypeName = typeName;
			Size = size;
			Offset = offset;
			IsBase = Name.IndexOf("Base: ") == 0;
		}

		public void ProcessChildren(IDiaSymbol symbol)
		{
			IDiaEnumSymbols children;
			symbol.findChildren(SymTagEnum.SymTagNull, null, 0, out children);

			foreach (IDiaSymbol child in children)
			{
				CruncherSymbol childInfo;
				if (ProcessChild(child, out childInfo))
				{
					AddChild(childInfo);
				}
			}
			// Sort children by offset, recalc padding.
			// Sorting is not needed normally (for data fields), but sometimes base class order is wrong.
			if (HasChildren)
			{
				m_children.Sort(CompareOffsets);
				for (int i = 0; i < m_children.Count; ++i)
				{
					CruncherSymbol child = m_children[i];
					child.Padding = CalcPadding(child.Offset, i);
				}
				Padding = CalcPadding((long)Size, m_children.Count);
			}
		}

		private bool ProcessChild(IDiaSymbol symbol, out CruncherSymbol info)
		{
			info = new CruncherSymbol();
			if (symbol.isStatic != 0 || (symbol.symTag != (uint)SymTagEnum.SymTagData && symbol.symTag != (uint)SymTagEnum.SymTagBaseClass))
			{
				return false;
			}
			if (symbol.locationType != LocationType.IsThisRel && symbol.locationType != LocationType.IsNull && symbol.locationType != LocationType.IsBitField)
			{
				return false;
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

			info.Set(symbolName, (typeSymbol != null ? typeSymbol.name : ""), len, symbol.offset);

			return true;
		}

		private long CalcPadding(long offset, int index)
		{
			long padding = 0;
			if (HasChildren && index > 0)
			{
				CruncherSymbol lastInfo = m_children[index - 1];
				padding = offset - (lastInfo.Offset + (long)lastInfo.Size);
			}
			return padding > 0 ? padding : 0;
		}

		public bool HasChildren { get { return m_children != null; } }

		public long CalcTotalPadding()
		{
			long totalPadding = Padding;
			if (HasChildren)
			{
				foreach (CruncherSymbol info in m_children)
				{
					totalPadding += info.Padding;
				}
			}
			return totalPadding;
		}

		private void AddChild(CruncherSymbol child)
		{
			if (m_children == null)
			{
				m_children = new List<CruncherSymbol>();
			}
			m_children.Add(child);
		}

		private static int CompareOffsets(CruncherSymbol x, CruncherSymbol y)
		{
			// Base classes have to go first.
			if (x.IsBase && !y.IsBase)
			{
				return -1;
			}
			if (!x.IsBase && y.IsBase)
			{
				return 1;
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
