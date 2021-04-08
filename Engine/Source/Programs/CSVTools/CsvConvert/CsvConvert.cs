// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Diagnostics;
using System.IO.Compression;
using CSVStats;

namespace CSVInfo
{
    class Version
    {
        private static string VersionString = "1.01";

        public static string Get() { return VersionString; }
    };

    class Program : CommandLineTool
    {
        static int Main(string[] args)
        {
            Program program = new Program();
            if (Debugger.IsAttached)
            {
                program.Run(args);
            }
            else
            {
                try
                {
                    program.Run(args);
                }
                catch (System.Exception e)
                {
                    Console.WriteLine("[ERROR] " + e.Message);
                    return 1;
                }
            }

            return 0;
        }

		static bool isCsvFile(string filename)
		{
			return filename.ToLowerInvariant().EndsWith(".csv");
		}
		static bool isCsvBinFile(string filename)
		{
			return filename.ToLowerInvariant().EndsWith(".csv.bin");
		}

		void Run(string[] args)
        {
			string formatString =
				"Format: \n" +
				"  <filename>\n" +
				"OR \n" +
				"  -in <filename>\n" +
				"  -outFormat=<csv|bin|csvNoMetadata>\n" +
				"  [-binCompress=<0|1|2>] (0=none)\n" +
				"  [-o outFilename]\n" +
				"  [-force]\n"+
				"  [-verify]\n"+
				"  [-dontFixMissingID] - doesn't generate a CsvID if it's not found in the metadata\n";


			// Read the command line
			if (args.Length < 1)
            {
                WriteLine(formatString);
                return;
            }


            ReadCommandLine(args);

			string inFilename;
			string outFormat;
			string outFilename = null;
			if (args.Length==1)
			{
				inFilename = args[0];
				// Determine the output format automatically
				if (isCsvBinFile(inFilename))
				{
					outFormat = "csv";
				}
				else if (isCsvFile(inFilename))
				{
					outFormat = "bin";
				}
				else
				{
					throw new Exception("Unknown input format!");
				}
			}
			else
			{
				// Advanced mode. Output format is required
				inFilename = GetArg("in",true);
				outFormat = GetArg("outformat", true);
				if (outFormat == "" || inFilename == "")
				{
					return;
				}
				outFilename = GetArg("o", GetArg("out", null));
			}

			outFormat = outFormat.ToLower();
			bool bBinOut=false;
			bool bWriteMetadata = true;
			if (outFormat == "bin")
			{
				bBinOut = true;
			}
			else if (outFormat=="csv")
			{
				bBinOut = false;
			}
			else if (outFormat == "csvnometadata")
			{
				bBinOut = false;
				bWriteMetadata = false;
			}
			else
			{
				throw new Exception("Unknown output format!");
			}

			string inFilenameWithoutExtension;
			if (isCsvBinFile(inFilename))
			{
				inFilenameWithoutExtension = inFilename.Substring(0, inFilename.Length - 8);
			}
			else if (isCsvFile(inFilename))
			{
				inFilenameWithoutExtension = inFilename.Substring(0, inFilename.Length - 4);
			}
			else
			{
				throw new Exception("Unexpected input filename extension!");
			}
			
			if (outFilename == null)
			{
				outFilename = inFilenameWithoutExtension + (bBinOut ? ".csv.bin" : ".csv");
			}

			if (outFilename.ToLowerInvariant() == inFilename.ToLowerInvariant())
			{
				throw new Exception("Input and output filenames can't match!");
			}

			if (System.IO.File.Exists(outFilename) && !GetBoolArg("force"))
			{
				throw new Exception("Output file already exists! Use -force to overwrite anyway.");
			}

			Console.WriteLine("Converting "+inFilename+" to "+outFormat+" format. Output filename: "+outFilename);
			Console.WriteLine("Reading input file...");

			bool bFixMissingCsvId = !GetBoolArg("dontFixMissingID");
			CsvStats csvStats = CsvStats.ReadCSVFile(inFilename, null, 0, bFixMissingCsvId);
			Console.WriteLine("Writing output file...");
			if (bBinOut)
			{
				int binCompression = GetIntArg("binCompress", 0);
				CompressionLevel[] compressionLevels = { CompressionLevel.NoCompression, CompressionLevel.Fastest, CompressionLevel.Optimal };
				if (binCompression < 0 || binCompression > 2)
				{
					throw new Exception("Bad compression level specified! Must be 0, 1 or 2");
				}
				csvStats.WriteBinFile(outFilename, (CSVStats.CsvBinCompressionLevel)binCompression);
			}
			else
			{
				csvStats.WriteToCSV(outFilename, bWriteMetadata);
			}

			if (GetBoolArg("verify"))
			{
				// TODO: if verify is specified, use a temp intermediate file?
				Console.WriteLine("Verifying output...");
				CsvStats csvStats2 = CsvStats.ReadCSVFile(outFilename,null);
				if (csvStats.SampleCount != csvStats2.SampleCount)
				{
					throw new Exception("Verify failed! Sample counts don't match");
				}
				double sum1 = 0.0;
				double sum2 = 0.0;
				foreach (StatSamples stat in csvStats.Stats.Values)
				{
					StatSamples stat2 = csvStats2.GetStat(stat.Name);

					if (stat2==null)
					{
						throw new Exception("Verify failed: missing stat: "+stat.Name);
					}
					for ( int i=0; i<csvStats.SampleCount; i++ )
					{
						sum1 += stat.samples[i];
						sum2 += stat2.samples[i];
					}
				}
				if (sum1 != sum2)
				{
					if (bBinOut == false)
					{
						// conversion to CSV is lossy. Just check the sums are within 0.25%
						double errorMargin = Math.Abs(sum1) * 0.0025;
						if (Math.Abs(sum1-sum2)> errorMargin)
						{
							throw new Exception("Verify failed: stat sums didn't match!");
						}
					}
					else
					{
						throw new Exception("Verify failed: stat sums didn't match!");
					}
				}
				// conversion to CSV is lossy. Hashes won't match because stat totals will be slightly different, so skip this check
				if (bBinOut)
				{
					if (csvStats.MakeCsvIdHash() != csvStats2.MakeCsvIdHash())
					{
						throw new Exception("Verify failed: hashes didn't match");
					}
				}
				Console.WriteLine("Verify success: Input and output files matched");
			}
		}
	}
}
