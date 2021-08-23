// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Collections;
using System.Reflection;

using SolidWorks.Interop.sldworks;
using SolidWorks.Interop.swpublished;
using SolidWorks.Interop.swconst;
using SolidWorksTools;
using SolidWorksTools.File;
using System.Collections.Generic;
using System.Diagnostics;
using SolidworksDatasmith.Engine;

namespace SolidworksDatasmith
{
	/// <summary>
	/// Summary description for SolidworksDatasmith.
	/// </summary>
	[Guid("6d79a432-9aa3-4457-aefd-ade6f2b17cce"), ComVisible(true)]
	[SwAddin(
		Description = "SolidworksDatasmith description",
		Title = "SolidworksDatasmith",
		LoadAtStartup = true
		)]
	public class SwAddin : ISwAddin
	{
		public static SwAddin Instance;

		private UserProgressBar ProgressBar = null;
		private object ProgressLock = new object();

		#region Local Variables
		ISldWorks iSwApp = null;
		ICommandManager iCmdMgr = null;
		int addinID = 0;
		BitmapHandler iBmp;

		public const int mainCmdGroupID = 5;
		public const int mainItemID1 = 0;
		public const int mainItemID2 = 1;
		public const int mainItemID3 = 2;
		public const int flyoutGroupID = 91;

		#region Event Handler Variables
		Hashtable openDocs = new Hashtable();
		SolidWorks.Interop.sldworks.SldWorks SwEventPtr = null;
		#endregion

		#region Property Manager Variables
		UserPMPage ppage = null;
		#endregion


		// Public Properties
		public ISldWorks SwApp
		{
			get { return iSwApp; }
		}
		public ICommandManager CmdMgr
		{
			get { return iCmdMgr; }
		}

		public Hashtable OpenDocs
		{
			get { return openDocs; }
		}

		#endregion

		#region SolidWorks Registration
		[ComRegisterFunctionAttribute]
		public static void RegisterFunction(Type t)
		{
			#region Get Custom Attribute: SwAddinAttribute
			SwAddinAttribute SWattr = null;
			Type type = typeof(SwAddin);

//#if DEBUG
//            int processId = Process.GetCurrentProcess().Id;
//            string message = string.Format("Please attach the debugger (elevated on Vista or Win 7) to process [{0}].", processId);
//            MessageBox.Show(message, "Debug");
//#endif

			foreach (System.Attribute attr in type.GetCustomAttributes(false))
			{
				if (attr is SwAddinAttribute)
				{
					SWattr = attr as SwAddinAttribute;
					break;
				}
			}

			#endregion

			try
			{
				Microsoft.Win32.RegistryKey hklm = Microsoft.Win32.Registry.LocalMachine;
				Microsoft.Win32.RegistryKey hkcu = Microsoft.Win32.Registry.CurrentUser;

				string keyname = "SOFTWARE\\SolidWorks\\Addins\\{" + t.GUID.ToString() + "}";
				Microsoft.Win32.RegistryKey addinkey = hklm.CreateSubKey(keyname);
				addinkey.SetValue(null, 0);

				addinkey.SetValue("Description", SWattr.Description);
				addinkey.SetValue("Title", SWattr.Title);

				keyname = "Software\\SolidWorks\\AddInsStartup\\{" + t.GUID.ToString() + "}";
				addinkey = hkcu.CreateSubKey(keyname);
				addinkey.SetValue(null, Convert.ToInt32(SWattr.LoadAtStartup), Microsoft.Win32.RegistryValueKind.DWord);
			}
			catch (System.NullReferenceException nl)
			{
				Console.WriteLine("There was a problem registering this dll: SWattr is null. \n\"" + nl.Message + "\"");
				System.Windows.Forms.MessageBox.Show("There was a problem registering this dll: SWattr is null.\n\"" + nl.Message + "\"");
			}

			catch (System.Exception e)
			{
				Console.WriteLine(e.Message);

				System.Windows.Forms.MessageBox.Show("There was a problem registering the function: \n\"" + e.Message + "\"");
			}
		}

