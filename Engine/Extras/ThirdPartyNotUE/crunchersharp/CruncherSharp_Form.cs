using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Windows.Forms;

namespace CruncherSharp
{
    public partial class CruncherSharp_Form : Form
    {
		public DataTable m_table = null;

		/// <summary>
		/// Result of attempting to load the PDB from the file dialog. True if success
		/// </summary>
		private bool m_LoadResult = false;

		private string preExistingFilterCache;

		private string m_CurrentPDBFilePath = null;

		public CruncherSharp_Form()
        {
            InitializeComponent();
			BindControlMouseClicks(this);
			m_table = Utils.CreateDataTable();

			m_CruncherData = new CruncherData(m_table);

            bindingSourceSymbols.DataSource = m_table;
            dataGridSymbols.DataSource = bindingSourceSymbols;

            dataGridSymbols.Columns[0].Width = 271;
        }

        private void exitToolStripMenuItem_Click(object sender, EventArgs e)
        {
            this.Close();
        }

        private void loadPDBToolStripMenuItem_Click(object sender, EventArgs e)
        {
            if (openPdbDialog.ShowDialog() == DialogResult.OK)
            {
				if (loadingBackgroundWorker.IsBusy)
				{
					MessageBox.Show(this, "Already loading a file! Please wait for it to finish.");
					return;
				}

				ResetLoadingBar();

				m_CurrentPDBFilePath = openPdbDialog.FileName;
				
				loadingBackgroundWorker.RunWorkerAsync();
			}
		}

		private void ResetLoadingBar()
		{
			// Reset the loading bar
			loadingProgressBar.Maximum = 100;
			loadingProgressBar.Step = 1;
			loadingProgressBar.Value = 0;
		}

		private void loadingBackgroundWorker_DoWork(object sender, DoWorkEventArgs e)
		{
			BackgroundWorker backgroundWorker = sender as BackgroundWorker;
			// Temporarily clear the filter so, if current filter is invalid, we don't generate a ton of exceptions while populating the table
			preExistingFilterCache = textBoxFilter.Text;
			textBoxFilter.Text = "";
			bUpdateStack = false;

			Cursor.Current = Cursors.WaitCursor;
			string LoadRes = m_CruncherData.LoadDataFromPdb(m_CurrentPDBFilePath, backgroundWorker);
			m_LoadResult = (LoadRes == null);
		}

		private void loadingBackgroundWorker_ProgressChanged(object sender, ProgressChangedEventArgs e)
		{
			loadingProgressBar.Value = e.ProgressPercentage;
		}

		private void loadingBackgroundWorker_RunWorkerCompleted(object sender, RunWorkerCompletedEventArgs e)
		{
			//m_CruncherData.PopulateDataTable(m_table);
			if (!m_LoadResult)
			{
				MessageBox.Show(this, "Something went wrong loading a PDB, see log.");
			}

			this.Text = "Cruncher # - " + System.IO.Path.GetFileName(openPdbDialog.FileName);

			// Sort by name by default (ascending)
			dataGridSymbols.Sort(dataGridSymbols.Columns[0], ListSortDirection.Ascending);
			bindingSourceSymbols.Filter = null;// "Symbol LIKE '*rde*'";
			Cursor.Current = Cursors.Default;

			// Restore the filter now that the table is populated
			bUpdateStack = true;
			textBoxFilter.Text = preExistingFilterCache;

			ShowSelectedSymbolInfo();
		}

		private ulong GetCacheLineSize()
        {
            return Convert.ToUInt32(textBoxCache.Text);
        }

		CruncherSymbol FindSelectedSymbolInfo()
        {
            if (dataGridSymbols.SelectedRows.Count == 0)
            {
                return null;
            }

            DataGridViewRow selectedRow = dataGridSymbols.SelectedRows[0];
            string symbolName = selectedRow.Cells[0].Value.ToString();

			CruncherSymbol info = m_CruncherData.FindSymbolInfo(symbolName);
            return info;
        }

		CruncherData m_CruncherData;
        long m_prefetchStartOffset = 0;
		Stack<string> UndoStack = new Stack<string>();
		Stack<string> RedoStack = new Stack<string>();
		bool bUpdateStack = true;

        private void dataGridSymbols_SelectionChanged(object sender, EventArgs e)
        {
            m_prefetchStartOffset = 0;
            ShowSelectedSymbolInfo();
        }

        void ShowSelectedSymbolInfo()
        {
            dataGridViewSymbolInfo.Rows.Clear();
			CruncherSymbol info = FindSelectedSymbolInfo();
            if (info != null)
            {
                ShowSymbolInfo(info);
            }
        }

        delegate void InsertCachelineBoundaryRowsDelegate(long nextOffset);

