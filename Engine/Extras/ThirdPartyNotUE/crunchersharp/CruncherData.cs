using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Diagnostics;
using Dia2Lib;

namespace CruncherSharp
{
	/// <summary>
	/// Contains data loaded from the PDB symbols. The data 
	/// that the form represents. 
	/// </summary>
	public class CruncherData
    {
		IDiaDataSource m_DiaSource = null;
		IDiaSession m_DiaSession = null;
		Dictionary<string, CruncherSymbol> m_SymbolMap = new Dictionary<string, CruncherSymbol>();

		public CruncherData()
		{}

		/// <summary>
		/// Creates a new set of cruncher data for the given PDB
		/// </summary>
		/// <param name="FileName">Absolute file path of the PDB to load</param>
		public CruncherData(string FileName)
			: base()
        {
			LoadDataFromPdb(FileName, null);
		}

		/// <summary>
		/// Clears the symbol map and creates a new Dia source class. Sets Dia session to null
		/// </summary>
		private void Reset()
		{
			m_SymbolMap.Clear();
			// If the creation of the DiaSourceClass fails, then you likely need
			// to register the MSDIA1xx.dll on your machine. See readme for more details
			m_DiaSource = new DiaSourceClass();
			m_DiaSession = null;
		}

		/// <summary>
		///Attempt to load the PDB file from the given file location and populate the data table on the left
		/// </summary>
		/// <param name="FileName">Absolute file path of the PDB</param>
		/// <returns>True if success</returns>
		public bool LoadDataFromPdb(string FileName, BackgroundWorker LoadingWorker)
		{
			try
			{
				Reset();
				m_DiaSource.loadDataFromPdb(FileName);
                m_DiaSource.openSession(out m_DiaSession);
				
				IDiaEnumSymbols allSymbols;
				m_DiaSession.findChildren(m_DiaSession.globalScope, SymTagEnum.SymTagUDT, null, 0, out allSymbols);

				ProcessSymbols(allSymbols, LoadingWorker);
			}
			catch (System.Exception e)
			{
				Console.WriteLine("Error loading PDB {0}, full message:\n{1}", FileName, e.ToString());
				return false;
			}

			return true;
		}

		/// <summary>
		/// Parse all Dia symbols and put them into a map
		/// </summary>
		/// <param name="symbols">Dia symbols to load</param>
		private void ProcessSymbols(IDiaEnumSymbols symbols, BackgroundWorker LoadingWorker)
		{
			int TotalSymbolCount = symbols.count;
			int CurSymIndex = 0;

			string msg = String.Format("Loading {0} symbols...", TotalSymbolCount);
			Console.WriteLine(msg);
			LoadingWorker?.ReportProgress(0, msg);

			System.Diagnostics.Stopwatch watch = new Stopwatch();
			watch.Start();

			foreach (IDiaSymbol sym in symbols)
			{
				if (sym.length > 0 && !HasSymbol(sym.name))
				{
					m_SymbolMap.Add(
						sym.name,
						new CruncherSymbol(sym.name, sym.length, /* offset= */ 0, sym)
					);

					// Report progress to loading bar
					int percentProgress = (int)Math.Round((double)(100 * CurSymIndex++) / TotalSymbolCount);
					percentProgress = Math.Max(Math.Min(percentProgress, 99), 1);
					LoadingWorker?.ReportProgress(percentProgress, String.Format("Adding symbol {0} of {1}", CurSymIndex, TotalSymbolCount));
				}
			}

			watch.Stop();

			// Format and display the TimeSpan value.
			TimeSpan ts = watch.Elapsed;
			string elapsedTime = String.Format("{0:00}:{1:00}:{2:00}.{3:00}",
				ts.Hours, ts.Minutes, ts.Seconds,
				ts.Milliseconds / 10);
			string CompleteMessage = String.Format("Finished processing {0} symbols in {1}", TotalSymbolCount, elapsedTime);
			
			Console.WriteLine(CompleteMessage);
			LoadingWorker?.ReportProgress(100, CompleteMessage);
		}

		/// <summary>
		/// Clears the given table and adds a DataRow for each Cruncher symbol in the map
		/// </summary>
		/// <param name="table"></param>
		public void PopulateDataTable(DataTable table)
		{
			table.Rows.Clear();

			table.BeginLoadData();

			foreach (KeyValuePair<string, CruncherSymbol> Pair in m_SymbolMap)
			{
				CruncherSymbol info = Pair.Value;
				DataRow row = table.NewRow();

				row["Symbol"] = info.Name;
				row["Size"] = info.Size;
				row["Padding"] = info.TotalPadding;
				row["Padding/Size"] = (double)info.TotalPadding / info.Size;
				table.Rows.Add(row);
			}

			table.EndLoadData();
		}

        public bool HasSymbol(string name)
        {
            return m_SymbolMap.ContainsKey(name);
        }

        public CruncherSymbol FindSymbolInfo(string name)
        {
			CruncherSymbol info;
            m_SymbolMap.TryGetValue(name, out info);
            return info;
        }

        public void dumpSymbolInfo(System.IO.TextWriter tw, CruncherSymbol info)
        {
            tw.WriteLine("Symbol: " + info.Name);
            tw.WriteLine("Size: " + info.Size.ToString());
            tw.WriteLine("Total padding: " + info.TotalPadding.ToString());
            tw.WriteLine("Members");
            tw.WriteLine("-------");

            foreach (CruncherSymbol child in info.Children)
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
