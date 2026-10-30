import {useState,useEffect,useContext,useRef,useCallback} from 'react';import {QRCodeSVG} from 'qrcode.react';import {Html5Qrcode} from 'html5-qrcode';
import {api} from '../api.js';import {Ctx} from '../ctx.js';

function Camera({onScan,onError}){const cb=useRef(onScan);cb.current=onScan;
 useEffect(()=>{const s=new Html5Qrcode('cam');let busy=false;
  const p=s.start({facingMode:'environment'},{fps:10,qrbox:240},t=>{if(busy)return;busy=true;cb.current(t).finally(()=>setTimeout(()=>{busy=false},2000))}).catch(e=>onError('Camera blocked, or the page is not HTTPS: '+e));
  return()=>{p.then(()=>s.stop()).catch(()=>{})}},[]);
 return <div id="cam" style={{maxWidth:360,margin:'12px auto'}}/>}

export default function Attendance(){const {me,toast}=useContext(Ctx);
 const [events,setEvents]=useState(null),[eid,setEid]=useState(''),[mode,setMode]=useState('qr'),[qr,setQr]=useState(null),[parts,setParts]=useState([]),[res,setRes]=useState(null),[manual,setManual]=useState('');
 useEffect(()=>{api(me.role==='admin'?'/admin/events':'/my/events').then(r=>{const ev=r.events.filter(e=>e.status==='published');setEvents(ev);if(ev[0])setEid(String(ev[0].id))}).catch(e=>toast(e.message,1))},[]);
 const loadParts=useCallback(()=>{if(eid)api(`/events/${eid}/participants`).then(r=>setParts(r.participants)).catch(()=>{})},[eid]);
 const loadQr=useCallback(()=>{if(eid)api(`/events/${eid}/checkin-qr`).then(setQr).catch(e=>toast(e.message,1))},[eid]);
 useEffect(()=>{loadParts();loadQr();const a=setInterval(loadParts,10000),b=setInterval(loadQr,60000);return()=>{clearInterval(a);clearInterval(b)}},[eid]);
 const check=async t=>{try{const j=await api('/attendance/check','POST',{event_id:+eid,token:t.trim()});const ok=j.result==='ok';
  setRes({kind:ok?'ok':'warn',text:`${ok?'✅ Checked in':'⚠️ Already checked in'}: ${j.name} (${j.reg_code})${j.certificate&&ok?' — certificate issued':''}${j.certificate_pending?' — certificate will be issued once the admin sets the certificate authority':''}`});loadParts()}catch(e){setRes({kind:'err',text:'❌ '+e.message})}};
 const resend=async id=>{try{toast((await api(`/certificates/${id}/resend`,'POST')).message);setTimeout(loadParts,3000)}catch(e){toast(e.message,1)}};
 const csv=()=>{const rows=[['Name','Email','RegID','Status','CheckedIn'],...parts.map(x=>[x.name,x.email,x.reg_code,x.status,x.checked_in_at||''])];
  const b=new Blob([rows.map(r=>r.map(c=>'"'+String(c).replace(/"/g,'""')+'"').join(',')).join('\n')],{type:'text/csv'});const a=document.createElement('a');a.href=URL.createObjectURL(b);a.download='attendance.csv';a.click()};
 if(!events)return <main><div className="sk"/></main>;
 if(!events.length)return <main><h2>Attendance</h2><div className="empty">No published events yet. Events appear here after the admin approves them.</div></main>;
 const colors={ok:'#dcfce7;#166534',warn:'#fef9c3;#854d0e',err:'#ffe4e6;#9f1239'};
 return <main><h2>Attendance</h2><div className="panel" style={{maxWidth:'none',textAlign:'center'}}>
  <label style={{textAlign:'left'}}>Event<select value={eid} onChange={e=>{setEid(e.target.value);setQr(null)}}>{events.map(e=><option key={e.id} value={e.id}>{e.title} ({e.event_date})</option>)}</select></label>
  <div style={{margin:'14px auto'}}>{mode==='qr'?(qr?<><h3>{qr.title}</h3><div className="qrbox"><QRCodeSVG value={`${location.origin}/#in=${eid}.${qr.token}`} size={280}/></div>
   <p><b>Students:</b> scan this QR with your phone camera and log in. Attendance and your certificate are issued instantly.</p>
   {!qr.is_today&&<p style={{color:'#be123c',fontWeight:700}}>Attendance counts only on the event date ({qr.event_date}).</p>}</>:<div className="sk"/>)
   :<Camera onScan={check} onError={m=>{toast(m,1);setMode('qr')}}/>}</div>
  <button onClick={()=>setMode('qr')}>Show attendance QR</button> <button className="o" onClick={()=>setMode('cam')}>Scan student passes with camera</button>
  {res&&<div className="res" style={{background:colors[res.kind].split(';')[0],color:colors[res.kind].split(';')[1]}}>{res.text}</div>}
  <div className="two" style={{marginTop:14}}><input placeholder="Or type registration ID (REG-...)" value={manual} onChange={e=>setManual(e.target.value)}/><button onClick={()=>check(manual)}>Check in</button></div></div>
  <h2>Participants</h2>{parts.length?<><p>{parts.filter(x=>x.status==='confirmed').length} registered · {parts.filter(x=>x.checked_in_at).length} present <button className="o" onClick={csv}>Export CSV</button></p>
  <table><thead><tr><th>Name</th><th>Reg ID</th><th>Status</th><th>Attendance</th><th>Certificate</th></tr></thead><tbody>{parts.map(x=><tr key={x.reg_code}><td>{x.name}</td><td>{x.reg_code}</td><td className="st">{x.status}</td><td>{x.checked_in_at?'Present '+x.checked_in_at:'-'}</td>
   <td>{x.cert_id?<>{x.cert_id} ({x.email_status}) {x.email_status!=='sent'&&<button className="o" onClick={()=>resend(x.cert_id)}>Retry email</button>}</>:x.checked_in_at?<small>Waiting for certificate authority</small>:'-'}</td></tr>)}</tbody></table></>:<div className="empty">No registrations yet.</div>}</main>}
