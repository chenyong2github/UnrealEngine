using System;
using System.Collections.Generic;
using System.Linq;
using System.Windows.Forms;

namespace CruncherSharp
{
    static class Program
    {
        /// <summary>
        /// The main entry point for the application.
        /// </summary>
        [STAThread]
        static void Main(string[] args)
        {
			if (args.Length > 0)
			{
				// Report mode
				if (args.Length != 2)
				{
					System.Console.WriteLine("Report mode for CruncherSharp takes 2 arguments. CruncherSharp <PDBToLoad.pdb> <SymbolsToValidate.csv>");
					return;
				}

				new CruncherReport(args[0], args[1]);
			}
			else
			{
				Application.EnableVisualStyles();
				Application.SetCompatibleTextRenderingDefault(false);
				Application.Run(new CruncherSharp_Form());
			}
        }
    }
}
