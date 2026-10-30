import {useState,useEffect,useContext} from 'react';import {api} from '../api.js';import {Ctx} from '../ctx.js';
import {MyEvents,EventForm} from './Events.jsx';import Attendance from './Attendance.jsx';import {Tabs,Answers,parseAnswers,downloadCsv} from './Shared.jsx';

function Overview(){const {go,toast}=useContext(Ctx);const [ev,setEv]=useState(null);
 useEffect(()=>{api('/my/events').then(r=>setEv(r.events)).catch(e=>toast(e.message,1))},[]);
 if(!ev)return <main><div className="sk"/></main>;
 const n=s=>ev.filter(e=>e.status===s).length,reg=ev.reduce((a,e)=>a+e.taken,0);
 return <main><div className="stats">{[['events',ev.length],['published',n('published')],['awaiting approval',n('pending')],['changes requested',n('changes_requested')],['rejected',n('rejected')],['registrations',reg]].map(([k,v])=><div className="stat" key={k}><b>{v}</b>{k}</div>)}</div>
  <button onClick={()=>go('dash',{tab:'create'})}>+ Create event</button> <button className="o" onClick={()=>go('dash',{tab:'registrations'})}>View registrations</button></main>}

// Students registered for an event, with the answers they gave on that event's form. Also used (all events) by the Admin Dashboard.
export function EventRegistrations({eid:init,admin}){const {toast}=useContext(Ctx);
 const [events,setEvents]=useState(null),[eid,setEid]=useState(init?String(init):''),[rows,setRows]=useState(null);
 useEffect(()=>{api(admin?'/admin/events':'/my/events').then(r=>{setEvents(r.events);if(!eid)setEid(admin?'all':r.events[0]?String(r.events[0].id):'')}).catch(e=>toast(e.message,1))},[]);
 useEffect(()=>{if(!eid)return;setRows(null);api(eid==='all'?'/admin/registrations':`/events/${eid}/participants`).then(r=>setRows(r.registrations||r.participants)).catch(e=>{toast(e.message,1);setRows([])})},[eid]);
 if(!events)return <main><div className="sk"/></main>;
 if(!events.length)return <main><h2>Registrations</h2><div className="empty">No events yet.</div></main>;
 const csv=()=>{const labels=[...new Set(rows.flatMap(r=>parseAnswers(r.answers).map(a=>a.label)))];
  downloadCsv('registrations.csv',[['Event','Name','Email','RegID','Status','Registered',...labels],...rows.map(r=>{const m=Object.fromEntries(parseAnswers(r.answers).map(a=>[a.label,a.value]));return [r.event||events.find(e=>String(e.id)===eid)?.title||'',r.name,r.email,r.reg_code,r.status,r.created_at,...labels.map(l=>m[l]??'')]})])};
 return <main><h2>Registrations</h2><div className="panel" style={{maxWidth:'none',marginBottom:16}}><label>Event<select value={eid} onChange={e=>setEid(e.target.value)}>{admin&&<option value="all">All events</option>}{events.map(e=><option key={e.id} value={e.id}>{e.title} ({e.event_date}) - {e.taken} registered</option>)}</select></label></div>
  {!rows?<div className="sk"/>:rows.length?<><p>{rows.filter(r=>r.status==='confirmed').length} confirmed · {rows.length} total <button className="o" onClick={csv}>Export CSV</button></p>
  <table><thead><tr>{eid==='all'&&<th>Event</th>}<th>Student</th><th>Reg ID</th><th>Status</th><th>Registered</th><th>Form details</th></tr></thead><tbody>
   {rows.map(r=><tr key={r.reg_code}>{eid==='all'&&<td>{r.event}</td>}<td>{r.name}<br/><small>{r.email}</small></td><td>{r.reg_code}</td><td className="st">{r.status}</td><td>{r.created_at}</td><td style={{minWidth:240}}><Answers s={r.answers}/></td></tr>)}</tbody></table></>
  :<div className="empty">No registrations for this event yet.</div>}</main>}

// The single Event Creator Dashboard (reuses the existing My Events, event form and attendance screens as tabs)
export default function Organizer({data}){const {go}=useContext(Ctx);const tab=data?.tab||'overview';
 return <><main style={{paddingBottom:0}}><h2>Organizer Dashboard</h2>
  <Tabs cur={tab} onPick={t=>go('dash',{tab:t})} tabs={[['overview','Overview'],['events','My Events'],['create','Create Event'],['registrations','Registrations'],['attendance','Attendance & Certificates']]}/></main>
  {tab==='overview'&&<Overview/>}{tab==='events'&&<MyEvents/>}{tab==='create'&&<EventForm key={data?.ev?.id||'new'} data={data?.ev}/>}
  {tab==='registrations'&&<EventRegistrations eid={data?.eid}/>}{tab==='attendance'&&<Attendance/>}</>}