		[ComUnregisterFunctionAttribute]
		public static void UnregisterFunction(Type t)
		{
//#if DEBUG
//            int processId = Process.GetCurrentProcess().Id;
//            string message = string.Format("Please attach the debugger (elevated on Vista or Win 7) to process [{0}].", processId);
//            MessageBox.Show(message, "Debug");
//#endif
			try
			{
				Microsoft.Win32.RegistryKey hklm = Microsoft.Win32.Registry.LocalMachine;
				Microsoft.Win32.RegistryKey hkcu = Microsoft.Win32.Registry.CurrentUser;

				string keyname = "SOFTWARE\\SolidWorks\\Addins\\{" + t.GUID.ToString() + "}";
				hklm.DeleteSubKey(keyname);

				keyname = "Software\\SolidWorks\\AddInsStartup\\{" + t.GUID.ToString() + "}";
				hkcu.DeleteSubKey(keyname);
			}
			catch (System.NullReferenceException nl)
			{
				Console.WriteLine("There was a problem unregistering this dll: " + nl.Message);
				System.Windows.Forms.MessageBox.Show("There was a problem unregistering this dll: \n\"" + nl.Message + "\"");
			}
			catch (System.Exception e)
			{
				Console.WriteLine("There was a problem unregistering this dll: " + e.Message);
				System.Windows.Forms.MessageBox.Show("There was a problem unregistering this dll: \n\"" + e.Message + "\"");
			}
		}

		#endregion

		#region ISwAddin Implementation
		public SwAddin()
		{
			Instance = this;
		}

		public bool ConnectToSW(object ThisSW, int cookie)
		{

			iSwApp = (ISldWorks)ThisSW;
			addinID = cookie;

			//Setup callbacks
			var bRet = iSwApp.SetAddinCallbackInfo(0, this, addinID);
			bRet = iSwApp.AddFileSaveAsItem2(addinID, "Datasmith_FileSave", "Unreal Datamisth (*.udatasmith)", "udatasmith", (int)swDocumentTypes_e.swDocASSEMBLY);
			bRet = iSwApp.AddFileSaveAsItem2(addinID, "Datasmith_FileSave", "Unreal Datamisth (*.udatasmith)", "udatasmith", (int)swDocumentTypes_e.swDocPART);

			#region Setup the Command Manager
			iCmdMgr = iSwApp.GetCommandManager(cookie);
			AddCommandMgr();
			#endregion

			#region Setup the Event Handlers
			SwEventPtr = (SolidWorks.Interop.sldworks.SldWorks)iSwApp;
			openDocs = new Hashtable();
			AttachEventHandlers();
			#endregion

			#region Setup Sample Property Manager
			AddPMP();
			#endregion

			SwSingleton.ProgressEvent += OnProgressMessage;

			// Init DirectLink
			bool bDirectLinkInitOk = FDatasmithFacadeDirectLink.Init();
			Debug.Assert(bDirectLinkInitOk);

			return true;
		}

		private void OnProgressMessage(object sender, SwSingleton.ProgressArgs args)
		{
			lock (ProgressLock)
			{
				if (args.Message == null)
				{
					if (ProgressBar != null)
					{
						//ProgressBar.End(); // stack overflow???
						ProgressBar = null;
					}
				}
				else
				{
					if (ProgressBar == null)
					{
						iSwApp.GetUserProgressBar(out ProgressBar);
						ProgressBar.Start(0, 100, "");
					}
					ProgressBar.UpdateTitle(args.Message);
				}
			}
		}

