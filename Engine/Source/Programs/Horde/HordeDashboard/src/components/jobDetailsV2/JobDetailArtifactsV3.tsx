// Copyright Epic Games, Inc. All Rights Reserved.

import { DetailsList, DetailsListLayoutMode, Dropdown, Link as FluentLink, FontIcon, IColumn, IDropdownOption, IconButton, Modal, PrimaryButton, ScrollablePane, Selection, SelectionMode, SelectionZone, Spinner, SpinnerSize, Stack, Text, mergeStyleSets } from "@fluentui/react";
import { action, makeObservable, observable } from "mobx";
import { observer } from "mobx-react-lite";
import { useEffect, useState } from "react";
import backend from "../../backend";
import { ArtifactContextType, GetArtifactDirectoryEntryResponse, GetArtifactDirectoryResponse, GetArtifactFileEntryResponse, GetArtifactResponseV2 } from "../../backend/Api";
import { hordeClasses } from "../../styles/Styles";
import { JobDetailsV2 } from "./JobDetailsViewCommon";


enum BrowserType {
   Directory,
   File,
   NavigateUp
}

type BrowserItem = {
   key: string;
   text: string;
   icon?: string;
   size?: number;
   type: BrowserType;
}


class ArtifactsHandler {

   constructor(details: JobDetailsV2, stepId: string) {
      makeObservable(this);
      this.details = details;
      this.stepId = stepId;

      this.set();
   }

   @observable
   updated = 0

   @action
   updateReady() {
      this.updated++;
   }

   async set() {

      const details = this.details;
      const jobData = details.jobData!;

      const artifacts = details.stepArtifacts.get(this.stepId);
      if (!artifacts) {
         console.error(`Missing artifacts for job: ${jobData.id} step: ${this.stepId}`);
         return;
      }

      this.artifacts = artifacts;

      this.contexts = [];

      let a = artifacts.find(a => a.type === "step-saved");
      if (a) {
         this.contexts.push("step-saved");
         this.context = "step-saved";
      }

      a = artifacts.find(a => a.type === "step-output");
      if (a) {
         this.contexts.push("step-output");
         this.context = this.context ?? "step-saved";
      }

      a = artifacts.find(a => a.type === "step-trace");
      if (a) {
         this.contexts.push("step-trace");
         this.context = this.context ?? "step-trace";
      }

      if (!this.context) {
         this.updateReady();
         return;
      }

      a = artifacts.find(a => a.type === this.context)!;
      this.artifact = a;

      this.browse = await backend.getBrowseArtifacts(a.id);

      this.updateReady();

   }

   hasContext(c: ArtifactContextType) {

      return !!this.artifacts?.find(a => a.type === c);

   }

   async setContext(c: ArtifactContextType) {


      let a = this.artifacts?.find(a => a.type === c)!;
      if (!a) {
         return;
      }

      this.context = c;

      this.artifact = a;
      this.path = undefined;
      this.browse = await backend.getBrowseArtifacts(a.id);
      this.updateReady();

   }

   async browseTo(path: string) {

      if (!this.artifact) {
         return;
      }

      this.browse = await backend.getBrowseArtifacts(this.artifact.id, path);

      this.path = path;

      this.updateReady();

   }

   get currentSelection(): { filesSelected: number, directoriesSelected: number, size: number, items: BrowserItem[] } {

      let result = {
         filesSelected: 0,
         directoriesSelected: 0,
         size: 0,
         items: [] as BrowserItem[]
      }

      const browse = this.browse;
      if (!browse) {
         return result;
      }

      let selection = this.selection?.getSelection() as (BrowserItem[] | undefined);

      if (!selection?.length) {
         selection = [];

         browse.directories.forEach(d => {
            selection!.push({ key: d.ioHash, text: d.name, icon: "Folder", type: BrowserType.Directory, size: d.length });
         });

         browse.files.forEach(d => {
            selection!.push({ key: d.ioHash, text: d.name, icon: "Document", type: BrowserType.File, size: d.length });
         });
      }

      selection.forEach(b => {
         result.items.push(b);
         if (b.type === BrowserType.Directory) {
            result.directoriesSelected++;
            result.size += b.size ?? 0;
         }
         if (b.type === BrowserType.File) {
            result.filesSelected++;
            result.size += b.size ?? 0;
         }
      });

      return result;

   }

