using System;
using System.Collections.Generic;
using System.Data;
using System.Diagnostics;
using Dia2Lib;

namespace CruncherSharp
{
	/**
	* Contains data loaded from the PDB symbols 
	*/
    public class CruncherData
    {
		IDiaDataSource m_source;
		IDiaSession m_session;
		Dictionary<string, SymbolInfo> m_symbols = new Dictionary<string, SymbolInfo>();
		public DataTable m_table = null;

		public CruncherData()
        {
			m_table = CreateDataTable();
        }

		/**
		* @brief	Attempt to load the PDB file from the given file location and populate the data table on the left 
		*/
		public string LoadDataFromPdb(string FileName)
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

		/**
		* @brief	Create the data table that displays on the left hand side of the screen
		*			where each symbol name will be displayed, along with it's summary.
		*/
        private static DataTable CreateDataTable()
        {
            DataTable table = new DataTable("Symbols");

			table.Columns.Add(
				new DataColumn("Symbol") { ReadOnly = true }
			);

			table.Columns.Add(
				new DataColumn("Size", System.Type.GetType("System.Int32")) { ReadOnly = true }
			);

			table.Columns.Add(
				new DataColumn("Padding", System.Type.GetType("System.Int32")) { ReadOnly = true }
			);

			table.Columns.Add(
				new DataColumn("Padding/Size", System.Type.GetType("System.Double")) { ReadOnly = true }
			);
            
            return table;
        }

        private void PopulateDataTable(DataTable table, IDiaEnumSymbols symbols)
        {
            table.Rows.Clear();

			table.BeginLoadData();
            foreach (IDiaSymbol sym in symbols)
            {
                if (sym.length > 0 && !HasSymbol(sym.name))
                {
                    SymbolInfo info = new SymbolInfo(sym.name, "", sym.length, 0, sym);

                    long totalPadding = info.CalcTotalPadding();

                    DataRow row = table.NewRow();
                    string symbolName = sym.name;
                    row["Symbol"] = symbolName;
                    row["Size"] = info.Size;
                    row["Padding"] = totalPadding;
                    row["Padding/Size"] = (double)totalPadding / info.Size;
                    table.Rows.Add(row);

                    m_symbols.Add(info.Name, info);
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

        public void dumpSymbolInfo(System.IO.TextWriter tw, SymbolInfo info)
        {
            tw.WriteLine("Symbol: " + info.Name);
            tw.WriteLine("Size: " + info.Size.ToString());
            tw.WriteLine("Total padding: " + info.CalcTotalPadding().ToString());
            tw.WriteLine("Members");
            tw.WriteLine("-------");

            foreach (SymbolInfo child in info.Children)
            {
                if (child.Padding > 0)
                {
                    long paddingOffset = child.Offset - child.Padding;
                    tw.WriteLine(String.Format("{0,-40} {1,5} {2,5}", "****Padding", paddingOffset, child.Padding));
                }

                tw.WriteLine(String.Format("{0,-40} {1,5} {2,5}", child.Name, child.Offset, child.Size));
            }
            // Final structure padding.
            if (info.Padding > 0)
            {
                long paddingOffset = (long)info.Size - info.Padding;
                tw.WriteLine(String.Format("{0,-40} {1,5} {2,5}", "****Padding", paddingOffset, info.Padding));
            }
        }
	}
}
