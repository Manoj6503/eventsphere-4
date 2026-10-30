import {useState,useEffect,useContext} from 'react';import {api} from '../api.js';import {Ctx} from '../ctx.js';import {CATS,CC} from '../common.js';
export function EventCard({e,mode,onRegister,onEdit,onRegs}){const left=e.capacity-e.taken,c=CC[e.category]||CC.Other,img=/^https?:\/\//.test(e.banner||'');
 return <div className="card"><div className="ban" style={img?{backgroundImage:`url(${JSON.stringify(e.banner)})`}:{background:`linear-gradient(135deg,${c[0]},${c[1]})`}}><span className="date">{e.event_date}</span>{!img&&<i className="em">{c[2]}</i>}</div>
  <div className="cb"><span className="tag">{e.category}</span><h3>{e.title}</h3><div className="meta">📍 {e.venue} · {e.start_time||''}</div>
  <div className="meta">by {e.organizer} · {left>0?`${left} of ${e.capacity} seats left`:'Full'}</div><div className="meta">{(e.description||'').slice(0,90)}</div>
  <div className="row">{mode==='pub'?<button disabled={left<1} onClick={()=>onRegister(e.id)}>{left<1?'Full':'Register'}</button>
   :<><span className="st">{e.status.replace('_',' ')}</span><button className="o" onClick={()=>onRegs(e)}>Registrations ({e.taken})</button><button className="o" onClick={()=>onEdit(e)}>Edit</button></>}</div></div></div>}

export function MyEvents(){const {go,toast}=useContext(Ctx);const [ev,setEv]=useState(null);
 useEffect(()=>{api('/my/events').then(r=>setEv(r.events)).catch(e=>toast(e.message,1))},[]);
 return <main><h2>My events</h2>{!ev?<div className="grid"><div className="sk"/></div>:<div className="grid">{ev.map(e=><EventCard key={e.id} e={e} mode="own" onEdit={x=>go('form',x)} onRegs={x=>go('dash',{tab:'registrations',eid:x.id})}/>)}{!ev.length&&<div className="empty">No proposals yet.</div>}</div>}</main>}

export function EventForm({data:ev}){const {go,toast}=useContext(Ctx);ev=ev||{};
 const [f,setF]=useState({title:ev.title||'',description:ev.description||'',category:ev.category||'Technical',banner:ev.banner||'',venue:ev.venue||'',event_date:ev.event_date||'',start_time:ev.start_time||'',end_time:ev.end_time||'',reg_open:ev.reg_open||'',reg_close:ev.reg_close||'',capacity:ev.capacity||50,eligibility:ev.eligibility||'',contact:ev.contact||'',form_type:ev.form_type||''});
 const [custom,setCustom]=useState(()=>{try{return JSON.parse(ev.form_fields||'[]').map(q=>({label:q.label,type:q.type,required:!!q.required,options:(q.options||[]).join(', ')}))}catch{return []}}),[tpl,setTpl]=useState(null);
 useEffect(()=>{api('/form-templates').then(setTpl).catch(()=>{})},[]);
 const setQ=(i,k,v)=>setCustom(c=>c.map((q,j)=>j===i?{...q,[k]:v}:q));
 const set=k=>e=>setF({...f,[k]:e.target.value});
 const inp=(k,l,t='text',req=true)=><label>{l}<input type={t} value={f[k]} onChange={set(k)} required={req}/></label>;
 const submit=async e=>{e.preventDefault();try{const form_fields=custom.filter(q=>q.label.trim()).map(q=>({label:q.label.trim(),type:q.type,required:q.required,options:q.type==='select'?q.options.split(',').map(x=>x.trim()).filter(Boolean):undefined}));
  const r=await api(ev.id?`/events/${ev.id}/update`:'/events','POST',{...f,form_fields});toast(r.message);go('myev')}catch(err){toast(err.message,1)}};
 return <main><form className="box" onSubmit={submit}><h2>{ev.id?'Edit event':'Propose an event'}</h2>{inp('title','Title')}
  <label>Description<textarea rows={3} value={f.description} onChange={set('description')}/></label>
  <div className="two"><label>Category<select value={f.category} onChange={set('category')}>{CATS.map(c=><option key={c}>{c}</option>)}</select></label>{inp('capacity','Capacity','number')}</div>
  {inp('banner','Banner image URL','url',false)}{inp('venue','Venue')}
  <div className="two">{inp('event_date','Event date','date')}{inp('start_time','Start','time',false)}{inp('end_time','End','time',false)}{inp('reg_open','Registration opens','date')}{inp('reg_close','Registration closes','date')}</div>
  {inp('eligibility','Eligibility','text',false)}{inp('contact','Contact details','text',false)}
  <h3 style={{marginBottom:0}}>Student registration form</h3>
  <label>Form template<select value={f.form_type} onChange={set('form_type')}><option value="">Automatic: match the category ({f.category})</option>{CATS.map(c=><option key={c} value={c}>{c} registration form</option>)}</select></label>
  {tpl&&<div className="meta">Students will be asked: {[...tpl.common,...(tpl.templates[f.form_type||f.category]||tpl.templates.Other)].map(x=>x.label+(x.required?'*':'')).join(' · ')}</div>}
  <b style={{fontSize:14}}>Extra questions (optional, up to 8)</b>
  {custom.map((q,i)=><div key={i} className="cq"><input aria-label="Question" placeholder="Question" value={q.label} maxLength={80} onChange={e=>setQ(i,'label',e.target.value)}/>
   <select aria-label="Answer type" value={q.type} onChange={e=>setQ(i,'type',e.target.value)}><option value="text">Short text</option><option value="textarea">Long text</option><option value="number">Number</option><option value="select">Dropdown</option><option value="checkbox">Checkbox</option></select>
   {q.type==='select'&&<input aria-label="Options" placeholder="Options, comma separated" value={q.options} onChange={e=>setQ(i,'options',e.target.value)}/>}
   <label className="chk"><input type="checkbox" checked={q.required} onChange={e=>setQ(i,'required',e.target.checked)}/> <span>Required</span></label><button type="button" className="d" onClick={()=>setCustom(c=>c.filter((_,j)=>j!==i))}>Remove</button></div>)}
  {custom.length<8&&<button type="button" className="o" onClick={()=>setCustom(c=>[...c,{label:'',type:'text',required:false,options:''}])}>+ Add question</button>}
  <br/><br/><button>Submit for approval</button></form></main>}
