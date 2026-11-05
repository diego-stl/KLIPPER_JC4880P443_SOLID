(function(){
'use strict';
const P=globalThis.PortalProposal,lcd=document.getElementById('lcd'),nav=document.getElementById('view-list');
let state=P.initial(),timer;
const params=new URLSearchParams(location.search);
if(P.views.some(([v])=>v===params.get('view')))state.view=params.get('view');
if(params.get('rotation')==='270')state.rotation=270;
nav.innerHTML=P.views.map(([v,label])=>`<button type="button" data-view="${v}" aria-pressed="false">${label}</button>`).join('');
function draw(){lcd.innerHTML=P.render(state);nav.querySelectorAll('button').forEach(b=>b.setAttribute('aria-pressed',String(b.dataset.view===state.view)));document.getElementById('view-caption').textContent=P.captions[state.view];document.getElementById('rotation').value=state.rotation;document.getElementById('thumbnail').checked=state.thumbnail;}
function go(view){state.history.push(state.view);state.view=view;state.modal=null;state.notice=null;draw();}
function notify(message){state.notice=message;document.getElementById('announcement').textContent=message;clearTimeout(timer);draw();timer=setTimeout(()=>{state.notice=null;draw();},2600);}
function dispatch(id){
 if(state.modal&&!id.startsWith('modal-')&&!id.startsWith('draft-'))return;
 if(P.views.some(([v])=>v===id)){go(id);return;}
 if(id==='back'){state.view=state.history.pop()||'standby';state.modal=null;state.notice=null;draw();return;}
 if(id.startsWith('file-')){state.file=Number(id.slice(5));draw();return;}
 if(id.startsWith('macro-')){state.macro=Number(id.slice(6));draw();return;}
 if(id.startsWith('object-')){state.object=Number(id.slice(7));draw();return;}
 if(id==='pause'){go('paused');return;}
 if(id==='resume'){go('printing');return;}
 if(['emergency','cancel','confirm-print','shutdown','restart'].includes(id)){state.modal=id;draw();return;}
 if(id==='edit-hotend'||id==='edit-bed'){state.modal=id;state.draft=state[id==='edit-hotend'?'hotend':'bed'];draw();return;}
 if(id==='draft-plus'||id==='draft-minus'){const max=state.modal==='edit-hotend'?300:120;state.draft=Math.min(max,Math.max(0,state.draft+(id==='draft-plus'?5:-5)));draw();return;}
 if(id==='modal-close'){state.modal=null;draw();return;}
 if(id==='modal-confirm'){
  const modal=state.modal;state.modal=null;
  if(modal==='edit-hotend'||modal==='edit-bed'){state[modal==='edit-hotend'?'hotend':'bed']=state.draft;notify('Objetivo aplicado · simulación');}
  else if(modal==='confirm-print'){go('printing');notify('Impresión iniciada · simulación');}
  else if(modal==='cancel'){go('standby');notify('Trabajo cancelado · simulación');}
  else if(modal==='emergency'){go('error');notify('Parada solicitada · simulación');}
  else notify('Solicitud aceptada · simulación');
  return;
 }
 if(id==='materials'){const v=['PLA','PETG','TPU','ABS'];state.material=v[(v.indexOf(state.material)+1)%v.length];draw();return;}
 if(id==='apply-material'){const v={PLA:[170,55],PETG:[200,65],TPU:[170,55],ABS:[255,100]};[state.hotend,state.bed]=v[state.material];notify('Preset '+state.material+' aplicado · simulación');return;}
 if(id==='cool'){state.hotend=0;state.bed=0;notify('Objetivos a 0 °C · simulación');return;}
 if(id==='steps'){const v=[1,5,10,20,50,100];state.step=v[(v.indexOf(state.step)+1)%v.length];draw();return;}
 if(id==='exclude-object'){state.excluded=[...new Set([...state.excluded,state.object])];notify('Objeto '+(state.object+1)+' excluido · simulación');return;}
 if(id==='brightness'){state.brightness=state.brightness>=100?5:Math.min(100,state.brightness+5);draw();return;}
 if(id==='timeout'){const v=[1,5,10];state.timeout=v[(v.indexOf(state.timeout)+1)%v.length];draw();return;}
 if(id.startsWith('point-')){notify('Marcador '+id.slice(6)+' · toque simulado');return;}
 if(id.startsWith('jog-')||id.startsWith('home-')||['motors-off','extrude','retract','run-macro'].includes(id)){notify('Orden de muestra · no se envía a la impresora');return;}
}
nav.addEventListener('click',e=>{const b=e.target.closest('[data-view]');if(b)go(b.dataset.view);});
lcd.addEventListener('click',e=>{const b=e.target.closest('[data-action]');if(b&&b.getAttribute('aria-disabled')!=='true')dispatch(b.dataset.action);});
lcd.addEventListener('keydown',e=>{if(e.key==='Escape'&&state.modal){dispatch('modal-close');return;}if(e.key==='Enter'||e.key===' '){const b=e.target.closest('[data-action]');if(b&&b.getAttribute('aria-disabled')!=='true'){e.preventDefault();dispatch(b.dataset.action);}}});
document.getElementById('rotation').addEventListener('change',e=>{state.rotation=Number(e.target.value);go('orientation');});
document.getElementById('thumbnail').addEventListener('change',e=>{state.thumbnail=e.target.checked;draw();});
document.getElementById('reset-demo').addEventListener('click',()=>{clearTimeout(timer);state=P.initial();draw();});
globalThis.portalPreview={getState:()=>JSON.parse(JSON.stringify(state)),setState:patch=>{clearTimeout(timer);state={...P.initial(),...patch};draw();},dispatch};
draw();
})();