		/** Shows the given symbol info on the tables on the right hand side */
        void ShowSymbolInfo(CruncherSymbol info)
        {
            dataGridViewSymbolInfo.Rows.Clear();
            if (!info.HasChildren)
            {
                return;
            }

			if (bUpdateStack && (UndoStack.Count == 0 || info.Name != UndoStack.Peek()))
			{
				RedoStack.Clear();
				UndoStack.Push(info.Name);
			}

            long cacheLineSize = (long)GetCacheLineSize();
            long prevCacheBoundaryOffset = m_prefetchStartOffset;

            if (prevCacheBoundaryOffset > (long)info.Size)
            {
                prevCacheBoundaryOffset = (long)info.Size;
            }

            long numCacheLines = 0;

            InsertCachelineBoundaryRowsDelegate InsertCachelineBoundaryRows = (nextOffset) =>
            {
                while (nextOffset - prevCacheBoundaryOffset >= cacheLineSize)
                {
                    numCacheLines = numCacheLines + 1;
                    long cacheLineOffset = numCacheLines * cacheLineSize + m_prefetchStartOffset;
                    string[] boundaryRow = { "Cacheline boundary", cacheLineOffset.ToString(), "" };
                    dataGridViewSymbolInfo.Rows.Add(boundaryRow);
                    prevCacheBoundaryOffset = cacheLineOffset;
                }
            };

            foreach (CruncherSymbol child in info.m_children)
            {
                if (child.Padding > 0)
                {
                    long paddingOffset = child.Offset - child.Padding;

                    InsertCachelineBoundaryRows(paddingOffset);

                    string[] paddingRow = { "*Padding*", paddingOffset.ToString(), child.Padding.ToString() };
                    dataGridViewSymbolInfo.Rows.Add(paddingRow);
                }

                InsertCachelineBoundaryRows(child.Offset);

                string[] row = { child.Name, child.Offset.ToString(), child.Size.ToString() };
                dataGridViewSymbolInfo.Rows.Add(row);
                dataGridViewSymbolInfo.Rows[dataGridViewSymbolInfo.Rows.Count - 1].Tag = child;
                if (child.TypeName != null && child.TypeName.Length != 0)
                {
                    dataGridViewSymbolInfo.Rows[dataGridViewSymbolInfo.Rows.Count - 1].Cells[0].ToolTipText = child.TypeName;
                }
            }
            // Final structure padding.
            if (info.Padding > 0)
            {
                long paddingOffset = (long)info.Size - info.Padding;
                string[] paddingRow = { "*Padding*", paddingOffset.ToString(), info.Padding.ToString() };
                dataGridViewSymbolInfo.Rows.Add(paddingRow);
            }
        }

        private void dataGridSymbols_SortCompare(object sender, DataGridViewSortCompareEventArgs e)
        {
            if (e.Column.Index > 0)
            {
                e.Handled = true;
                int val1;
                int val2;
                Int32.TryParse(e.CellValue1.ToString(), out val1);
                Int32.TryParse(e.CellValue2.ToString(), out val2);
                e.SortResult = val1 - val2;
            }
        }

        private void dataGridViewSymbolInfo_CellPainting(object sender, DataGridViewCellPaintingEventArgs e)
        {
            for (int i = 0; i < dataGridViewSymbolInfo.Rows.Count; ++i)
            {
                DataGridViewRow row = dataGridViewSymbolInfo.Rows[i];
                DataGridViewCell cell = row.Cells[0];
                string CellValue = cell.Value.ToString();

                if (CellValue == "*Padding*")
                {
                    cell.Style.BackColor = Color.LightPink;
					cell.Style.Alignment = DataGridViewContentAlignment.MiddleCenter;
                    row.Cells[1].Style.BackColor = Color.LightPink;
                    row.Cells[2].Style.BackColor = Color.LightPink;
                }
                else if (CellValue.IndexOf("Base: ") == 0)
                {
                    cell.Style.BackColor = Color.LightGreen;
                    row.Cells[1].Style.BackColor = Color.LightGreen;
                    row.Cells[2].Style.BackColor = Color.LightGreen;
                }
                else if (CellValue == "Cacheline boundary")
                {
                    cell.Style.BackColor = Color.LightGray;
                    row.Cells[1].Style.BackColor = Color.LightGray;
                    row.Cells[2].Style.BackColor = Color.LightGray;
                }
                else if (i + 1 < dataGridViewSymbolInfo.Rows.Count)
                {
                    if (dataGridViewSymbolInfo.Rows[i+1].Cells[0].Value.ToString() == "Cacheline boundary")
                    {
                        UInt64 CachelineBoundary = UInt64.Parse(dataGridViewSymbolInfo.Rows[i + 1].Cells[1].Value.ToString());
                        UInt64 MemberStart = UInt64.Parse(row.Cells[1].Value.ToString());
                        UInt64 MemberSize = UInt64.Parse(row.Cells[2].Value.ToString());
                        if (MemberStart + MemberSize > CachelineBoundary)
                        {
                            cell.Style.BackColor = Color.LightYellow;
                            row.Cells[1].Style.BackColor = Color.LightYellow;
                            row.Cells[2].Style.BackColor = Color.LightYellow;
                        }
                    }
                }
            }
        }