		public bool DisconnectFromSW()
		{
			if (SwSingleton.CurrentScene != null)
			{
				SwSingleton.CurrentScene.Processor.Stop();
			}

			FDatasmithFacadeDirectLink.Shutdown();

			bool bRet = iSwApp.RemoveFileSaveAsItem2(addinID, "Datasmith_FileSave", "Unreal (*.udatasmith)", "udatasmith", (int)swDocumentTypes_e.swDocASSEMBLY);
			bRet = iSwApp.RemoveFileSaveAsItem2(addinID, "Datasmith_FileSave", "Unreal (*.udatasmith)", "udatasmith", (int)swDocumentTypes_e.swDocPART);

			RemoveCommandMgr();
			RemovePMP();
			DetachEventHandlers();

			System.Runtime.InteropServices.Marshal.ReleaseComObject(iCmdMgr);
			iCmdMgr = null;
			System.Runtime.InteropServices.Marshal.ReleaseComObject(iSwApp);
			iSwApp = null;
			//The addin _must_ call GC.Collect() here in order to retrieve all managed code pointers
			GC.Collect();
			GC.WaitForPendingFinalizers();

			GC.Collect();
			GC.WaitForPendingFinalizers();

			return true;
		}
		#endregion

		public string ParseFilename(string sFileName)
		{
			string strExtension = null;
			int lPos = 0;
			int lNumExtensionOccurrences = 0;
			int lStart = 0;
			string strSearchString = null;
			int lNumNonRealExtensions = 0;
			string strExtensionWithPeriod = null;

			// Set the extension for which to look
			strExtension = "udatasmith";
			strExtensionWithPeriod = "." + strExtension;

			// Strip the trailing 'w' or 'r' and any leading and trailing white space
			sFileName = (sFileName.Substring(0, sFileName.Length - 1)).Trim(' ');

			// Strip extension from the back
			sFileName = (sFileName.Substring(0, sFileName.Length - strExtension.Length)).Trim(' ');

			// Change to lowercase to make search case-insensitive
			strSearchString = sFileName.ToLower();

			lNumExtensionOccurrences = 0;

			lStart = 1;

			do
			{
				lPos = strSearchString.IndexOf(strExtensionWithPeriod.ToLower(), lStart - 1) + 1;
				if (lPos > 0)
				{
					lNumExtensionOccurrences = lNumExtensionOccurrences + 1;

					// Move start point of search
					lStart = (int)(lPos + strExtensionWithPeriod.Length);

				}

			} while (lPos > 0);

			// There is 1 real extension and n*2 non-real extension
			lNumNonRealExtensions = (lNumExtensionOccurrences / 2);

			// Start searching from the end to locate the real extension
			// Skip the number of non-real extensions, before reaching the real extension

			// Change to lowercase to make search case-insensitive
			strSearchString = sFileName.ToLower();

			lPos = strSearchString.LastIndexOf(strExtensionWithPeriod.ToLower(), lStart);

			sFileName = sFileName.Substring(0, lPos) + strExtensionWithPeriod;

			return sFileName;
		}

		public void Datasmith_FileSave(string sFileName)
		{
			// S_OK          =   Saved successfully
			// S_FALSE       =   Unsuccessful
			// !(SUCCEEDED)  =   Insuccessful

			foreach (var pp in SwSingleton.CurrentScene.Parts)
			{
				pp.Value.Load(false);
			}

			sFileName = ParseFilename(sFileName);

			SwSingleton.CurrentScene.EvaluateScene();

			ExportCommand cmd = new ExportCommand();
			cmd.Path = Path.GetDirectoryName(sFileName);
			cmd.SceneName = Path.GetFileNameWithoutExtension(sFileName);
			SwSingleton.CurrentScene.Processor.AddCommand(cmd);
		}

