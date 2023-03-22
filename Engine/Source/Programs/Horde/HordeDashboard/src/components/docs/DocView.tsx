import { Stack } from "@fluentui/react";
import { action, makeObservable, observable } from "mobx";
import { observer } from "mobx-react-lite";
import { useRef, useState } from "react";
import { useLocation } from "react-router-dom";
import { Markdown } from "../../base/components/Markdown";
import { ISideRailLink, SideRail } from "../../base/components/SideRail";
import { useWindowSize } from "../../base/utilities/hooks";
import { hordeClasses, modeColors } from "../../styles/Styles";
import { BreadcrumbItem, Breadcrumbs } from "../Breadcrumbs";
import { TopNav } from "../TopNav";

type Anchor = {
   text: string;
   anchor: string;
}

const documentCache = new Map<string, string>();
const crumbCache = new Map<string, BreadcrumbItem[]>();
const anchorCache = new Map<string, Anchor[]>();

type State = {
   crumbs: BreadcrumbItem[];
   jumpLinks: ISideRailLink[];
}

class LinkState {

   constructor() {
      makeObservable(this);
   }

   @observable
   state: State = { crumbs: [], jumpLinks: [] };

   @action
   setState(crumbs: BreadcrumbItem[], jumpLinks: ISideRailLink[]) {
      this.state = { crumbs: crumbs, jumpLinks: jumpLinks };
   }
}

const linkState = new LinkState();