   get directories(): GetArtifactDirectoryEntryResponse[] {
      return this.browse?.directories ?? [];
   }

   get files(): GetArtifactFileEntryResponse[] {
      return this.browse?.files ?? [];
   }

   clear() {
      this.selection = undefined;
      this.selectionCallback = undefined;
      this.path = undefined;
      this.browse = undefined;
      this.artifact = undefined;
      this.artifacts = undefined;
      this.context = undefined;
      this.contexts = undefined;
      this.stepId = "";
   }


   details: JobDetailsV2;

   selectionCallback?: () => void;

   path?: string;

   selection?: Selection = new Selection({ onSelectionChanged: () => { if (this.selectionCallback) this.selectionCallback() } });

   browse?: GetArtifactDirectoryResponse;

   artifact?: GetArtifactResponseV2;
   artifacts?: GetArtifactResponseV2[];

   contexts?: ArtifactContextType[];

   context?: ArtifactContextType;

   stepId: string;


}

const styles = mergeStyleSets({
   list: {
      selectors: {
         '.ms-DetailsRow': {
            border: "1px solid #EDEBE9"
         }
      }
   }
});


const BrowseBreadCrumbs: React.FC<{ handler: ArtifactsHandler }> = observer(({ handler }) => {

   // subscribe
   if (handler.updated) { }

   const fontSize = 16;

   if (!handler.path) {
      return <Stack><Text style={{ fontSize: fontSize, paddingLeft: 4, paddingRight: 4 }}>/
      </Text>
      </Stack>
   }

   const ppath = handler.path.split("/");

   const elements = ppath.map((e, index) => {
      const path = ppath.slice(0, index + 1).join("/");
      return <Stack horizontal onClick={() => handler.browseTo(path)} style={{ cursor: "pointer" }}>
         <Stack>
            <Text style={{ fontSize: fontSize }}>{e}</Text>
         </Stack>
         {index !== (ppath.length - 1) && <Stack style={{ paddingLeft: 4, paddingRight: 4 }}><Text style={{ fontSize: fontSize }}>/</Text></Stack>}
      </Stack>
   });

   elements.unshift(<Stack style={{ cursor: "pointer" }} onClick={() => handler.browseTo("")}>
      <Stack style={{ paddingLeft: 4, paddingRight: 4 }}>
         <Text style={{ fontSize: fontSize }}>/</Text>
      </Stack>
   </Stack>);

   return <Stack horizontal>{elements}</Stack>

})

function formatBytes(bytes: number, decimals = 2) {
   if (!+bytes) return '0 Bytes'

   const k = 1024
   const dm = decimals < 0 ? 0 : decimals
   const sizes = ['Bytes', 'KB', 'MB', 'GB', 'TB', 'PB', 'EB', 'ZB', 'YB']

   const i = Math.floor(Math.log(bytes) / Math.log(k))

   return `${parseFloat((bytes / Math.pow(k, i)).toFixed(dm))} ${sizes[i]}`
}