		#region UI Methods
		public void AddCommandMgr()
		{
			ICommandGroup cmdGroup;
			if (iBmp == null)
				iBmp = new BitmapHandler();
			Assembly thisAssembly;
			int cmdIndex0, cmdIndex1;
			string Title = "Datasmith", ToolTip = "Unreal Datasmith Exporter for Solidworks";

			int[] docTypes = new int[]{(int)swDocumentTypes_e.swDocASSEMBLY,
									   (int)swDocumentTypes_e.swDocDRAWING,
									   (int)swDocumentTypes_e.swDocPART};

			thisAssembly = System.Reflection.Assembly.GetAssembly(this.GetType());


			int cmdGroupErr = 0;
			bool ignorePrevious = false;

			object registryIDs;
			//get the ID information stored in the registry
			bool getDataResult = iCmdMgr.GetGroupDataFromRegistry(mainCmdGroupID, out registryIDs);

			int[] knownIDs = new int[2] { mainItemID1, mainItemID2 };

			if (getDataResult)
			{
				if (!CompareIDs((int[])registryIDs, knownIDs)) //if the IDs don't match, reset the commandGroup
				{
					ignorePrevious = true;
				}
			}

			cmdGroup = iCmdMgr.CreateCommandGroup2(mainCmdGroupID, Title, ToolTip, "", -1, ignorePrevious, ref cmdGroupErr);
			cmdGroup.LargeIconList = iBmp.CreateFileFromResourceBitmap("SolidworksDatasmith.ToolbarLarge.bmp", thisAssembly);
			cmdGroup.SmallIconList = iBmp.CreateFileFromResourceBitmap("SolidworksDatasmith.ToolbarSmall.bmp", thisAssembly);
			cmdGroup.LargeMainIcon = iBmp.CreateFileFromResourceBitmap("SolidworksDatasmith.MainIconLarge.bmp", thisAssembly);
			cmdGroup.SmallMainIcon = iBmp.CreateFileFromResourceBitmap("SolidworksDatasmith.MainIconSmall.bmp", thisAssembly);

			int menuToolbarOption = (int)(swCommandItemType_e.swMenuItem | swCommandItemType_e.swToolbarItem);
			cmdIndex0 = cmdGroup.AddCommandItem2("Direct Link Synchronize", -1, "Update the Direct Link connection", "Synchronize", 1, "DirectLinkUpdate", "DirectLinkUpdateStatus", mainItemID1, menuToolbarOption);
			cmdIndex1 = cmdGroup.AddCommandItem2("Pause/Resume Direct Link", 0, "Pause/Resume DirectLink Auto Sync", "Pause/Resume Direct Link", 0, "EnableDisableLink", "EnableDisableLinkStatus", mainItemID1, menuToolbarOption);
			//cmdIndex1 = cmdGroup.AddCommandItem2("Show PMP", -1, "Display sample property manager", "Show PMP", 2, "ShowPMP", "EnablePMP", mainItemID2, menuToolbarOption);

			cmdGroup.HasToolbar = true;
			//cmdGroup.HasMenu = true;
			cmdGroup.Activate();

			bool bResult;

			//FlyoutGroup flyGroup = iCmdMgr.CreateFlyoutGroup(flyoutGroupID, "Dynamic Flyout", "Flyout Tooltip", "Flyout Hint",
			//  cmdGroup.SmallMainIcon, cmdGroup.LargeMainIcon, cmdGroup.SmallIconList, cmdGroup.LargeIconList, "FlyoutCallback", "FlyoutEnable");


			//flyGroup.AddCommandItem("FlyoutCommand 1", "test", 0, "FlyoutCommandItem1", "FlyoutEnableCommandItem1");

			//flyGroup.FlyoutType = (int)swCommandFlyoutStyle_e.swCommandFlyoutStyle_Simple;


			foreach (int type in docTypes)
			{
				CommandTab cmdTab;

				cmdTab = iCmdMgr.GetCommandTab(type, Title);

				if (cmdTab != null & !getDataResult | ignorePrevious)//if tab exists, but we have ignored the registry info (or changed command group ID), re-create the tab.  Otherwise the ids won't matchup and the tab will be blank
				{
					bool res = iCmdMgr.RemoveCommandTab(cmdTab);
					cmdTab = null;
				}

				//if cmdTab is null, must be first load (possibly after reset), add the commands to the tabs
				if (cmdTab == null)
				{
					cmdTab = iCmdMgr.AddCommandTab(type, Title);

					CommandTabBox cmdBox = cmdTab.AddCommandTabBox();

					int[] cmdIDs = new int[3];
					int[] TextType = new int[3];

					cmdIDs[0] = cmdGroup.get_CommandID(cmdIndex0);
					cmdIDs[1] = cmdGroup.get_CommandID(cmdIndex1);

					TextType[0] = (int)swCommandTabButtonTextDisplay_e.swCommandTabButton_TextBelow;
					TextType[1] = (int)swCommandTabButtonTextDisplay_e.swCommandTabButton_TextBelow;

					//cmdIDs[2] = cmdGroup.ToolbarId;
					//TextType[2] = (int)swCommandTabButtonTextDisplay_e.swCommandTabButton_TextHorizontal | (int)swCommandTabButtonFlyoutStyle_e.swCommandTabButton_ActionFlyout;

					bResult = cmdBox.AddCommands(cmdIDs, TextType);


					/*
					CommandTabBox cmdBox1 = cmdTab.AddCommandTabBox();
					cmdIDs = new int[1];
					TextType = new int[1];

					cmdIDs[0] = flyGroup.CmdID;
					TextType[0] = (int)swCommandTabButtonTextDisplay_e.swCommandTabButton_TextBelow | (int)swCommandTabButtonFlyoutStyle_e.swCommandTabButton_ActionFlyout;

					bResult = cmdBox1.AddCommands(cmdIDs, TextType);

					cmdTab.AddSeparator(cmdBox1, cmdIDs[0]);
					*/

				}

			}
			thisAssembly = null;

		}

