using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Diagnostics;
using Dia2Lib;

namespace CruncherSharp
{

    public class CruncherData
    {
		IDiaDataSource m_DiaSource = null;
		IDiaSession m_DiaSession = null;
		Dictionary<string, CruncherSymbol> m_SymbolMap = new Dictionary<string, CruncherSymbol>();
		public DataTable m_table = null;

		public CruncherData(DataTable table)
        {
			m_table = table;
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
		/// Attempt to load the PDB file from the given file location and populate the data table on the left
		/// </summary>
		/// <param name="FileName">Absolute file path of the PDB</param>
		/// <returns>Error message if failure</returns>
		public string LoadDataFromPdb(string FileName, BackgroundWorker LoadingWorker)
		{
			m_SymbolMap.Clear();
            Stopwatch watch;

			if(FileName == null)
			{
				return "Empty file path provided! ";
			}

            m_DiaSource = new DiaSourceClass();
			try
			{
				Reset();
				watch = System.Diagnostics.Stopwatch.StartNew();

                m_DiaSource.loadDataFromPdb(FileName);
                watch.Stop();
                long elapsedMs = watch.ElapsedMilliseconds;
                Console.WriteLine("Loading data from {0} took {1}ms ({2} sec.)", FileName, elapsedMs, (elapsedMs * 1000));

                m_DiaSource.openSession(out m_DiaSession);
			}
			catch (System.Runtime.InteropServices.COMException exc)
			{
				return exc.ToString();
			}

			try
			{
				IDiaEnumSymbols allSymbols;
				m_DiaSession.findChildren(m_DiaSession.globalScope, SymTagEnum.SymTagUDT, null, 0, out allSymbols);
				{
					watch = System.Diagnostics.Stopwatch.StartNew();

					PopulateDataTable(m_table, allSymbols, LoadingWorker);

					watch.Stop();
					long elapsedMs = watch.ElapsedMilliseconds;
					Console.WriteLine("PopulateDataTable took took {0}ms ({1} sec.)", elapsedMs, (elapsedMs * 1000));
				}
			}
			catch(System.Exception e)
			{
				Console.WriteLine("Error populating data table: " + e.Message);
			}

			return null;
		}

        void PopulateDataTable(DataTable table, IDiaEnumSymbols symbols, BackgroundWorker LoadingWorker)
        {
			int TotalSymbolCount = symbols.count;
			int CurSymIndex = 0;
			
			string msg = String.Format("Loading {0} symbols...", TotalSymbolCount);
			Console.WriteLine(msg);
			LoadingWorker?.ReportProgress(0, msg);
			
			System.Diagnostics.Stopwatch watch = new Stopwatch();
			watch.Start();

			table.BeginLoadData();
            foreach (IDiaSymbol sym in symbols)
            {
                if (sym.length > 0 && !HasSymbol(sym.name))
                {
                    CruncherSymbol info = new CruncherSymbol(sym.name, "", sym.length, 0);
                    info.ProcessChildren(sym);

                    long totalPadding = info.CalcTotalPadding();

                    DataRow row = table.NewRow();
                    string symbolName = sym.name;
                    row["Symbol"] = symbolName;
                    row["Size"] = info.Size;
                    row["Padding"] = totalPadding;
                    row["Padding/Size"] = (double)totalPadding / info.Size;
                    table.Rows.Add(row);

                    m_SymbolMap.Add(info.Name, info);

					// Report progress to loading bar
					int percentProgress = (int)Math.Round((double)(100 * CurSymIndex++) / TotalSymbolCount);
					percentProgress = Math.Max(Math.Min(percentProgress, 99), 1);
					LoadingWorker?.ReportProgress(percentProgress, String.Format("Adding symbol {0} of {1}", CurSymIndex, TotalSymbolCount));
				}
            }
			table.EndLoadData();
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
            tw.WriteLine("Total padding: " + info.CalcTotalPadding().ToString());
            tw.WriteLine("Members");
            tw.WriteLine("-------");

            foreach (CruncherSymbol child in info.m_children)
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