const DownloadButton: React.FC<{ handler: ArtifactsHandler }> = observer(({ handler }) => {

   const [downloading, setDownloading] = useState(false);
   const [selectKey, setSelectionKey] = useState(0);


   // subscribe
   if (handler.updated) { };

   const browse = handler.browse;

   if (!browse) {
      return null;
   }

   handler.selectionCallback = () => {
      setSelectionKey(selectKey + 1);
   }

   let buttonText = "Download";


   const selection = handler.currentSelection;


   let sizeText = "0KB";
   if (selection.size) {
      sizeText = formatBytes(selection.size, (selection.size < (1024 * 1024)) ? 0 : 1)
   }

   if (selection.directoriesSelected || selection.filesSelected > 1) {
      buttonText = `Download Zip (${sizeText})`;
   } else if (selection.filesSelected === 1) {
      buttonText = `Download File (${sizeText})`;
   }

   return <Stack>
      <PrimaryButton styles={{ root: { fontFamily: 'Horde Open Sans Semibold !important' } }} disabled={!selection.size} onClick={async () => {

         const selection = handler.currentSelection.items;

         if (!selection?.length || !handler.artifact) {
            return;
         }

         // download a single file
         if (selection.length === 1) {
            const item = selection[0] as BrowserItem;
            if (item.type === BrowserType.File) {

               try {
                  setDownloading(true);
                  await backend.downloadArtifactV2(handler.artifact.id, (handler.path ? handler.path + "/" : "") + item.text, item.text);
               } catch (err) {
                  console.error(err);
               } finally {
                  setDownloading(false);
               }

               return;
            }
         }

         const path = handler.path ? handler.path + "/" : "";

         const filters = selection.map(s => {
            const item = s as BrowserItem;

            if (item.type === BrowserType.NavigateUp) {
               return "";
            }

            if (item.type === BrowserType.Directory) {
               return `${path}${item.text}/...`;
            }
            return `${path}${item.text}`;
         }).filter(f => !!f);

         let context = handler.context;
         let contextName = "step";
         if (context === "step-output") {
            contextName = "output";
         }

         if (context === "step-trace") {
            contextName = "trace";
         }

         const filename = "horde-" + contextName + "-artifacts-" + handler.details!.jobId! + '-' + handler.stepId + ".zip";

         try {
            setDownloading(true);
            await backend.downloadArtifactZipV2(handler.artifact.id, { filter: filters }, filename);
         } catch (err) {
            console.error(err);
         } finally {
            setDownloading(false);
         }

      }}>{buttonText}{!!downloading && <Spinner style={{ paddingLeft: 12 }} size={SpinnerSize.medium} />}</PrimaryButton>
   </Stack>

});