		public void RemoveCommandMgr()
		{
			iBmp.Dispose();

			iCmdMgr.RemoveCommandGroup(mainCmdGroupID);
			iCmdMgr.RemoveFlyoutGroup(flyoutGroupID);
		}

		public bool CompareIDs(int[] storedIDs, int[] addinIDs)
		{
			List<int> storedList = new List<int>(storedIDs);
			List<int> addinList = new List<int>(addinIDs);

			addinList.Sort();
			storedList.Sort();

			if (addinList.Count != storedList.Count)
			{
				return false;
			}
			else
			{

				for (int i = 0; i < addinList.Count; i++)
				{
					if (addinList[i] != storedList[i])
					{
						return false;
					}
				}
			}
			return true;
		}

		public Boolean AddPMP()
		{
			ppage = new UserPMPage(this);
			return true;
		}

		public Boolean RemovePMP()
		{
			ppage = null;
			return true;
		}

		#endregion

		#region UI Callbacks
		public void DirectLinkUpdate()
		{
			SwSingleton.CurrentScene.DirectLinkUpdate();
		}

		public int DirectLinkUpdateStatus()
		{
			if (SwSingleton.CurrentScene == null)
			{
				return 0;
			}

			return SwSingleton.CurrentScene.bDirectLinkAutoSync ? 0 : 1;
		}

		public void EnableDisableLink()
		{
			SwSingleton.CurrentScene.bDirectLinkAutoSync = !SwSingleton.CurrentScene.bDirectLinkAutoSync;

			if (SwSingleton.CurrentScene.bDirectLinkAutoSync)
			{
				// Run update on auto sync enable, in case we got changes
				SwSingleton.CurrentScene.DirectLinkUpdate();
			}
		}

		public int EnableDisableLinkStatus()
		{
			// 0 Deselects and disables the item
			// 1 Deselects and enables the item; this is the default state if no update function is specified
			// 2 Selects and disables the item
			// 3 Selects and enables the item
			if (SwSingleton.CurrentScene == null)
			{
				return 0;
			}

			return SwSingleton.CurrentScene.bDirectLinkAutoSync ? 1 : 3;
		}

