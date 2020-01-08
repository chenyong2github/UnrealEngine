// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Drawing;
using System.Linq;
using System.Windows.Forms;

namespace DatasmithRevitExporter
{
	public class DatasmithRevitExportOptions : Form
	{
		private CheckBox		WriteLogFile;
		private NumericUpDown	LevelOfTessellation;
		private CheckedListBox	ViewsList;

		public List<Autodesk.Revit.DB.View3D> Selected3DViews { get; private set; } = new List<Autodesk.Revit.DB.View3D>();
		private List<Autodesk.Revit.DB.View3D> All3DViews;

		public DatasmithRevitExportOptions(Autodesk.Revit.DB.Document document)
		{
			ToolTip OptionToolTip = new ToolTip();
			OptionToolTip.AutoPopDelay = 10000; // milliseconds

			Label WriteLogFileLabel = new Label();
			WriteLogFileLabel.Name = "WriteLogFileLabel";
			WriteLogFileLabel.Text = "Write Log File";
			WriteLogFileLabel.Anchor = AnchorStyles.Left;
			WriteLogFileLabel.AutoSize = true;
			WriteLogFileLabel.Location = new Point(3, 3);
			WriteLogFileLabel.Size = new Size(72, 13);
			WriteLogFileLabel.TabIndex = 0;
			WriteLogFileLabel.TextAlign = ContentAlignment.MiddleLeft;

			WriteLogFile = new CheckBox();
			WriteLogFile.Name = "WriteLogFile";
			WriteLogFile.Anchor = AnchorStyles.Left;
			WriteLogFile.AutoSize = true;
			WriteLogFile.Location = new Point(113, 3);
			WriteLogFile.Size = new Size(15, 14);
			WriteLogFile.TabIndex = 1;
			WriteLogFile.UseVisualStyleBackColor = true;

			OptionToolTip.SetToolTip(WriteLogFile,
									 "Write a '.log' file aside the '.udatasmith' file.\n" +
									 "This log file records some details about the exported Revit elements.\n" +
									 "The log file can become quite big for complex 3D views.");

			Label LevelOfTessellationLabel = new Label();
			LevelOfTessellationLabel.Name = "LevelOfTessellationLabel";
			LevelOfTessellationLabel.Text = "Level of Tessellation";
			LevelOfTessellationLabel.Anchor = AnchorStyles.Left;
			LevelOfTessellationLabel.AutoSize = true;
			LevelOfTessellationLabel.Location = new Point(3, 26);
			LevelOfTessellationLabel.Size = new Size(104, 13);
			LevelOfTessellationLabel.TabIndex = 2;
			LevelOfTessellationLabel.TextAlign = ContentAlignment.MiddleLeft;

			LevelOfTessellation = new NumericUpDown();
			LevelOfTessellation.Name = "LevelOfTessellation";
			LevelOfTessellation.Minimum = -1;
			LevelOfTessellation.Maximum = 15;
			LevelOfTessellation.Value = 8;
			LevelOfTessellation.Anchor = AnchorStyles.Left;
			LevelOfTessellation.AutoSize = true;
			LevelOfTessellation.Location = new Point(113, 23);
			LevelOfTessellation.Size = new Size(35, 20);
			LevelOfTessellation.TabIndex = 3;
			LevelOfTessellation.TextAlign = HorizontalAlignment.Right;

			OptionToolTip.SetToolTip(LevelOfTessellation,
									 "-1 :\tRevit will use its default algorithm, which is based on output resolution.\n" +
									 "0 to 15 :\tRevit will use the suggested level of detail when tessellating faces.\n" +
									 "\tUsing a value close to the middle of the range yields a very reasonable tessellation.\n" +
									 "\tRevit uses level 8 as its 'normal' level of detail.");

			Button OKButton = new Button();
			OKButton.Name = "OKButton";
			OKButton.Text = "OK";
			OKButton.Anchor = ((AnchorStyles)((AnchorStyles.Bottom | AnchorStyles.Right)));
			OKButton.DialogResult = DialogResult.OK;
			OKButton.Location = new Point(211, 68);
			OKButton.Margin = new Padding(78, 13, 3, 3);
			OKButton.Size = new Size(75, 23);
			OKButton.TabIndex = 4;
			OKButton.UseVisualStyleBackColor = true;
			OKButton.Click += (s, e) => 
			{
				for (int ItemIndex = 0; ItemIndex < ViewsList.Items.Count; ++ItemIndex)
				{
					if (ViewsList.GetItemChecked(ItemIndex))
					{
						Selected3DViews.Add(All3DViews[ItemIndex]);
					}
				}
				if (Selected3DViews.Count == 0)
				{
					string message = "No views selected for export! Please select at least one 3D view.";
					MessageBox.Show(message, "View selection", MessageBoxButtons.OK, MessageBoxIcon.Warning);
					DialogResult = DialogResult.None;
				}
			};

			ViewsList = new CheckedListBox();
			ViewsList.Size = new Size(300, 100);

			Label ViewsLabel = new Label();
			ViewsLabel.Name = "ViewsLabel";
			ViewsLabel.Text = "Select 3D Views";
			ViewsLabel.Anchor = AnchorStyles.Left;
			ViewsLabel.AutoSize = true;

			TableLayoutPanel DialogLayout = new TableLayoutPanel();
			DialogLayout.Name = "DialogLayout";
			DialogLayout.ColumnCount = 2;
			DialogLayout.ColumnStyles.Add(new ColumnStyle());
			DialogLayout.ColumnStyles.Add(new ColumnStyle());
			DialogLayout.RowCount = 5;
			DialogLayout.RowStyles.Add(new RowStyle());
			DialogLayout.RowStyles.Add(new RowStyle(SizeType.Percent, 100F));
			DialogLayout.RowStyles.Add(new RowStyle());
			DialogLayout.RowStyles.Add(new RowStyle());
			DialogLayout.RowStyles.Add(new RowStyle());
			DialogLayout.AutoSize = true;
			DialogLayout.AutoSizeMode = AutoSizeMode.GrowAndShrink;
			DialogLayout.Dock = DockStyle.Fill;
			DialogLayout.Location = new Point(10, 10);
			DialogLayout.Size = new Size(300, 100);
			DialogLayout.TabIndex = 0;

			DialogLayout.Controls.Add(ViewsLabel,				0, 0);
			DialogLayout.Controls.Add(ViewsList,				0, 1);
			DialogLayout.Controls.Add(WriteLogFileLabel,		0, 2);
			DialogLayout.Controls.Add(WriteLogFile,				1, 2);
			DialogLayout.Controls.Add(LevelOfTessellationLabel, 0, 3);
			DialogLayout.Controls.Add(LevelOfTessellation,		1, 3);
			DialogLayout.Controls.Add(OKButton,					1, 4);

			DialogLayout.SetColumnSpan(ViewsList, 2);

			Name = "UnrealDatasmithExportOptions";
			Text = "Unreal Datasmith Export - Debug Options";
			AcceptButton = OKButton;
			AutoScaleDimensions = new SizeF(6F, 13F);
			AutoScaleMode = AutoScaleMode.Font;
			AutoSize = true;
			AutoSizeMode = AutoSizeMode.GrowAndShrink;
			ClientSize = new Size(309, 114);
			FormBorderStyle = FormBorderStyle.FixedDialog;
			MaximizeBox = false;
			MinimizeBox = false;
			Padding = new Padding(10);
			SizeGripStyle = SizeGripStyle.Hide;
			StartPosition = FormStartPosition.CenterParent;

			Controls.Add(DialogLayout);

			// Get all 3D views that can be exported.
			All3DViews = new Autodesk.Revit.DB.FilteredElementCollector(document).OfClass(typeof(Autodesk.Revit.DB.View3D)).Cast<Autodesk.Revit.DB.View3D>().ToList();
			All3DViews.RemoveAll(view => (view.IsTemplate || !view.CanBePrinted));

			foreach (var View in All3DViews)
			{
				ViewsList.Items.Add(View.Name, document.ActiveView.Id == View.Id);
			}
		}

		public bool GetWriteLogFile()
		{
			return WriteLogFile.Checked;
		}

		public int GetLevelOfTessellation()
		{
			return Decimal.ToInt32(LevelOfTessellation.Value);
		}
	}
}
