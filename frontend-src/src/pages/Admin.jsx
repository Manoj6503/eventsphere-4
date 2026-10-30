import {useState,useEffect,useContext} from 'react';import {api} from '../api.js';import {Ctx} from '../ctx.js';
import {Tabs} from './Shared.jsx';import {EventRegistrations} from './Organizer.jsx';import Attendance from './Attendance.jsx';

function Overview({s}){const mx=Math.max(1,...s.byCategory.map(c=>c.n));
 return <main><div className="stats">{Object.entries(s.stats).map(([k,v])=><div className="stat" key={k}><b>{v}</b>{k.replace(/_/g,' ')}</div>)}</div>
  <div className="panel" style={{maxWidth:'none',marginBottom:20}}><b>Registrations by category</b>{s.byCategory.map(c=><div className="meta" key={c.category}>{c.category} ({c.n})<div className="bar" style={{width:`${c.n/mx*100}%`,minWidth:4}}/></div>)}</div></main>}

// Event creator requests: nobody becomes an organizer until the admin approves here
function Requests({reload}){const {toast}=useContext(Ctx);const [r,setR]=useState(null);
 const load=()=>api('/admin/organizer-requests').then(x=>setR(x.requests)).catch(e=>toast(e.message,1));useEffect(()=>{load()},[]);
 const review=async(id,action)=>{let reason='';if(action==='reject'){reason=prompt('Reason for rejecting (required):');if(!reason)return}
  try{toast((await api(`/admin/organizer-requests/${id}/review`,'POST',{action,reason})).message);load();reload()}catch(e){toast(e.message,1)}};
 if(!r)return <main><div className="sk"/></main>;
 return <main><h2>Event creator requests</h2>{r.length?<table><thead><tr><th>Applicant</th><th>Organization</th><th>Proposed event</th><th>Reason</th><th>Submitted</th><th>Status</th><th>Decision</th></tr></thead><tbody>
  {r.map(x=><tr key={x.id}><td>{x.name}<br/><small>{x.email}</small></td><td>{x.organization}</td><td><b>{x.event_title}</b><br/><small>{x.event_category}{x.expected_date&&' · '+x.expected_date}</small><br/><small>{x.event_description}</small></td><td style={{minWidth:180}}><small>{x.reason}</small></td><td>{x.created_at}</td>
   <td className="st">{x.status}{x.admin_note&&<><br/><small style={{fontWeight:400,color:'#475569'}}>{x.admin_note}</small></>}</td>
   <td>{x.status==='pending'?<><button onClick={()=>review(x.id,'approve')}>Approve</button> <button className="d" onClick={()=>review(x.id,'reject')}>Reject</button></>:'-'}</td></tr>)}</tbody></table>:<div className="empty">No organizer requests yet.</div>}</main>}

function Organizers({reload}){const {toast}=useContext(Ctx);const [o,setO]=useState(null);
 const load=()=>api('/admin/organizers').then(x=>setO(x.organizers)).catch(e=>toast(e.message,1));useEffect(()=>{load()},[]);
 const revoke=async x=>{const reason=prompt(`Remove organizer access for ${x.name}? Optional reason:`);if(reason===null)return;
  try{toast((await api(`/admin/organizers/${x.id}/revoke`,'POST',{reason})).message);load();reload()}catch(e){toast(e.message,1)}};
 if(!o)return <main><div className="sk"/></main>;
 return <main><h2>Approved event organizers</h2>{o.length?<table><thead><tr><th>Name</th><th>Email</th><th>Events</th><th>Registrations</th><th>Since</th><th></th></tr></thead><tbody>
  {o.map(x=><tr key={x.id}><td>{x.name}</td><td>{x.email}</td><td>{x.events}</td><td>{x.registrations}</td><td>{x.created_at}</td><td><button className="d" onClick={()=>revoke(x)}>Remove access</button></td></tr>)}</tbody></table>:<div className="empty">No approved organizers yet.</div>}</main>}

function Events(){const {toast}=useContext(Ctx);const [l,setL]=useState(null);
 const load=()=>api('/admin/events').then(x=>setL(x.events)).catch(e=>toast(e.message,1));useEffect(()=>{load()},[]);
 const review=async(id,action)=>{let reason='';if(action!=='approve'){reason=prompt('Reason (required):');if(!reason)return}
  try{toast((await api(`/admin/events/${id}/review`,'POST',{action,reason})).message);load()}catch(e){toast(e.message,1)}};
 if(!l)return <main><div className="sk"/></main>;
 return <main><h2>Events</h2><table><thead><tr><th>Event</th><th>Organizer</th><th>Date</th><th>Seats</th><th>Status</th><th>Review</th></tr></thead><tbody>{l.map(e=><tr key={e.id}><td>{e.title}</td><td>{e.organizer}</td><td>{e.event_date}</td><td>{e.taken}/{e.capacity}</td><td className="st">{e.status.replace('_',' ')}</td>
   <td><button onClick={()=>review(e.id,'approve')}>Approve</button> <button className="o" onClick={()=>review(e.id,'changes')}>Changes</button> <button className="d" onClick={()=>review(e.id,'reject')}>Reject</button></td></tr>)}</tbody></table></main>}