		public void ShowPMP()
		{
			if (ppage != null)
				ppage.Show();
		}

		public int EnablePMP()
		{
			if (iSwApp.ActiveDoc != null)
				return 1;
			else
				return 0;
		}

		public void FlyoutCallback()
		{
			FlyoutGroup flyGroup = iCmdMgr.GetFlyoutGroup(flyoutGroupID);
			flyGroup.RemoveAllCommandItems();

			flyGroup.AddCommandItem(System.DateTime.Now.ToLongTimeString(), "test", 0, "FlyoutCommandItem1", "FlyoutEnableCommandItem1");

		}
		public int FlyoutEnable()
		{
			return 1;
		}

		public void FlyoutCommandItem1()
		{
			iSwApp.SendMsgToUser("Flyout command 1");
		}

		public int FlyoutEnableCommandItem1()
		{
			return 1;
		}
		#endregion

		#region Event Methods
		public bool AttachEventHandlers()
		{
			AttachSwEvents();
			//Listen for events on all currently open docs
			AttachEventsToAllDocuments();
			return true;
		}

		private bool AttachSwEvents()
		{
			try
			{
				SwEventPtr.ActiveDocChangeNotify += new DSldWorksEvents_ActiveDocChangeNotifyEventHandler(OnDocChange);
				SwEventPtr.DocumentLoadNotify2 += new DSldWorksEvents_DocumentLoadNotify2EventHandler(OnDocLoad);
				SwEventPtr.FileNewNotify2 += new DSldWorksEvents_FileNewNotify2EventHandler(OnFileNew);
				SwEventPtr.ActiveModelDocChangeNotify += new DSldWorksEvents_ActiveModelDocChangeNotifyEventHandler(OnModelChange);
				SwEventPtr.FileOpenPostNotify += new DSldWorksEvents_FileOpenPostNotifyEventHandler(FileOpenPostNotify);
				SwEventPtr.FileCloseNotify += new DSldWorksEvents_FileCloseNotifyEventHandler(FileCloseNotify);
				SwEventPtr.CommandCloseNotify += new DSldWorksEvents_CommandCloseNotifyEventHandler(CommandCloseNotify);
				SwEventPtr.OnIdleNotify += new DSldWorksEvents_OnIdleNotifyEventHandler(OnIdle);
				return true;
			}
			catch (Exception e)
			{
				Console.WriteLine(e.Message);
				return false;
			}
		}



		private bool DetachSwEvents()
		{
			try
			{
				SwEventPtr.ActiveDocChangeNotify -= new DSldWorksEvents_ActiveDocChangeNotifyEventHandler(OnDocChange);
				SwEventPtr.DocumentLoadNotify2 -= new DSldWorksEvents_DocumentLoadNotify2EventHandler(OnDocLoad);
				SwEventPtr.FileNewNotify2 -= new DSldWorksEvents_FileNewNotify2EventHandler(OnFileNew);
				SwEventPtr.ActiveModelDocChangeNotify -= new DSldWorksEvents_ActiveModelDocChangeNotifyEventHandler(OnModelChange);
				SwEventPtr.FileOpenPostNotify -= new DSldWorksEvents_FileOpenPostNotifyEventHandler(FileOpenPostNotify);
				SwEventPtr.FileCloseNotify -= new DSldWorksEvents_FileCloseNotifyEventHandler(FileCloseNotify);
				SwEventPtr.CommandCloseNotify -= new DSldWorksEvents_CommandCloseNotifyEventHandler(CommandCloseNotify);
				SwEventPtr.OnIdleNotify -= new DSldWorksEvents_OnIdleNotifyEventHandler(OnIdle);
				return true;
			}
			catch (Exception e)
			{
				Console.WriteLine(e.Message);
				return false;
			}

		}

