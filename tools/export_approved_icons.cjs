/* Export the approved local specimen as neutral alpha bitmaps for LVGL. */
const fs = require('fs'), path = require('path'), crypto = require('crypto');
const modules = 'C:/Users/Diego/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const {chromium} = require(require.resolve('playwright', {paths: [modules]}));
const root = path.resolve(__dirname, '..');
const review = path.join(root, 'design/revision-0.6.1');
const out = path.join(root, 'assets/portal/approved-icons');
const selection = JSON.parse(fs.readFileSync(path.join(review, 'selection.json')));
const names = ['print','move','last_job','settings','macro','stop','pause','play',
 'cancel','exclude','extrude','retract','nozzle','bed','home','refresh','power',
 'back','wifi','details','plus','minus','backspace','left','right','up','down',
 'file','audio','mute','cool','motors_off'];
const sha = data => crypto.createHash('sha256').update(data).digest('hex');
const manifest = {approval: 'User approved lamina-iconos-logos-v2.png in this chat',
 size: 64, selection_sha256: sha(fs.readFileSync(path.join(review,'selection.json'))),
 font_sha256: sha(fs.readFileSync(path.join(review,'sources/uicons-solid-rounded.woff2'))), icons: []};
if (selection.length !== names.length) throw Error('Approved selection must contain 32 icons');
fs.mkdirSync(out, {recursive: true});
(async () => {
 const browser = await chromium.launch({executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',headless:true});
 try {
  const page = await browser.newPage({viewport:{width:64,height:64},deviceScaleFactor:1});
  await page.route('http://**', route => route.abort());
  await page.route('https://**', route => route.abort());
  const font = fs.readFileSync(path.join(review,'sources/uicons-solid-rounded.woff2')).toString('base64');
  for (let i=0;i<selection.length;i++) {
   const chosen = selection[i];
   let content, source;
   if(chosen.source) {
    const bytes=fs.readFileSync(path.join(review,'sources',chosen.source.file));
    source={...chosen.source,sha256:sha(bytes)};
    content=`<span style="display:block;width:64px;height:64px;background:white;mask:url('data:image/png;base64,${bytes.toString('base64')}') center/contain no-repeat"></span>`;
   } else {
    source={family:'UIcons Solid Rounded',name:chosen.name,codepoint:chosen.codepoint};
    content=`<i>&#${chosen.codepoint};</i>`;
   }
   await page.setContent(`<style>@font-face{font-family:Uicons;src:url(data:font/woff2;base64,${font})}html,body{margin:0;background:transparent}#glyph{width:64px;height:64px;display:flex;align-items:center;justify-content:center;color:white}i{display:inline-block;font-family:Uicons;font-style:normal;font-size:64px;line-height:1}</style><div id="glyph">${content}</div>`);
   await page.evaluate(()=>document.fonts.ready);
   if(!chosen.source && !await page.evaluate(()=>document.fonts.check('64px Uicons')))throw Error('Font unavailable');
   const file=names[i]+'.png';
   const bitmap=await page.locator('#glyph').screenshot({omitBackground:true});
   fs.writeFileSync(path.join(out,file),bitmap);
   manifest.icons.push({name:names[i],approved_id:chosen.id,file,sha256:sha(bitmap),source});
  }
  manifest.renderer=browser.version();
  fs.writeFileSync(path.join(out,'manifest.json'),JSON.stringify(manifest,null,2)+'\n');
  console.log('Exported '+manifest.icons.length+' approved icons at 64x64');
 } finally {await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
