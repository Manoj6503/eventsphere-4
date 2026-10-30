import {useState,useEffect,useContext} from 'react';import {api} from '../api.js';import {Ctx} from '../ctx.js';
// Opens the registration form that belongs to ONE event (fields come from the server: category template + organizer's own questions)
export default function Register({data}){const {me,go,toast}=useContext(Ctx);const id=data?.id;
 const [ev,setEv]=useState(null),[a,setA]=useState({}),[done,setDone]=useState(null),[err,setErr]=useState(''),[busy,setBusy]=useState(false);
 useEffect(()=>{if(!id){setErr('No event selected');return}api('/events/'+id).then(r=>setEv(r.event)).catch(e=>setErr(e.message))},[id,me]);
 const toLogin=p=>{try{sessionStorage.setItem('es_next',JSON.stringify({name:'register',data:{id}}))}catch{}go(p)};
 if(err)return <main><div className="panel" style={{textAlign:'center'}}><h2>❌ {err}</h2><button onClick={()=>go('home')}>Back to events</button></div></main>;
 if(!ev)return <main><div className="sk"/></main>;
 const left=ev.capacity-ev.taken,mine=ev.my_registration;
 const head=<div className="meta" style={{marginBottom:10}}><span className="tag">{ev.category}</span><h2 style={{margin:'8px 0'}}>{ev.title}</h2>📅 {ev.event_date} {ev.start_time||''} · 📍 {ev.venue}<br/>by {ev.organizer} · {left>0?`${left} of ${ev.capacity} seats left`:'Full'}{ev.eligibility&&<><br/>Eligibility: {ev.eligibility}</>}</div>;
 if(done)return <main><div className="panel" style={{textAlign:'center'}}><h2>✅ Registration confirmed</h2><p>You are registered for <b>{ev.title}</b></p><p>Registration ID: <b>{done}</b></p><p className="meta">{ev.event_date} · {ev.venue}<br/>A confirmation was added to your notifications.</p>
  <button onClick={()=>go('mine')}>My registrations</button> <button className="o" onClick={()=>go('home')}>More events</button></div></main>;
 const blocked=ev.status!=='published'?'This event is not published yet (preview only).':mine?.status==='confirmed'?`You are already registered (ID ${mine.reg_code}).`:ev.open!==1?'Registration is not open for this event.':left<1?'This event is full.':me&&me.role!=='student'?'Only student accounts can register for events.':'';
 const set=(k,v)=>setA(x=>({...x,[k]:v}));
 const field=f=>{const v=a[f.key]??'',lab=<>{f.label}{f.required&&<span aria-hidden> *</span>}</>;
  if(f.type==='checkbox')return <label key={f.key} className="chk"><input type="checkbox" checked={!!a[f.key]} required={f.required} onChange={e=>set(f.key,e.target.checked)}/> <span>{lab}</span></label>;
  if(f.type==='select')return <label key={f.key}>{lab}<select value={v} required={f.required} onChange={e=>set(f.key,e.target.value)}><option value="">Select...</option>{f.options.map(o=><option key={o}>{o}</option>)}</select></label>;
  if(f.type==='textarea')return <label key={f.key}>{lab}<textarea rows={3} maxLength={600} value={v} required={f.required} onChange={e=>set(f.key,e.target.value)}/></label>;
  return <label key={f.key}>{lab}<input type={f.type==='number'?'number':f.type==='tel'?'tel':'text'} min={f.min} max={f.max} maxLength={150} value={v} required={f.required} onChange={e=>set(f.key,e.target.value)}/></label>};
 const submit=async e=>{e.preventDefault();setBusy(true);try{const r=await api(`/events/${id}/register`,'POST',{answers:a});setDone(r.reg_code)}catch(x){toast(x.message,1)}setBusy(false)};
 return <main><form className="box" onSubmit={submit}>{head}
  {blocked?<div className="res" style={{background:'#fef9c3',color:'#854d0e'}}>{blocked}{mine?.status==='confirmed'&&<> <button type="button" className="o" onClick={()=>go('mine')}>My registrations</button></>}</div>
  :<>{!me&&<div className="res" style={{background:'#ede9fe',color:'#4c1d95'}}>Log in with your student account to register. <button type="button" onClick={()=>toLogin('login')}>Login</button> <button type="button" className="o" onClick={()=>toLogin('signup')}>Sign up</button></div>}
   <h3>{ev.category} registration form</h3>
   <fieldset disabled={!me} style={{border:0,padding:0,margin:0}}>{ev.form.map(field)}
   <p className="meta">{me?<>Registering as <b>{me.name}</b> ({me.email})</>:'Sign in to submit this form.'}</p><button disabled={busy||!me}>{busy?'Submitting...':'Submit registration'}</button> <button type="button" className="o" onClick={()=>go('home')}>Cancel</button></fieldset></>}
 </form></main>}
