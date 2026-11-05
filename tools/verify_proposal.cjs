/* Checks only the visual proposal; no printer/device/network endpoint is contacted. */
const fs=require('fs'),path=require('path'),assert=require('assert/strict');
const modules=process.env.CODEX_NODE_MODULES||'C:/Users/Diego/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const {chromium}=require(require.resolve('playwright',{paths:[modules]}));
const project=path.resolve(__dirname,'..'),url=require('url').pathToFileURL(path.join(project,'design','portal-800x480','index.html')).href;
const out=path.join(project,'verification');fs.mkdirSync(out,{recursive:true});
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.PROPOSAL_BROWSER||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',headless:true});
 try{
  const page=await browser.newPage({viewport:{width:1000,height:1000},deviceScaleFactor:1}),errors=[],requests=[];page.on('pageerror',e=>errors.push(e.message));page.on('request',r=>{if(/^https?:/.test(r.url()))requests.push(r.url());});
  await page.goto(url);await page.waitForFunction(()=>!!window.portalPreview);
  const geometry=[];
  for(const [view] of await page.evaluate(()=>PortalProposal.views)){
   await page.evaluate(v=>portalPreview.setState({view:v}),view);
   const bad=await page.evaluate(()=>[...document.querySelectorAll('#lcd>svg text,#lcd>svg [data-action]')].map(e=>{const b=e.getBoundingClientRect(),p=document.querySelector('#lcd').getBoundingClientRect();return {tag:e.tagName,label:e.textContent.trim().slice(0,80),x:b.x-p.x,y:b.y-p.y,w:b.width,h:b.height};}).filter(b=>b.x<-.6||b.y<-.6||b.x+b.w>800.6||b.y+b.h>480.6));
   geometry.push({view,outside:bad});assert.equal(bad.length,0,'Content outside LCD: '+JSON.stringify({view,bad}));
  }
  await page.evaluate(()=>portalPreview.setState({view:'standby'}));
  await page.locator('[data-action="files"]').click();assert.equal(await page.evaluate(()=>portalPreview.getState().view),'files');
  await page.locator('[data-action="file-2"]').click();assert.equal(await page.evaluate(()=>portalPreview.getState().file),2);
  await page.locator('[data-action="confirm-print"]').click();assert.equal(await page.evaluate(()=>portalPreview.getState().modal),'confirm-print');
  await page.locator('[data-action="modal-close"]').click();assert.equal(await page.evaluate(()=>portalPreview.getState().view),'files');
  await page.locator('[data-action="confirm-print"]').click();await page.locator('[data-action="modal-confirm"]').click();assert.equal(await page.evaluate(()=>portalPreview.getState().view),'printing');assert((await page.locator('#lcd').textContent()).includes('ENGRANAJE_24T.gcode'));
  await page.evaluate(()=>portalPreview.setState({view:'toolhead',hotend:0}));
  await page.locator('[data-action="edit-hotend"]').click();await page.locator('[data-action="draft-plus"]').click();await page.locator('[data-action="modal-close"]').click();assert.equal(await page.evaluate(()=>portalPreview.getState().hotend),0);
  await page.locator('[data-action="edit-hotend"]').click();await page.locator('[data-action="draft-plus"]').click();await page.locator('[data-action="modal-confirm"]').click();assert.equal(await page.evaluate(()=>portalPreview.getState().hotend),5);
  await page.evaluate(()=>portalPreview.setState({view:'toolhead'}));await page.locator('[data-action="materials"]').click();assert.equal(await page.evaluate(()=>portalPreview.getState().hotend),0);await page.locator('[data-action="apply-material"]').click();assert.deepEqual(await page.evaluate(()=>[portalPreview.getState().hotend,portalPreview.getState().bed]),[200,65]);
  await page.evaluate(()=>portalPreview.setState({view:'printing'}));await page.locator('[data-action="pause"]').click();assert.equal(await page.evaluate(()=>portalPreview.getState().view),'paused');await page.locator('[data-action="resume"]').click();assert.equal(await page.evaluate(()=>portalPreview.getState().view),'printing');
  await page.evaluate(()=>portalPreview.setState({view:'exclude'}));await page.locator('[data-action="object-1"]').last().click();await page.locator('[data-action="exclude-object"]').click();assert.deepEqual(await page.evaluate(()=>portalPreview.getState().excluded),[1]);assert.equal(await page.locator('[data-action="exclude-object"]').getAttribute('aria-disabled'),'true');
  await page.selectOption('#rotation','270');assert.equal(await page.evaluate(()=>portalPreview.getState().rotation),270);assert.equal(await page.evaluate(()=>portalPreview.getState().view),'orientation');
  await page.evaluate(()=>portalPreview.setState({view:'printing'}));await page.check('#thumbnail');assert.equal(await page.evaluate(()=>portalPreview.getState().thumbnail),true);
  const transforms=[];
  for(const r of [90,270]){let count=0;const seen=new Set();for(let v=0;v<480;v++)for(let u=0;u<800;u++){const x=r===90?479-v:v,y=r===90?u:799-u;assert(x>=0&&x<480&&y>=0&&y<800);const uu=r===90?y:799-y,vv=r===90?479-x:x;assert.equal(uu,u);assert.equal(vv,v);seen.add(y*480+x);count++;}assert.equal(seen.size,384000);transforms.push({rotation:r,pixels:count,bijective:true,touchInverse:true});}
  await page.evaluate(()=>portalPreview.setState({view:'standby'}));await page.screenshot({path:path.join(out,'browser-preview.png'),fullPage:true});
  const responsive=[];for(const width of [736,390,320]){await page.setViewportSize({width,height:1000});responsive.push({width,overflow:await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth)});}assert(responsive.every(x=>!x.overflow));
  assert.equal(errors.length,0,JSON.stringify(errors));assert.equal(requests.length,0,JSON.stringify(requests));
  const result={date:'2026-10-24',timezone:'America/Bogota',status:'PASS',type:'proposal-only',renderer:'SVG proposal; not LVGL firmware',views:geometry,interactionChecks:['navigation','file selection','print confirmation/cancel','target cancel/apply','material selection does not apply until confirmation','pause/resume','exclude selection','orientation selector','thumbnail toggle'],transforms,responsive,consoleErrors:errors,networkRequests:requests,hardwareVerified:false,firmwareBuilt:false,deviceFlashed:false};
  fs.writeFileSync(path.join(out,'visual-review.json'),JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify({status:result.status,views:geometry.length,transforms,interactionChecks:result.interactionChecks.length}));
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
