/* Export the actual preview SVGs with sharp. No firmware or device access. */
const fs=require('fs'),path=require('path'),crypto=require('crypto');
const project=path.resolve(__dirname,'..'),out=path.join(project,'design','portal-800x480');
const modules=process.env.CODEX_NODE_MODULES||'C:/Users/Diego/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const sharp=require(require.resolve('sharp',{paths:[modules]}));
const source=process.argv[2]||'D:/PROYECTOS/DISPOSITIVOS/ES3C28P ESP32-S3 DISPLAY/PROYECTOS/KLIPPER_ES3C28P_SOLID';
const assets=path.join(out,'assets');fs.mkdirSync(assets,{recursive:true});
const provenance=[];
for(const [key,file] of [['glados','glados_suspended.png'],['potatos','potatOS_panel.png']]){
 const from=path.join(source,'assets','glados',file),data=fs.readFileSync(from);
 fs.writeFileSync(path.join(assets,file),data);provenance.push({key,file,source:from,sha256:crypto.createHash('sha256').update(data).digest('hex')});
}
const fixturePath=path.join(source,'tests','fixtures','thumbnail_100.h');
const png=Buffer.from(fs.readFileSync(fixturePath,'utf8').match(/0x[0-9a-f]{2}/gi).map(x=>parseInt(x,16)));
fs.writeFileSync(path.join(assets,'thumbnail-fixture.png'),png);
provenance.push({key:'thumbnail',file:'thumbnail-fixture.png',source:fixturePath,sha256:crypto.createHash('sha256').update(png).digest('hex'),note:'Synthetic 100×100 fixture; not a real print thumbnail.'});
fs.writeFileSync(path.join(assets,'provenance.json'),JSON.stringify({date:'2026-10-24',timezone:'America/Bogota',copiedWithoutModification:provenance},null,2)+'\n');
globalThis.portalAssets=Object.fromEntries(provenance.map(a=>[a.key,'data:image/png;base64,'+fs.readFileSync(path.join(assets,a.file)).toString('base64')]));
fs.writeFileSync(path.join(assets,'assets.js'),'globalThis.portalAssets='+JSON.stringify(globalThis.portalAssets)+';\n');
const P=require(path.join(out,'portal.js')),captures=path.join(out,'capturas');fs.mkdirSync(captures,{recursive:true});
const variants=[...P.views.filter(([v])=>v!=='orientation').map(([view,label])=>({name:view,label,state:{view}})),{name:'orientation-r90',label:'Horizontal R90',state:{view:'orientation',rotation:90}},{name:'orientation-r270',label:'Horizontal R270',state:{view:'orientation',rotation:270}},{name:'confirm-emergency',label:'Confirmación de emergencia',state:{view:'printing',modal:'emergency'}},{name:'edit-target',label:'Editar objetivo',state:{view:'toolhead',modal:'edit-hotend',draft:210}},{name:'printing-thumbnail',label:'Impresión con miniatura de prueba',state:{view:'printing',thumbnail:true}}];
(async()=>{
 for(const v of variants){const svg=P.render({...P.initial(),...v.state});let portable=svg;for(const a of provenance)portable=portable.replaceAll(globalThis.portalAssets[a.key],'../assets/'+a.file);fs.writeFileSync(path.join(captures,v.name+'.svg'),portable);await sharp(Buffer.from(svg)).png().toFile(path.join(captures,v.name+'.png'));}
 async function sheet(name,items,cols,scale){const tileW=Math.round(800*scale),tileH=Math.round(480*scale),pad=24,labelH=35;const rows=Math.ceil(items.length/cols),w=cols*(tileW+pad)+pad,h=rows*(tileH+labelH+pad)+pad;let overlays=[];for(let i=0;i<items.length;i++){const [file,label]=items[i],x=pad+(i%cols)*(tileW+pad),y=pad+Math.floor(i/cols)*(tileH+labelH+pad);const labelSvg=`<svg width="${tileW}" height="${labelH}"><text x="0" y="21" font-family="Arial" font-size="16" fill="#abc3df">${label}</text></svg>`;overlays.push({input:Buffer.from(labelSvg),left:x,top:y},{input:await sharp(path.join(captures,file+'.png')).resize(tileW,tileH).toBuffer(),left:x,top:y+labelH});}await sharp({create:{width:w,height:h,channels:4,background:'#10171d'}}).composite(overlays).png().toFile(path.join(out,name));}
 await sheet('propuesta.png',[['startup','ARRANQUE · PORTAL'],['standby','ESPERA · PORTAL'],['printing','IMPRESIÓN · PORTAL'],['toolhead','TOOLHEAD · PORTAL']],2,0.8);
 await sheet('orientaciones.png',[['orientation-r90','R90 · borde nativo superior a la izquierda'],['orientation-r270','R270 · borde nativo superior a la derecha']],1,1);
 fs.writeFileSync(path.join(out,'manifest.json'),JSON.stringify({date:'2026-10-24',logicalSize:[800,480],theme:'PORTAL',type:'visual-proposal',deviceAccess:false,networkAccess:false,variants:variants.map(v=>({name:v.name,label:v.label,size:[800,480],pngSha256:crypto.createHash('sha256').update(fs.readFileSync(path.join(captures,v.name+'.png'))).digest('hex')}))},null,2)+'\n');
 console.log(JSON.stringify({variants:variants.length,output:out,dimensions:[800,480]}));
})().catch(e=>{console.error(e);process.exitCode=1;});
