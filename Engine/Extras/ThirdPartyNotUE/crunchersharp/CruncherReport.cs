using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using Microsoft.VisualBasic.FileIO;

namespace CruncherSharp
{
	class CruncherReport
	{
		public CruncherReport(string PDBFile, string CSVFile)
		{
			m_CruncherData = new CruncherData();
			string result = m_CruncherData.loadDataFromPdb(PDBFile);
			if (result != null)
			{
				System.Console.WriteLine(result);
				return;
			}

			List<Tuple<string, ulong, ulong>> SymbolsToValidate = new List<Tuple<string, ulong, ulong>>();
			TextFieldParser parser = new TextFieldParser(CSVFile);
			parser.Delimiters = new String[] { "," };
			while(true)
			{
				string[] elements = parser.ReadFields();
				if (elements == null)
				{
					break;
				}
				SymbolsToValidate.Add(new Tuple<string, ulong, ulong>(elements[0], Convert.ToUInt64(elements[1]), Convert.ToUInt64(elements[2])));
			}
			
			foreach(Tuple<string, ulong, ulong> SymbolToValidate in SymbolsToValidate)
			{
				SymbolInfo Info = m_CruncherData.FindSymbolInfo(SymbolToValidate.Item1);
				if (Info == null)
				{
					System.Console.WriteLine("ERROR: Unable to find symbol '" + SymbolToValidate.Item1 + "' in PDB");
				}
				else
				{
					if (Info.m_size > SymbolToValidate.Item2)
					{
						string Larger = "WARNING: Symbol '"
										+ SymbolToValidate.Item1
										+ "'was expected to be "
										+ SymbolToValidate.Item2
										+ " bytes with "
										+ SymbolToValidate.Item3
										+ " bytes of padding, however it is "
										+ Info.m_size
										+ " bytes with "
										+ Info.m_padding
										+ " bytes of padding. Inspect symbol to ensure no unneeded size regressions have occurred and update expected symbol size csv as needed.";
						System.Console.WriteLine(Larger);
					}
					else if (Info.m_size < SymbolToValidate.Item2)
					{
						string Smaller = "Symbol '"
										+ SymbolToValidate.Item1
										+ "'was expected to be "
										+ SymbolToValidate.Item2
										+ " bytes with "
										+ SymbolToValidate.Item3
										+ " bytes of padding, however it is "
										+ Info.m_size
										+ " bytes with "
										+ Info.m_padding
										+ " bytes of padding. Consider updating symbol size csv as to maintain size savings.";
						System.Console.WriteLine(Smaller);
					}
				}
			}
		}

		CruncherData m_CruncherData;

	}
}