const DocPanel: React.FC<{ docName: string }> = ({ docName }) => {

   const [state, setState] = useState(0);

   const docRef = useRef<HTMLDivElement>(null);

   const text = documentCache.get(docName);

   if (!text) {
      fetch(`/${docName}`, { cache: "no-cache" })
         .then((response) => response.text())
         .then((textContent) => {

            let crumbs: BreadcrumbItem[] = [];
            let anchors: Anchor[] = [];

            textContent = textContent.trim();

            if (textContent?.indexOf("<!doctype html>") === -1) {

               // generate anchors
               let lines = (textContent.match(/[^\r\n]+/g) ?? []) as string[];
               lines = lines.map(i => i.trim()).filter( i => !!i);
               lines.forEach(line => {
                  let anchor = "";

                  if (line.startsWith("# ")) {
                     anchor = line.split("# ")[1];
                  }

                  if (line.startsWith("## ")) {
                     anchor = line.split("## ")[1];
                  }

                  if (anchor) {
                     anchor = anchor.trim();
                     anchors.push({text: anchor, anchor: anchor.replace(/[^a-z0-9- ]/gi, '').replace(/ /gi, '-').toLowerCase() })
                  }
               })               

               // generate crumbs
               if (lines && lines.length) {
                  const line = lines[0];
                  if (line.startsWith("[Horde]")) {

                     line.split(">").map(c => c.trim()).forEach(c => {
                        const link = c.split("](");
                        if (link.length === 1) {
                           crumbs.push({ text: link[0] })
                        } else {
                           let [cname, clink] = [link[0].replace("[", ""), link[1].replace(")", "")];

                           let docPath = docName.split("/").slice(2);
                           docPath.pop();

                           const relative = (clink.match(/..\//g) || []).length;

                           if (clink.indexOf("README.md") !== -1) {
                              clink = "/docs"
                           } else {
                              docPath = docPath.slice(docPath.length - 1, -relative);
                              const elements = clink.split("/");
                              docPath.push(elements[elements.length - 1]);
                              clink = docPath.join("/");
                           }

                           if (cname !== "Horde") {
                              crumbs.push({ text: cname, link: clink });
                           }

                        }
                     });

                     textContent = textContent.replace(line, "");
                  }
               }

               documentCache.set(docName, textContent);
               crumbCache.set(docName, crumbs);
               anchorCache.set(docName, anchors);

            } else {
               documentCache.set(docName, `###Missing document ${docName}`);
               crumbCache.set(docName, []);
               anchorCache.set(docName, []);
            }

            setState(state + 1);

         });
   }

   if (!text) {
      return null;
   }

   let crumbs = crumbCache.get(docName) ?? [];
   let anchors = anchorCache.get(docName) ?? [];

   linkState.setState(crumbs, anchors.map(a => {
      return { text: a.text, url: a.anchor}
   }));


   return <Stack>
      <Stack styles={{ root: { paddingTop: 0, paddingLeft: 12, paddingRight: 12, width: "100%" } }} >
         <Stack tokens={{ childrenGap: 12 }}>
            <Stack style={{ paddingBottom: 24, paddingLeft: 6, paddingRight: 6, paddingTop: 12 }}>
               <div ref={docRef}>
                  <Markdown>{text}</Markdown>
               </div>
            </Stack>
         </Stack>
      </Stack>
   </Stack>;


}

const DocRail = observer(() => {

   const state = linkState.state;

   const refLinks: ISideRailLink[] = [];
   refLinks.push({ text: "Introduction", url: "/docs" });
   refLinks.push({ text: "User Guild", url: "/docs/Users.md" });
   refLinks.push({ text: "Deployment", url: "/docs/Deployment.md" });
   refLinks.push({ text: "Configuration", url: "/docs/Config.md" });
   refLinks.push({ text: "Horde Internals", url: "/docs/Internals.md" });

   return <SideRail jumpLinks={state.jumpLinks} relatedLinks={refLinks} />

})

const DocCrumbs = observer(() => {

   const state = linkState.state;

   const crumbs: BreadcrumbItem[] = [];
   crumbs.push({ text: "Documentation", link: "/docs" })
   crumbs.push(...state.crumbs.map(c => { return { text: c.text, link: c.link } }));

   return <Breadcrumbs items={crumbs} />

})



export const DocView = () => {

   const location = useLocation();

   // fixme
   let docName = location.pathname.replace("/docs/", "").replace("/docs", "").trim();
   if (!docName || docName.indexOf("README.md") !== -1) {
      docName = "documentation/README.md";
   } else {

      if (!docName.startsWith("Docs/")) {
         docName = `documentation/Docs/${docName}`;
      } else {
         docName = docName.replace("Docs/", "documentation/Docs/");
      }
   }

   const windowSize = useWindowSize();
   const vw = Math.max(document.documentElement.clientWidth, window.innerWidth || 0);

   return <Stack className={hordeClasses.horde}>
      <TopNav />
      <DocCrumbs />
      <Stack horizontal>
         <div key={`windowsize_streamview_${windowSize.width}_${windowSize.height}`} style={{ width: vw / 2 - (1440 / 2), flexShrink: 0, backgroundColor: modeColors.background }} />
         <Stack tokens={{ childrenGap: 0 }} styles={{ root: { backgroundColor: modeColors.background, width: "100%", "position": "relative", paddingTop: 12 } }}>
            <div style={{ overflowY: 'scroll', overflowX: 'hidden', height: "calc(100vh - 162px)" }} data-is-scrollable={true}>
               <Stack horizontal>
                  <Stack style={{ width: 1240, paddingTop: 6, marginLeft: 4, height: '100%' }}>
                     <Stack className={hordeClasses.raised}>
                        <Stack style={{ width: "100%", height: "max-content" }} tokens={{ childrenGap: 18 }}>
                           <DocPanel docName={docName} />
                        </Stack>
                     </Stack>
                     <Stack style={{ paddingBottom: 24 }} />
                  </Stack>
                  <Stack style={{ paddingLeft: 1280, paddingTop: 12, position: "absolute", pointerEvents: "none" }}>
                     <div style={{ pointerEvents: "all" }}>
                        <DocRail />
                     </div>
                  </Stack>
               </Stack>
            </div>
         </Stack>
      </Stack>
   </Stack>

}