        private void textBoxFilter_TextChanged(object sender, EventArgs e)
        {
			try
			{
                if (textBoxFilter.Text.Length == 0)
                {
                    bindingSourceSymbols.Filter = null;
                }
                else
                {
                    bindingSourceSymbols.Filter = "Symbol LIKE '" + textBoxFilter.Text + "'";
                }

				textBoxFilter.BackColor = Color.Empty;
				textBoxFilter.ForeColor = Color.Empty;
				filterFeedbackLabel.Text = "";
			}
			catch (System.Data.EvaluateException)
			{
				textBoxFilter.BackColor = Color.Red;
				textBoxFilter.ForeColor = Color.White;
				filterFeedbackLabel.Text = "Invalid filter";
			}
		}

        private void copyTypeLayoutToClipboardToolStripMenuItem_Click(object sender, EventArgs e)
        {
			CruncherSymbol info = FindSelectedSymbolInfo();
            if (info != null)
            {
                System.IO.StringWriter tw = new System.IO.StringWriter();
				m_CruncherData.dumpSymbolInfo(tw, info);
                Clipboard.SetText(tw.ToString());
            }
        }

        private void textBoxCache_KeyPress(object sender, KeyPressEventArgs e)
        {
            if (e.KeyChar == (char)Keys.Enter || e.KeyChar == (char)Keys.Escape)
            {
                ShowSelectedSymbolInfo();
            }
            base.OnKeyPress(e);
        }

        private void textBoxCache_Leave(object sender, EventArgs e)
        {
            ShowSelectedSymbolInfo();
        }

        private void setPrefetchStartOffsetToolStripMenuItem_Click(object sender, EventArgs e)
        {
            if (dataGridViewSymbolInfo.SelectedRows.Count != 0)
            {
                DataGridViewRow selectedRow = dataGridViewSymbolInfo.SelectedRows[0];
                long symbolOffset = Convert.ToUInt32(selectedRow.Cells[1].Value);
                m_prefetchStartOffset = symbolOffset;
                ShowSelectedSymbolInfo();
            }
        }

        private void dataGridViewSymbolInfo_CellMouseDoubleClick(object sender, DataGridViewCellMouseEventArgs e)
        {
            if (dataGridViewSymbolInfo.SelectedRows.Count != 0)
            {
                DataGridViewRow selectedRow = dataGridViewSymbolInfo.SelectedRows[0];
				CruncherSymbol info = selectedRow.Tag as CruncherSymbol;
                if (info != null && info.TypeName != null)
                {
                    textBoxFilter.Text = info.TypeName.Replace("*", "[*]");
                }
            }
        }

		private void buttonHistoryBack_Click(object sender, EventArgs e)
		{
			if (UndoStack.Count > 1)
			{
				RedoStack.Push(UndoStack.Pop());

				bUpdateStack = false;
				textBoxFilter.Text = UndoStack.Peek();
				bUpdateStack = true;
			}
		}

		private void buttonHistoryForward_Click(object sender, EventArgs e)
		{
			if (RedoStack.Count > 0)
			{
				UndoStack.Push(RedoStack.Pop());

				bUpdateStack = false;
				textBoxFilter.Text = UndoStack.Peek();
				bUpdateStack = true;
			}
		}

		private void CruncherSharp_MouseDown(object sender, MouseEventArgs e)
		{
			if (e.Button == MouseButtons.XButton1)
			{
				buttonHistoryBack.PerformClick();
			}
			else if (e.Button == MouseButtons.XButton2)
			{
				buttonHistoryForward.PerformClick();
			}
		}

		private void BindControlMouseClicks(Control con)
		{
			con.MouseClick += delegate (object sender, MouseEventArgs e)
			{
				TriggerMouseClicked(sender, e);
			};
			// bind to controls already added
			foreach (Control i in con.Controls)
			{
				BindControlMouseClicks(i);
			}
			// bind to controls added in the future
			con.ControlAdded += delegate (object sender, ControlEventArgs e)
			{
				BindControlMouseClicks(e.Control);
			};
		}

		private void TriggerMouseClicked(object sender, MouseEventArgs e)
		{
			CruncherSharp_MouseDown(sender, e);
		}

		private void reloadCurrentToolStripMenuItem_Click(object sender, EventArgs e)
		{			
			if(loadingBackgroundWorker.IsBusy)
			{
				MessageBox.Show(this, "Already loading a file! Please wait for it to finish.");
				return;
			}

			ResetLoadingBar();
			loadingBackgroundWorker.RunWorkerAsync();			
		}
	}
}