const JobDetailArtifactsInner: React.FC<{ handler: ArtifactsHandler }> = observer(({ handler }) => {

   // subscribe
   if (handler.updated) { }

   const browse = handler.browse;

   if (!browse) {
      return null;
   }

   const coptions: IDropdownOption[] = [];
   if (handler.hasContext("step-saved")) {
      coptions.push({ key: 'step-saved', text: 'Step' })
   }
   if (handler.hasContext("step-output")) {
      coptions.push({ key: 'step-output', text: 'Output' })
   }

   if (handler.hasContext("step-trace")) {
      coptions.push({ key: 'step-trace', text: 'Trace' })
   }

   const items: BrowserItem[] = [];

   if (handler.path?.length) {
      items.push({ key: "navigate up", text: "..", type: BrowserType.NavigateUp });
   }

   browse.directories.forEach(d => {
      items.push({ key: d.ioHash, text: d.name, icon: "Folder", type: BrowserType.Directory, size: d.length });
   });

   browse.files.forEach(d => {
      items.push({ key: d.ioHash, text: d.name, icon: "Document", type: BrowserType.File, size: d.length });
   });

   const columns: IColumn[] = [
      { key: 'column1', name: 'Name', minWidth: 640, maxWidth: 640, isResizable: false, isPadded: false },
      { key: 'column2', name: 'Size', minWidth: 128, maxWidth: 128, isResizable: false, isPadded: false },
   ];

   const renderItem = (item: any, index?: number, column?: IColumn) => {

      if (!column) {
         return null;
      }

      if (column.name === "Size") {
         if (!item.size) {
            return null;
         }
         return <Stack>
            <Text>{formatBytes(item.size, (item.size < (1024 * 1024)) ? 0 : 1)}</Text>
         </Stack>

      }

      if (column.name === "Name") {

         let href: string | undefined;
         let target: string | undefined;

         if (item.type === BrowserType.File) {
            target = "_blank";
            const path = encodeURI(handler.path + "/" + item.text);
            const server = backend.serverUrl;
            href = `${server}/api/v2/artifacts/${handler.artifact!.id}/file?path=${path}&inline=true`;
         }

         return <FluentLink href={href} target={target} style={{ color: "unset", height: "100%", width: "100%" }}><Stack style={{ cursor: "pointer" }} onClick={(ev) => {

            if (item.type === BrowserType.Directory) {
               const nbrowse = handler.path ? `${handler.path}/${item.text}` : item.text;
               handler.browseTo(nbrowse);
            }
            if (item.type === BrowserType.NavigateUp && handler.path) {
               const nbrowse = handler.path.split("/")
               nbrowse.pop();
               handler.browseTo(nbrowse.join("/"));
            }

         }}>
            {item.type !== BrowserType.NavigateUp && <Stack data-selection-disabled horizontal tokens={{ childrenGap: 8 }}>
               <Stack>
                  <FontIcon style={{ fontSize: 16 }} iconName={item.icon} />
               </Stack>
               <Stack>
                  <Text>{item.text}</Text>
               </Stack>
            </Stack>}
            {item.type === BrowserType.NavigateUp && <Stack data-selection-disabled style={{ height: "100%", width: "100%" }}>
               <Text>..</Text>
            </Stack>
            }
         </Stack>
         </FluentLink>
      }

      return null;

   }

   return <Stack tokens={{ childrenGap: 12 }}>
      <Stack horizontal verticalAlign="center" tokens={{ childrenGap: 18 }} style={{ paddingBottom: 12 }}>
         <Stack>
            <Dropdown className={hordeClasses.modal} style={{ width: 128 }} options={coptions} selectedKey={handler.context} onChange={(ev, option) => {
               if (option?.key) {
                  handler.setContext(option.key as ArtifactContextType);
               }
            }} />
         </Stack>
         <Stack>
            <BrowseBreadCrumbs handler={handler} />
         </Stack>
         <Stack grow />
         <DownloadButton handler={handler} />
      </Stack >
      <Stack style={{ height: 492 + 160, position: "relative" }}>
         <ScrollablePane style={{ height: 492 + 160 }}>
            <SelectionZone selection={handler.selection!}>
               <DetailsList
                  className={styles.list}
                  isHeaderVisible={false}
                  compact={true}
                  items={items}
                  columns={columns}
                  layoutMode={DetailsListLayoutMode.fixedColumns}
                  selectionMode={SelectionMode.multiple}
                  selection={handler.selection}
                  selectionPreservedOnEmptyClick={true}
                  //onItemInvoked={this._onItemInvoked} <--- double click*/
                  onRenderItemColumn={renderItem}
               />
            </SelectionZone>

         </ScrollablePane>
      </Stack>
   </Stack >

   //
})



export const JobArtifactsModal: React.FC<{ jobDetails: JobDetailsV2; stepId: string, onClose: () => void }> = ({ jobDetails, stepId, onClose }) => {

   const [handler] = useState(new ArtifactsHandler(jobDetails, stepId));

   useEffect(() => {
      return () => {
         handler?.clear();
      };
   }, [handler]);

   return <Stack>
      <Modal isOpen={true} isBlocking={true} topOffsetFixed={true} styles={{ main: { padding: 8, width: 1180, height: 820, hasBeenOpened: false, top: "80px", position: "absolute" } }} onDismiss={() => onClose()} className={hordeClasses.modal}>
         <Stack styles={{ root: { paddingTop: 18, paddingRight: 12 } }}>
            <Stack style={{ paddingLeft: 24, paddingRight: 24 }}>
               <Stack tokens={{ childrenGap: 12 }} style={{ height: 800 }}>
                  <Stack horizontal>
                     <Stack>
                        <Text variant="mediumPlus" styles={{ root: { fontFamily: "Horde Open Sans SemiBold" } }}>Artifacts</Text>
                     </Stack>
                     <Stack grow />
                     <Stack horizontalAlign="end">
                        <IconButton
                           iconProps={{ iconName: 'Cancel' }}
                           onClick={() => { onClose() }}
                        />
                     </Stack>

                  </Stack>
                  <Stack styles={{ root: { paddingLeft: 4, paddingRight: 0, paddingTop: 8, paddingBottom: 4 } }}>
                     <JobDetailArtifactsInner handler={handler} />
                  </Stack>
               </Stack>
            </Stack>
         </Stack>
      </Modal>
   </Stack>;
};