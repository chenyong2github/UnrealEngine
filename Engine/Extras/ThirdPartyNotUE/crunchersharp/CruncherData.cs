using System;
using System.Collections.Generic;
using System.Data;
using System.Diagnostics;
using Dia2Lib;
using System.Threading;
using System.Threading.Tasks;

namespace CruncherSharp
{			
    public static class LocationType
    {
        public static uint IsNull       = 0;
        public static uint IsThisRel    = 4;
        public static uint IsBitField   = 6;
    }

    public class CruncherData
    {
        public CruncherData()
        {
			m_table = CreateDataTable();
        }

		public string loadDataFromPdb(string FileName)
		{
			m_symbols.Clear();
            Stopwatch watch;

            m_source = new DiaSourceClass();
			try
			{
                watch = System.Diagnostics.Stopwatch.StartNew();

                m_source.loadDataFromPdb(FileName);
                watch.Stop();
                long elapsedMs = watch.ElapsedMilliseconds;
                Console.WriteLine("Loading data from {0} took {1}ms ({2} sec.)", FileName, elapsedMs, (elapsedMs * 1000));

                m_source.openSession(out m_session);
			}
			catch (System.Runtime.InteropServices.COMException exc)
			{
				return exc.ToString();
			}

			IDiaEnumSymbols allSymbols;
			m_session.findChildren(m_session.globalScope, SymTagEnum.SymTagUDT, null, 0, out allSymbols);

			{
                watch = System.Diagnostics.Stopwatch.StartNew();

                PopulateDataTable(m_table, allSymbols);

                watch.Stop();
                long elapsedMs = watch.ElapsedMilliseconds;
                Console.WriteLine("PopulateDataTable took took {0}ms ({1} sec.)", elapsedMs, (elapsedMs * 1000));
            }

			return null;
		}

        DataTable CreateDataTable()
        {
            DataTable table = new DataTable("Symbols");

            DataColumn column = new DataColumn();
            column.ColumnName = "Symbol";
            column.ReadOnly = true;
            table.Columns.Add(column);
            column = new DataColumn();
            column.ColumnName = "Size";
            column.ReadOnly = true;
            column.DataType = System.Type.GetType("System.Int32");
            table.Columns.Add(column);
            column = new DataColumn();
            column.ColumnName = "Padding";
            column.ReadOnly = true;
            column.DataType = System.Type.GetType("System.Int32");
            table.Columns.Add(column);
            column = new DataColumn();
            column.ColumnName = "Padding/Size";
            column.ReadOnly = true;
            column.DataType = System.Type.GetType("System.Double");
            table.Columns.Add(column);

            return table;
        }

        void PopulateDataTable(DataTable table, IDiaEnumSymbols symbols)
        {
            table.Rows.Clear();

			table.BeginLoadData();
            foreach (IDiaSymbol sym in symbols)
            {
                if (sym.length > 0 && !HasSymbol(sym.name))
                {
                    SymbolInfo info = new SymbolInfo(sym.name, "", sym.length, 0);
                    info.ProcessChildren(sym);

                    long totalPadding = info.CalcTotalPadding();

                    DataRow row = table.NewRow();
                    string symbolName = sym.name;
                    row["Symbol"] = symbolName;
                    row["Size"] = info.m_size;
                    row["Padding"] = totalPadding;
                    row["Padding/Size"] = (double)totalPadding / info.m_size;
                    table.Rows.Add(row);

                    m_symbols.Add(info.m_name, info);
                }
            }
			table.EndLoadData();

        }

        public bool HasSymbol(string name)
        {
            return m_symbols.ContainsKey(name);
        }

        public SymbolInfo FindSymbolInfo(string name)
        {
            SymbolInfo info;
            m_symbols.TryGetValue(name, out info);
            return info;
        }

        IDiaDataSource m_source;
        IDiaSession m_session;
        Dictionary<string, SymbolInfo> m_symbols = new Dictionary<string, SymbolInfo>();
        public DataTable m_table = null;

        public void dumpSymbolInfo(System.IO.TextWriter tw, SymbolInfo info)
        {
            tw.WriteLine("Symbol: " + info.m_name);
            tw.WriteLine("Size: " + info.m_size.ToString());
            tw.WriteLine("Total padding: " + info.CalcTotalPadding().ToString());
            tw.WriteLine("Members");
            tw.WriteLine("-------");

            foreach (SymbolInfo child in info.m_children)
            {
                if (child.m_padding > 0)
                {
                    long paddingOffset = child.m_offset - child.m_padding;
                    tw.WriteLine(String.Format("{0,-40} {1,5} {2,5}", "****Padding", paddingOffset, child.m_padding));
                }

                tw.WriteLine(String.Format("{0,-40} {1,5} {2,5}", child.m_name, child.m_offset, child.m_size));
            }
            // Final structure padding.
            if (info.m_padding > 0)
            {
                long paddingOffset = (long)info.m_size - info.m_padding;
                tw.WriteLine(String.Format("{0,-40} {1,5} {2,5}", "****Padding", paddingOffset, info.m_padding));
            }
        }
	}

	public class SymbolInfo
	{
        public SymbolInfo()
        {

        }

        public SymbolInfo(string name, string typeName, ulong size, long offset)
        {
            Set(name, typeName, size, offset);
        }

        public void Set(string name, string typeName, ulong size, long offset)
        {
            m_name = name;
            m_typeName = typeName;
            m_size = size;
            m_offset = offset;
            m_IsBase = m_name.IndexOf("Base: ") == 0;
        }

        public void ProcessChildren(IDiaSymbol symbol)
		{
			IDiaEnumSymbols children;
			symbol.findChildren(SymTagEnum.SymTagNull, null, 0, out children);

			foreach (IDiaSymbol child in children)
			{
				SymbolInfo childInfo;
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
					SymbolInfo child = m_children[i];
					child.m_padding = CalcPadding(child.m_offset, i);
				}
				m_padding = CalcPadding((long)m_size, m_children.Count);
			}
		}

		private bool ProcessChild(IDiaSymbol symbol, out SymbolInfo info)
		{
			info = new SymbolInfo();
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
				SymbolInfo lastInfo = m_children[index - 1];
				padding = offset - (lastInfo.m_offset + (long)lastInfo.m_size);
			}
			return padding > 0 ? padding : 0;
		}

		public bool HasChildren { get { return m_children != null; } }
		
        public long CalcTotalPadding()
		{
			long totalPadding = m_padding;
			if (HasChildren)
			{
				foreach (SymbolInfo info in m_children)
				{
					totalPadding += info.m_padding;
				}
			}
			return totalPadding;
		}
		
        private void AddChild(SymbolInfo child)
		{
			if (m_children == null)
			{
				m_children = new List<SymbolInfo>();
			}
			m_children.Add(child);
		}

		private static int CompareOffsets(SymbolInfo x, SymbolInfo y)
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

			if (x.m_offset == y.m_offset)
			{
				return (x.m_size == y.m_size ? 0 : (x.m_size < y.m_size) ? -1 : 1);
			}
			else
			{
				return (x.m_offset < y.m_offset) ? -1 : 1;
			}
		}

		public string m_name;
		public string m_typeName;

        private bool m_IsBase;
        public bool IsBase { get { return m_IsBase; } }

        /** Total size of this symbol in bytes */
		public ulong m_size;
        
        /** Offset of this symbol in bytes */
		public long m_offset;

		/** Total padding of this symbol in bytes */
        public long m_padding;
		public List<SymbolInfo> m_children;
	};
}