		public void AttachEventsToAllDocuments()
		{
			ModelDoc2 modDoc = (ModelDoc2)iSwApp.GetFirstDocument();
			while (modDoc != null)
			{
				if (!openDocs.Contains(modDoc))
				{
					AttachModelDocEventHandler(modDoc);
				}
				modDoc = (ModelDoc2)modDoc.GetNext();
			}
		}

		public bool AttachModelDocEventHandler(ModelDoc2 modDoc)
		{
			if (modDoc == null)
				return false;

			DocumentEventHandler docHandler = null;

			if (!openDocs.Contains(modDoc))
			{
				switch (modDoc.GetType())
				{
					case (int)swDocumentTypes_e.swDocPART:
						{
							docHandler = new PartEventHandler(modDoc, this);
							break;
						}
					case (int)swDocumentTypes_e.swDocASSEMBLY:
						{
							docHandler = new AssemblyEventHandler(modDoc, this);
							break;
						}
					case (int)swDocumentTypes_e.swDocDRAWING:
						{
							docHandler = new DrawingEventHandler(modDoc, this);
							break;
						}
					default:
						{
							return false; //Unsupported document type
						}
				}
				docHandler.AttachEventHandlers();
				openDocs.Add(modDoc, docHandler);
			}
			return true;
		}

		public bool DetachModelEventHandler(ModelDoc2 modDoc)
		{
			DocumentEventHandler docHandler;
			docHandler = (DocumentEventHandler)openDocs[modDoc];
			openDocs.Remove(modDoc);
			modDoc = null;
			docHandler = null;
			return true;
		}

		public bool DetachEventHandlers()
		{
			DetachSwEvents();

			//Close events on all currently open docs
			DocumentEventHandler docHandler;
			int numKeys = openDocs.Count;
			object[] keys = new Object[numKeys];

			//Remove all document event handlers
			openDocs.Keys.CopyTo(keys, 0);
			foreach (ModelDoc2 key in keys)
			{
				docHandler = (DocumentEventHandler)openDocs[key];
				docHandler.DetachEventHandlers(); //This also removes the pair from the hash
				docHandler = null;
			}
			return true;
		}
		#endregion

		#region Event Handlers
		//Events
		public int OnDocChange()
		{
			var current = iSwApp.ActiveDoc as ModelDoc2;
			SwSingleton.SwitchToScene(current);
			return 0;
		}

		public int FileCloseNotify(string FileName, int reason)
		{
			return 0;
		}

		public int CommandCloseNotify(int Command, int reason)
		{
			if (Command == 2789) // unable to find swCommands_e.swCommands_FileClose enum value in interop enums
			{
				int opendocumentscount = iSwApp.GetDocumentCount();
				List<string> opendocumentpaths = new List<string>();
				object[] models = (object[])iSwApp.GetDocuments();
				for (int index = 0; index < opendocumentscount; index++)
				{
					ModelDoc2 swModel = models[index] as ModelDoc2;
					if (swModel != null)
					{
						opendocumentpaths.Add(swModel.GetPathName());
					}
				}
				SwSingleton.SynchScenesOpened(opendocumentpaths);
			}
			return 0;
		}

		public int OnDocLoad(string docTitle, string docPath)
		{
			AttachEventsToAllDocuments();
			return 0;
		}

		int FileOpenPostNotify(string FileName)
		{
			AttachEventsToAllDocuments();
			return 0;
		}

		public int OnIdle()
		{
			if (SwSingleton.CurrentScene != null)
			{
				if (SwSingleton.CurrentScene.bIsDirty && SwSingleton.CurrentScene.bDirectLinkAutoSync)
				{
					SwSingleton.CurrentScene.DirectLinkUpdate();
				}
			}
			return 0;
		}

		public int OnFileNew(object newDoc, int docType, string templateName)
		{
			AttachEventsToAllDocuments();
			return 0;
		}

		public int OnModelChange()
		{
			return 0;
		}

		#endregion
	}
}