// Certificate authority: name + designation (+ optional uploaded signature image). Certificates are signed only with what is configured here.
function CertSettings(){const {toast}=useContext(Ctx);const [g,setG]=useState(null),[name,setName]=useState(''),[des,setDes]=useState(''),[sig,setSig]=useState(''),[clr,setClr]=useState(false);
 const load=()=>api('/admin/certificate-settings').then(x=>{setG(x);setName(x.authority_name);setDes(x.designation);setSig('');setClr(false)}).catch(e=>toast(e.message,1));useEffect(()=>{load()},[]);
 // any image type is converted to a JPEG on a white background (what the PDF embeds)
 const pick=file=>{if(!file)return;const rd=new FileReader();rd.onload=()=>{const im=new Image();im.onload=()=>{const k=Math.min(1,600/im.width,240/im.height),c=document.createElement('canvas');c.width=Math.max(1,Math.round(im.width*k));c.height=Math.max(1,Math.round(im.height*k));
  const x=c.getContext('2d');x.fillStyle='#fff';x.fillRect(0,0,c.width,c.height);x.drawImage(im,0,0,c.width,c.height);setSig(c.toDataURL('image/jpeg',.92));setClr(false)};im.onerror=()=>toast('That file is not a readable image',1);im.src=rd.result};rd.readAsDataURL(file)};
 const save=async e=>{e.preventDefault();try{const r=await api('/admin/certificate-settings','POST',{authority_name:name,designation:des,signature:sig||undefined,clear_signature:clr});
  toast(`${r.message}${r.issued?` · ${r.issued} pending certificate(s) issued`:''}${r.reissued?` · ${r.reissued} earlier certificate(s) updated`:''}`);load()}catch(x){toast(x.message,1)}};
 if(!g)return <main><div className="sk"/></main>;const shown=clr?'':sig||g.signature;
 return <main><form className="box" onSubmit={save}><h2>Certificate authority</h2>
  <div className="res" style={g.configured?{background:'#dcfce7',color:'#166534'}:{background:'#fef9c3',color:'#854d0e'}}>{g.configured?'✅ Certificates are signed by the authority below.':'⚠️ Not configured. Attendance is recorded, but certificates are issued only after you save an authority here.'}</div>
  <label>Authority name<input value={name} maxLength={60} onChange={e=>setName(e.target.value)} placeholder="e.g. Dr. A. Kumar" required/></label>
  <label>Designation<input value={des} maxLength={80} onChange={e=>setDes(e.target.value)} placeholder="e.g. Principal" required/></label>
  <label>Signature image (optional, PNG or JPG of the authority's own signature)<input type="file" accept="image/*" onChange={e=>pick(e.target.files[0])}/></label>
  {shown&&<div style={{margin:'10px 0'}}><img src={shown} alt="Signature preview" style={{maxWidth:260,maxHeight:100,background:'#fff',border:'1px solid #ddd6fe',borderRadius:8}}/><br/>{!sig&&<label className="chk"><input type="checkbox" checked={clr} onChange={e=>setClr(e.target.checked)}/> <span>Remove the saved signature image</span></label>}</div>}
  <p className="meta">Only upload a signature you are authorised to use. Certificates already issued keep the authority they were signed with.</p><button>Save certificate authority</button></form></main>}

export default function Admin({data}){const {go,toast}=useContext(Ctx);const tab=data?.tab||'overview';const [s,setS]=useState(null);
 const reload=()=>api('/admin/stats').then(setS).catch(e=>toast(e.message,1));useEffect(()=>{reload()},[]);
 return <><main style={{paddingBottom:0}}><h2>Admin Dashboard</h2>
  <Tabs cur={tab} onPick={t=>go('admin',{tab:t})} tabs={[['overview','Overview'],['requests','Event Creator Requests',s?.stats.organizer_requests],['organizers','Organizers'],['events','Events'],['registrations','Registrations'],['attendance','Attendance'],['certificate','Certificate Authority']]}/></main>
  {tab==='overview'&&(s?<Overview s={s}/>:<main><div className="sk"/></main>)}{tab==='requests'&&<Requests reload={reload}/>}{tab==='organizers'&&<Organizers reload={reload}/>}{tab==='events'&&<Events/>}
  {tab==='registrations'&&<EventRegistrations admin/>}{tab==='attendance'&&<Attendance/>}{tab==='certificate'&&<CertSettings/>}</>}
